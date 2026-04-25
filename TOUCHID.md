# Touch ID Support

## Current State

This branch has two Touch ID-related paths on macOS:

1. `touchid-check` plus `contrib/shell-guards.zsh`
   This is the practical path for unsigned or Homebrew-style installs. It prompts
   with Touch ID or password before `envchain` runs, then lets the existing
   allowlist and fingerprint checks decide whether the target binary receives
   secrets.

2. `envchain --set -b`
   This stores a Keychain item with a biometric-style access control using
   `kSecAccessControlUserPresence`. This path depends on the build being signed
   correctly for biometric Keychain ACLs. Unsigned or ad-hoc signed builds
   usually fail preflight with `errSecMissingEntitlement`.

## Recommended Path

For normal local CLI usage, prefer the helper path:

```bash
touchid-check
source contrib/shell-guards.zsh
```

Then call `_require_touchid` before the shell function that eventually invokes
`envchain`.

## Helper Build And Install

On macOS, `make` builds `touchid-check` when `swiftc` is available, and
`make install` installs it alongside `envchain`.

If `swiftc` is not on `PATH`, build it explicitly:

```bash
swiftc -O -o touchid-check touchid-check.swift
```

The shell guards expect `touchid-check` to be on `PATH`.

## Shell Guards

`contrib/shell-guards.zsh` adds three shell-side checks:

- `_require_touchid`: prompt for Touch ID or password via `touchid-check`
- `_verify_binary`: verify a SHA-256 fingerprint for the binary the shell will run
- `envchain` allowlist: enforce the C-level allowlist inside envchain

Example:

```zsh
source /path/to/contrib/shell-guards.zsh

my_tool() {
  _require_touchid || return 1
  _verify_binary my_tool || return 1
  envchain my-namespace my_tool "$@"
}
```

## `--set -b` Behavior

```bash
envchain --set -b my-namespace MY_SECRET
```

When `-b` is used on macOS:

- envchain first creates a biometric/user-presence access control object
- it then performs a temporary Keychain preflight write to detect unsupported
  or unsigned builds before touching the real secret
- if the build is not signed correctly, the command fails with an actionable
  message and does not modify the real item
- if an existing item must be migrated and the biometric re-add fails, envchain
  restores a normal Keychain item from the just-entered value so the secret is
  not lost

The current read path is unchanged. The stored item's Keychain ACL determines
which prompt macOS shows during access.

## Platform Notes

- macOS:
  - `touchid-check` uses `.deviceOwnerAuthentication`, so Touch ID can fall back
    to password when biometrics are unavailable or rejected.
  - `--set -b` is available, but only signed builds are expected to support the
    biometric Keychain ACL path.

- Linux:
  - `--biometric` is not supported.
  - The Linux implementation returns a clear runtime error instead of failing at
    link time.

## Security Model

These layers are independent:

- Touch ID or password via `touchid-check`: authenticate the human
- binary fingerprint verification: authenticate the binary the shell resolved
- envchain allowlist: authorize which binary may receive secrets
- `nono` or another sandbox: constrain what the child process can do afterward

Each layer checks a different part of the flow. None of them replaces the
others.
