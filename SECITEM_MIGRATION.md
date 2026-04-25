# SecItem API Migration Plan

*Revised after review — reordered phases around the actual exec path,
separated API modernization from behavioral changes, corrected the
search/enumeration framing.*

## Problem

envchain_osx.c uses the legacy SecKeychain API (deprecated since macOS 10.10):

```
SecKeychainAddGenericPassword     → stores items
SecKeychainFindGenericPassword    → reads items
SecKeychainItemModifyAttributesAndData → updates items
SecKeychainItemCopyAccess         → reads ACLs
SecACLCopyContents / SecACLSetContents → manages ACLs
SecKeychainItemDelete             → deletes items
SecTrustedApplicationCreateFromPath → creates trusted app refs
```

These produce 10+ deprecation warnings on every build and block native
Touch ID on Keychain reads (the legacy API always shows a password dialog).

## Goal

Replace all legacy `SecKeychain*` calls with the modern `SecItem*` equivalents.

**API modernization only.** No CLI semantic changes in this PR. The `-p`/`-P`
flags keep their current behavior. The `-b` flag (from PR #2) continues to
work as-is. Behavioral changes (like unifying `-p` and `-b`) are a separate
discussion after the API layer is clean.

After migration:
- Zero deprecation warnings from envchain_osx.c
- Items stored with `--set -b` trigger native Touch ID on read (for signed builds)
- Existing items continue to work exactly as before
- `touchid-check` helper remains the practical path for unsigned/Homebrew builds

## Signing Reality

Native Touch ID on Keychain reads only works when:
1. The item was stored with `kSecAccessControlUserPresence` (`--set -b`)
2. The envchain binary is signed with an Apple Developer certificate

For unsigned/ad-hoc/Homebrew builds, `SecItemCopyMatching` reads items
silently (no prompt at all) — same as current behavior. The `touchid-check`
helper from PR #2 remains the practical Touch ID path for these builds.

This migration does not solve the signed-distribution story. It modernizes
the API so that signed builds get native Touch ID for free.

## API Mapping

| Legacy (current) | Modern (target) | Notes |
|-------------------|-----------------|-------|
| `SecKeychainAddGenericPassword` | `SecItemAdd` | Already used in biometric path |
| `SecKeychainFindGenericPassword` | `SecItemCopyMatching` | Used by save/delete to check existence |
| `SecKeychainItemModifyAttributesAndData` | `SecItemUpdate` | For value + attribute updates |
| `SecKeychainItemDelete` | `SecItemDelete` | Already used in biometric path |
| `SecKeychainItemCopyAccess` | `kSecAttrAccessControl` | Set at creation, not mutable after |
| `SecACLCopyContents` / `SecACLSetContents` | N/A | Legacy ACL model not used in modern API |
| `SecTrustedApplicationCreateFromPath` | N/A | Modern API uses access groups instead |
| `SecKeychainItemFreeContent` | `CFRelease` | Standard CF memory management |
| `SecKeychainSearchCreateFromAttributes` | `SecItemCopyMatching` | With `kSecMatchLimitAll` |
| `SecKeychainSearchCopyNext` | N/A | Results returned as CFArray |

## Corrected Phase Order

The original spec ordered phases around `envchain_find_value()`. That
function is only used by save/delete paths to check item existence. The
actual exec-time secret injection goes through `envchain_search_values()`.
Phases are reordered to deliver value on the real hot path first.

### Phase 1: Exec-time secret injection (`envchain_search_values`)

This is the function called by `envchain_exec()` on every `envchain ns cmd`
invocation. It enumerates all items in a namespace and invokes the callback
with each key/value pair.

**Current**: Uses `SecKeychainSearchCreateFromAttributes` /
`SecKeychainSearchCopyNext` loop with `SecKeychainItemCopyContent`.

**Target**: `SecItemCopyMatching` with exact `kSecAttrService` match and
`kSecMatchLimitAll`.

```c
CFMutableDictionaryRef query = CFDictionaryCreateMutable(...);
CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
CFDictionarySetValue(query, kSecAttrService, service_name_cf);
CFDictionarySetValue(query, kSecReturnAttributes, kCFBooleanTrue);
CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitAll);

CFTypeRef results = NULL;
OSStatus status = SecItemCopyMatching(query, &results);
// iterate CFArrayRef of dictionaries, extract kSecAttrAccount + kSecValueData
```

The service name is already exact (`envchain-<namespace>`), not a prefix.
No filtering needed — this is a direct lookup.

When an item has `kSecAccessControlUserPresence`, `SecItemCopyMatching`
triggers the native Touch ID dialog automatically. This is the main win.

### Phase 2: Namespace enumeration (`envchain_search_namespaces`)

Lists all envchain namespaces for `envchain --list`.

**Current**: Same legacy search API, filtering by description `"envchain"`.

**Target**: `SecItemCopyMatching` with `kSecAttrDescription` = `"envchain"`
and `kSecMatchLimitAll`. Extract unique namespace suffixes from
`kSecAttrService` values.

```c
CFDictionarySetValue(query, kSecAttrDescription, CFSTR("envchain"));
CFDictionarySetValue(query, kSecReturnAttributes, kCFBooleanTrue);
CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitAll);
```

Items are already tagged with description `"envchain"` by the save path.
This is exact-match on description, not a prefix search. The spec previously
overstated the difficulty.

### Phase 3: Existence check (`envchain_find_value`)

Used by save/delete to check if an item exists before adding/updating.

**Target**: `SecItemCopyMatching` with exact service + account, no data
return (just check `errSecSuccess` vs `errSecItemNotFound`).

```c
CFDictionarySetValue(query, kSecAttrService, svc);
CFDictionarySetValue(query, kSecAttrAccount, acct);
// no kSecReturnData — just checking existence
OSStatus status = SecItemCopyMatching(query, NULL);
```

### Phase 4: Write path (`envchain_save_value`)

**Current**: `SecKeychainAddGenericPassword` + `SecKeychainItemModifyAttributesAndData`

**Target**: `SecItemAdd` for new items, `SecItemUpdate` for existing.
The biometric path already uses `SecItemAdd` — align the non-biometric path.

### Phase 5: ACL/passphrase handling (`-p` / `-P` flags)

**Current behavior preserved exactly**:
- `-p` (require passphrase): currently manipulates legacy ACL via
  `SecACLSetContents` with `kSecKeychainPromptRequirePassphase`
- `-P` (no require passphrase): sets self-trusted app list via
  `SecTrustedApplicationCreateFromPath`

**Target**: Map to modern equivalents:
- `-p`: store with `kSecAttrAccessControl` = `kSecAccessControlUserPresence`
  (prompts for Touch ID or password on access)
- `-P`: store with `kSecAttrAccess` using the creating app as the trusted
  application (preserves the current self-trusted / no-prompt behavior more
  closely than a plain item with no access attribute)

This preserves the user-facing semantics: `-p` means "prompt me", `-P`
means "don't prompt me." The implementation changes from legacy ACLs to
modern item attributes, but the behavior is the same.

**Note**: `-p` and `-b` become functionally equivalent after migration
(both use `kSecAccessControlUserPresence`). Whether to unify them into
one flag is a separate CLI design decision, not part of this migration.

### Phase 6: Cleanup

Remove `SecKeychainRef envchain_keychain`, all `SecKeychain*` calls,
`SecACL*` calls, `SecTrustedApplication*` calls, and the legacy search
iteration code. A small `SecAccessCreate` compatibility shim remains for
`-P` until that behavior is redesigned separately.

## Compatibility

### Existing items

Items stored by the legacy API are readable by `SecItemCopyMatching`.
Apple maintains backward compatibility. No migration needed.

### Rollback

Items stored by the modern API are accessible via the legacy API.
The underlying Keychain storage format is the same.

### Linux

No changes. `envchain_linux.c` continues to use `libsecret`.

## Risks

1. **ACL behavior on unsigned builds**: `SecItemCopyMatching` may behave
   differently for unsigned builds regarding prompts. Needs testing with
   ad-hoc signed Homebrew bottles to confirm silent reads still work.

2. **`-p` semantic shift**: the legacy ACL model allowed fine-grained
   "require passphrase for this specific operation" control. The modern
   `kSecAccessControlUserPresence` is all-or-nothing. If anyone depends
   on the difference, this is a breaking change. Mitigate by documenting
   clearly.

## Estimated Scope

- Phase 1 (search_values): ~80 lines changed
- Phase 2 (search_namespaces): ~40 lines changed
- Phase 3 (find_value): ~30 lines changed
- Phase 4 (save_value): ~60 lines changed
- Phase 5 (ACL): ~50 lines changed
- Phase 6 (cleanup): ~150 lines removed
- Total: ~400 lines touched, net reduction of ~100 lines
