#include <mach-o/dyld.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "envchain.h"

#define ENVCHAIN_SERVICE_PREFIX "envchain-"
#define ENVCHAIN_ITEM_DESCRIPTION "envchain"

/* Legacy SecKeychainRef removed — using modern SecItem API */

/* misc */

static int
envchain_sortcmp_str(const void *a, const void *b)
{
  return strcmp(*(const char**)a, *(const char**)b);
}

static void
envchain_fail_osstatus(OSStatus status)
{
  CFStringRef str;
  const char *cstr;
  str = SecCopyErrorMessageString(status, NULL);
  cstr = CFStringGetCStringPtr(str, kCFStringEncodingMacRoman);
  if (cstr == NULL) {
    fprintf(stderr, "Error: %d\n", (int)status);
  }
  else {
    fprintf(stderr, "Error: %s\n", cstr);
  }
  CFRelease(str);
  exit(10);
}


static CFStringRef
envchain_generate_service_name_cf(const char *name)
{
  return CFStringCreateWithFormat(
      NULL, NULL,
      CFSTR("%s%s"), ENVCHAIN_SERVICE_PREFIX, name
  );
}

static void
envchain_log_cferror(const char *message, CFErrorRef error)
{
  CFStringRef desc = NULL;
  char buf[1024];

  if (error == NULL) {
    fprintf(stderr, "%s\n", message);
    return;
  }

  desc = CFErrorCopyDescription(error);
  if (desc != NULL &&
      CFStringGetCString(desc, buf, sizeof(buf), kCFStringEncodingUTF8)) {
    fprintf(stderr, "%s: %s\n", message, buf);
  }
  else {
    fprintf(stderr, "%s\n", message);
  }

  if (desc != NULL) CFRelease(desc);
}

static OSStatus
envchain_create_user_presence_acl(SecAccessControlRef *acl_out)
{
  CFErrorRef error = NULL;
  SecAccessControlRef acl = NULL;

  acl = SecAccessControlCreateWithFlags(
    kCFAllocatorDefault,
    kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
    kSecAccessControlUserPresence,
    &error);
  if (acl == NULL) {
    envchain_log_cferror("Error creating prompting access control", error);
    if (error != NULL) CFRelease(error);
    return errSecParam;
  }

  if (error != NULL) CFRelease(error);
  *acl_out = acl;
  return errSecSuccess;
}

static OSStatus
envchain_create_self_trusted_access(CFStringRef desc, SecAccessRef *access_out)
{
  OSStatus status;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  status = SecAccessCreate(desc, NULL, access_out);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  return status;
}

static OSStatus
envchain_preflight_access_mode(CFStringRef desc, int require_passphrase)
{
  OSStatus status = errSecSuccess;

  if (require_passphrase == 1) {
    SecAccessControlRef acl = NULL;

    status = envchain_create_user_presence_acl(&acl);
    if (acl != NULL) CFRelease(acl);
  }
  else if (require_passphrase == 0) {
    SecAccessRef access = NULL;

    status = envchain_create_self_trusted_access(desc, &access);
    if (access != NULL) CFRelease(access);
  }

  return status;
}

static OSStatus
envchain_add_item_with_access(CFStringRef svc, CFStringRef acct,
                              CFDataRef val, CFStringRef desc,
                              int require_passphrase)
{
  OSStatus status = errSecSuccess;
  CFMutableDictionaryRef add = NULL;
  SecAccessControlRef acl = NULL;
  SecAccessRef access = NULL;

  add = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(add, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(add, kSecAttrService, svc);
  CFDictionarySetValue(add, kSecAttrAccount, acct);
  CFDictionarySetValue(add, kSecValueData, val);
  CFDictionarySetValue(add, kSecAttrDescription, desc);

  if (require_passphrase == 1) {
    status = envchain_create_user_presence_acl(&acl);
    if (status != errSecSuccess) goto cleanup;
    CFDictionarySetValue(add, kSecAttrAccessControl, acl);
  }
  else if (require_passphrase == 0) {
    status = envchain_create_self_trusted_access(desc, &access);
    if (status != errSecSuccess) goto cleanup;
    CFDictionarySetValue(add, kSecAttrAccess, access);
  }

  status = SecItemAdd(add, NULL);

cleanup:
  if (acl != NULL) CFRelease(acl);
  if (access != NULL) CFRelease(access);
  if (add != NULL) CFRelease(add);
  return status;
}

static OSStatus
envchain_copy_item_data(CFStringRef svc, CFStringRef acct, CFDataRef *data_out)
{
  OSStatus status;
  CFTypeRef result = NULL;
  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, svc);
  CFDictionarySetValue(query, kSecAttrAccount, acct);
  CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

  status = SecItemCopyMatching(query, &result);
  CFRelease(query);

  if (status != errSecSuccess) return status;

  *data_out = (CFDataRef)result;
  return errSecSuccess;
}

/* Legacy code removed: envchain_get_self_path, envchain_self_trusted_app_list,
   envchain_search_values_applier, envchain_search_namespaces_uniqufier.
   All read/write/search paths now use modern SecItem API. */

int
envchain_search_namespaces(envchain_namespace_search_callback callback, void *data)
{
  OSStatus status;
  CFTypeRef results = NULL;
  CFStringRef description = CFStringCreateWithCString(
    NULL, ENVCHAIN_ITEM_DESCRIPTION, kCFStringEncodingUTF8);

  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrDescription, description);
  CFDictionarySetValue(query, kSecReturnAttributes, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitAll);

  status = SecItemCopyMatching(query, &results);
  CFRelease(query);
  CFRelease(description);

  if (status == errSecItemNotFound) return 0;
  if (status != errSecSuccess) {
    envchain_fail_osstatus(status);
    return 0;
  }

  CFArrayRef items = (CFArrayRef)results;
  CFIndex count = CFArrayGetCount(items);
  size_t prefix_len = strlen(ENVCHAIN_SERVICE_PREFIX);

  /* Extract unique namespace names from service attributes */
  char **names = malloc(sizeof(char*) * count);
  int name_count = 0;

  for (CFIndex i = 0; i < count; i++) {
    CFDictionaryRef item = CFArrayGetValueAtIndex(items, i);
    CFStringRef svc_cf = CFDictionaryGetValue(item, kSecAttrService);
    if (svc_cf == NULL) continue;

    char svc_buf[1024];
    if (!CFStringGetCString(svc_cf, svc_buf, sizeof(svc_buf), kCFStringEncodingUTF8))
      continue;

    if (strncmp(svc_buf, ENVCHAIN_SERVICE_PREFIX, prefix_len) != 0)
      continue;

    names[name_count] = strdup(svc_buf + prefix_len);
    name_count++;
  }

  CFRelease(items);

  /* Sort and deduplicate */
  qsort(names, name_count, sizeof(char*), envchain_sortcmp_str);
  char *prev = NULL;
  for (int i = 0; i < name_count; i++) {
    if (!prev || strcmp(prev, names[i]) != 0)
      callback(names[i], data);
    prev = names[i];
  }
  for (int i = 0; i < name_count; i++) free(names[i]);
  free(names);

  return 0;
}

int
envchain_search_values(const char *name, envchain_search_callback callback, void *data)
{
  OSStatus status;
  CFStringRef service_name = envchain_generate_service_name_cf(name);
  CFTypeRef results = NULL;

  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, service_name);
  CFDictionarySetValue(query, kSecReturnAttributes, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitAll);

  status = SecItemCopyMatching(query, &results);

  if (status == errSecItemNotFound) {
    fprintf(stderr,
      "WARNING: namespace `%s` not defined.\n"
      "         You can set via running `%s --set %s SOME_ENV_NAME`.\n\n",
      name, envchain_name, name
    );
    CFRelease(query);
    CFRelease(service_name);
    return 1;
  }
  if (status != errSecSuccess) {
    CFRelease(query);
    CFRelease(service_name);
    envchain_fail_osstatus(status);
    return 1;
  }

  CFArrayRef items = (CFArrayRef)results;
  CFIndex count = CFArrayGetCount(items);

  for (CFIndex i = 0; i < count; i++) {
    CFDictionaryRef item = CFArrayGetValueAtIndex(items, i);

    CFStringRef acct_cf = CFDictionaryGetValue(item, kSecAttrAccount);
    CFDataRef val_cf = CFDictionaryGetValue(item, kSecValueData);

    if (acct_cf == NULL || val_cf == NULL) continue;

    char acct_buf[1024];
    if (!CFStringGetCString(acct_cf, acct_buf, sizeof(acct_buf), kCFStringEncodingUTF8))
      continue;

    CFIndex val_len = CFDataGetLength(val_cf);
    char *value = malloc(val_len + 1);
    if (value == NULL) continue;
    memcpy(value, CFDataGetBytePtr(val_cf), val_len);
    value[val_len] = '\0';

    callback(acct_buf, value, data);

    memset(value, 0, val_len);
    free(value);
  }

  CFRelease(items);
  CFRelease(query);
  CFRelease(service_name);

  return 0;
}

void
envchain_save_value(const char *name, const char *key, char *value, int require_passphrase)
{
  CFStringRef svc = envchain_generate_service_name_cf(name);
  CFStringRef acct = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
  CFDataRef val = CFDataCreate(NULL, (const UInt8 *)value, strlen(value));
  CFStringRef desc = CFStringCreateWithCString(
    NULL, ENVCHAIN_ITEM_DESCRIPTION, kCFStringEncodingUTF8);
  CFDataRef old_val = NULL;
  OSStatus status;
  OSStatus restore_status;

  /* Build base query for existence check */
  CFMutableDictionaryRef match = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(match, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(match, kSecAttrService, svc);
  CFDictionarySetValue(match, kSecAttrAccount, acct);

  status = SecItemCopyMatching(match, NULL);

  if (status == errSecItemNotFound) {
    status = envchain_add_item_with_access(svc, acct, val, desc, require_passphrase);
  }
  else if (status == errSecSuccess) {
    if (require_passphrase < 0) {
      /* Existing item — update value in place */
      CFMutableDictionaryRef update = CFDictionaryCreateMutable(
        NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
      CFDictionarySetValue(update, kSecValueData, val);
      CFDictionarySetValue(update, kSecAttrDescription, desc);

      status = SecItemUpdate(match, update);
      CFRelease(update);
    }
    else {
      status = envchain_preflight_access_mode(desc, require_passphrase);
      if (status != errSecSuccess) goto cleanup;

      status = envchain_copy_item_data(svc, acct, &old_val);
      if (status != errSecSuccess) goto cleanup;

      status = SecItemDelete(match);
      if (status != errSecSuccess) goto cleanup;

      status = envchain_add_item_with_access(svc, acct, val, desc, require_passphrase);
      if (status != errSecSuccess) {
        fprintf(stderr,
          "%s: failed to update item access mode, restoring previous value without prompt\n",
          envchain_name);
        restore_status = envchain_add_item_with_access(svc, acct, old_val, desc, 0);
        if (restore_status != errSecSuccess) {
          fprintf(stderr, "%s: failed to restore previous item: %d\n",
                  envchain_name, (int)restore_status);
        }
      }
    }
  }

cleanup:
  CFRelease(match);
  CFRelease(svc);
  CFRelease(acct);
  CFRelease(val);
  CFRelease(desc);
  if (old_val != NULL) CFRelease(old_val);

  if (status != errSecSuccess) envchain_fail_osstatus(status);
}

void
envchain_delete_value(const char *name, const char *key)
{
  CFStringRef svc = envchain_generate_service_name_cf(name);
  CFStringRef acct = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);

  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, svc);
  CFDictionarySetValue(query, kSecAttrAccount, acct);

  SecItemDelete(query);

  CFRelease(query);
  CFRelease(svc);
  CFRelease(acct);
}

int
envchain_path_is_native_binary(const char *path)
{
  FILE *fp = fopen(path, "rb");
  UInt32 magic;

  if (fp == NULL) return 0;
  if (fread(&magic, sizeof(magic), 1, fp) != 1) {
    fclose(fp);
    return 0;
  }
  fclose(fp);

  return magic == MH_MAGIC || magic == MH_CIGAM ||
         magic == MH_MAGIC_64 || magic == MH_CIGAM_64 ||
         magic == FAT_MAGIC || magic == FAT_CIGAM ||
         magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64;
}

static char *
envchain_hex_prefixed(const unsigned char *bytes, size_t len, const char *prefix)
{
  size_t prefix_len = strlen(prefix);
  char *result = malloc(prefix_len + (len * 2) + 1);
  size_t i;

  if (result == NULL) return NULL;

  memcpy(result, prefix, prefix_len);
  for (i = 0; i < len; i++) {
    snprintf(result + prefix_len + (i * 2), 3, "%02x", bytes[i]);
  }
  result[prefix_len + (len * 2)] = '\0';
  return result;
}

static char *
envchain_sha256_file(const char *path)
{
  FILE *fp = fopen(path, "rb");
  CC_SHA256_CTX ctx;
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  unsigned char buf[8192];
  size_t nread;

  if (fp == NULL) return NULL;

  CC_SHA256_Init(&ctx);
  while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
    CC_SHA256_Update(&ctx, buf, (CC_LONG)nread);
  }
  if (ferror(fp)) {
    fclose(fp);
    return NULL;
  }

  CC_SHA256_Final(digest, &ctx);
  fclose(fp);

  return envchain_hex_prefixed(digest, sizeof(digest), "sha256-file:");
}

char *
envchain_binary_fingerprint(const char *path)
{
  CFURLRef url = NULL;
  SecStaticCodeRef code = NULL;
  CFDictionaryRef info = NULL;
  char *result = NULL;

  url = CFURLCreateFromFileSystemRepresentation(
    NULL, (const UInt8*)path, strlen(path), 0
  );
  if (url != NULL) {
    OSStatus status = SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &code);
    if (status == noErr && code != NULL) {
      status = SecCodeCopySigningInformation(code, kSecCSSigningInformation, &info);
      if (status == noErr && info != NULL) {
        CFDataRef unique = CFDictionaryGetValue(info, kSecCodeInfoUnique);
        if (unique != NULL && CFGetTypeID(unique) == CFDataGetTypeID()) {
          const unsigned char *bytes = CFDataGetBytePtr(unique);
          CFIndex len = CFDataGetLength(unique);
          const char *prefix = "codesign-unique:";

          if (len == 20) prefix = "cdhash-sha1:";
          else if (len == 32) prefix = "cdhash-sha256:";

          if (bytes != NULL && len > 0) {
            result = envchain_hex_prefixed(bytes, (size_t)len, prefix);
          }
        }
      }
    }
  }

  if (info != NULL) CFRelease(info);
  if (code != NULL) CFRelease(code);
  if (url != NULL) CFRelease(url);

  if (result != NULL) return result;
  return envchain_sha256_file(path);
}

int
envchain_save_value_biometric(const char *name, const char *key, char *value)
{
  CFStringRef svc = envchain_generate_service_name_cf(name);
  CFErrorRef error = NULL;

  if (svc == NULL) {
    return 1;
  }

  /* Preflight: test if biometric ACL creation works on this build */
  SecAccessControlRef access = SecAccessControlCreateWithFlags(
    kCFAllocatorDefault,
    kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
    kSecAccessControlUserPresence,
    &error
  );

  if (error != NULL || access == NULL) {
    fprintf(stderr, "%s: biometric access control not available on this build\n",
            envchain_name);
    if (error) CFRelease(error);
    CFRelease(svc);
    return 1;
  }

  /* Preflight: try adding a temporary test item to verify signing support */
  CFStringRef test_svc = CFStringCreateWithCString(
    NULL, "envchain-biometric-preflight", kCFStringEncodingUTF8);
  CFStringRef test_acct = CFStringCreateWithCString(
    NULL, "preflight-test", kCFStringEncodingUTF8);
  CFDataRef test_val = CFDataCreate(NULL, (const UInt8 *)"test", 4);

  CFMutableDictionaryRef preflight = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(preflight, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(preflight, kSecAttrService, test_svc);
  CFDictionarySetValue(preflight, kSecAttrAccount, test_acct);
  CFDictionarySetValue(preflight, kSecValueData, test_val);
  CFDictionarySetValue(preflight, kSecAttrAccessControl, access);

  OSStatus status = SecItemAdd(preflight, NULL);

  /* Clean up preflight item regardless of outcome */
  CFMutableDictionaryRef del_preflight = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(del_preflight, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(del_preflight, kSecAttrService, test_svc);
  CFDictionarySetValue(del_preflight, kSecAttrAccount, test_acct);
  SecItemDelete(del_preflight);
  CFRelease(del_preflight);
  CFRelease(preflight);
  CFRelease(test_svc);
  CFRelease(test_acct);
  CFRelease(test_val);

  if (status == errSecMissingEntitlement) {
    fprintf(stderr,
      "%s: this build is not signed for biometric keychain ACLs\n"
      "  Use the touchid-check helper instead (see contrib/shell-guards.zsh)\n"
      "  Or sign envchain with an Apple Developer certificate\n",
      envchain_name);
    CFRelease(access);
    CFRelease(svc);
    return 1;
  }
  if (status != errSecSuccess && status != errSecDuplicateItem) {
    fprintf(stderr, "%s: biometric preflight failed: %d\n",
            envchain_name, (int)status);
    CFRelease(access);
    CFRelease(svc);
    return 1;
  }

  /* Preflight passed — safe to attempt the real item */
  CFStringRef acct = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
  CFDataRef val = CFDataCreate(NULL, (const UInt8 *)value, strlen(value));
  CFStringRef desc = CFStringCreateWithCString(
    NULL, ENVCHAIN_ITEM_DESCRIPTION, kCFStringEncodingUTF8);

  /* Try adding first (works if item doesn't exist yet) */
  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, svc);
  CFDictionarySetValue(query, kSecAttrAccount, acct);
  CFDictionarySetValue(query, kSecValueData, val);
  CFDictionarySetValue(query, kSecAttrAccessControl, access);
  CFDictionarySetValue(query, kSecAttrDescription, desc);

  status = SecItemAdd(query, NULL);
  CFRelease(query);

  if (status == errSecDuplicateItem) {
    /* Item exists — delete and re-add */
    CFMutableDictionaryRef del_query = CFDictionaryCreateMutable(
      NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(del_query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(del_query, kSecAttrService, svc);
    CFDictionarySetValue(del_query, kSecAttrAccount, acct);
    SecItemDelete(del_query);
    CFRelease(del_query);

    /* Re-add with biometric ACL */
    CFMutableDictionaryRef re_query = CFDictionaryCreateMutable(
      NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(re_query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(re_query, kSecAttrService, svc);
    CFDictionarySetValue(re_query, kSecAttrAccount, acct);
    CFDictionarySetValue(re_query, kSecValueData, val);
    CFDictionarySetValue(re_query, kSecAttrAccessControl, access);
    CFDictionarySetValue(re_query, kSecAttrDescription, desc);

    status = SecItemAdd(re_query, NULL);
    CFRelease(re_query);

    if (status != errSecSuccess) {
      OSStatus restore_status;

      /* Biometric re-add failed — restore as normal item so secret isn't lost */
      fprintf(stderr, "%s: biometric save failed, restoring as normal item\n",
              envchain_name);
      CFMutableDictionaryRef restore = CFDictionaryCreateMutable(
        NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
      CFDictionarySetValue(restore, kSecClass, kSecClassGenericPassword);
      CFDictionarySetValue(restore, kSecAttrService, svc);
      CFDictionarySetValue(restore, kSecAttrAccount, acct);
      CFDictionarySetValue(restore, kSecValueData, val);
      CFDictionarySetValue(restore, kSecAttrDescription, desc);
      restore_status = SecItemAdd(restore, NULL);
      CFRelease(restore);

      if (restore_status != errSecSuccess) {
        fprintf(stderr,
          "%s: failed to restore the original non-biometric item: %d\n",
          envchain_name, (int)restore_status);
      }

      CFRelease(svc);
      CFRelease(acct);
      CFRelease(val);
      CFRelease(desc);
      CFRelease(access);
      CFRelease(svc);
      return 1;
    }
  }
  else if (status != errSecSuccess) {
    fprintf(stderr, "%s: failed to save biometric keychain item: %d\n",
            envchain_name, (int)status);
    CFRelease(svc);
    CFRelease(acct);
    CFRelease(val);
    CFRelease(desc);
    CFRelease(access);
    CFRelease(svc);
    return 1;
  }

  CFRelease(svc);
  CFRelease(acct);
  CFRelease(val);
  CFRelease(desc);
  CFRelease(access);

  fprintf(stderr, "Saved with Touch ID protection\n");
  return 0;
}
