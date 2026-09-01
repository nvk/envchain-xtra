#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "envchain_metadata.h"

static const char *const envchain_metadata_names[] = {
  "ENVCHAIN_METADATA_VERSION",
  "ENVCHAIN_NAMESPACES",
  "ENVCHAIN_KEYS",
  "ENVCHAIN_KEYS_ENCODING",
  "AGENT_PROFILE_CONTEXT_RENDERER",
  "BONDAGE_PROFILE_CONTEXT_VERSION",
  "BONDAGE_PROFILE",
  "BONDAGE_USE_ENVCHAIN",
  "BONDAGE_ENVCHAIN_NAMESPACES",
  "BONDAGE_USE_NONO",
  "BONDAGE_NONO_PROFILE",
  "BONDAGE_NONO_BIN",
  "BONDAGE_TOUCH_POLICY",
  "BONDAGE_NONO_ALLOW_CWD",
  "BONDAGE_NONO_ALLOW_DIR_COUNT",
  "BONDAGE_NONO_READ_DIR_COUNT",
  "BONDAGE_NONO_ALLOW_FILE_COUNT",
  "BONDAGE_NONO_READ_FILE_COUNT"
};

static void
envchain_metadata_fail(envchain_exec_metadata *metadata, const char *message)
{
  metadata->failed = 1;
  snprintf(metadata->error, sizeof(metadata->error), "%s", message);
}

static int
envchain_metadata_key_is_reserved(const char *key)
{
  size_t i;

  for (i = 0; i < sizeof(envchain_metadata_names) /
                       sizeof(envchain_metadata_names[0]); i++) {
    if (strcmp(key, envchain_metadata_names[i]) == 0) return 1;
  }
  return 0;
}

static int
envchain_metadata_has_key(const envchain_exec_metadata *metadata,
                          const char *key)
{
  size_t i;

  for (i = 0; i < metadata->count; i++) {
    if (strcmp(metadata->keys[i], key) == 0) return 1;
  }
  return 0;
}

static int
envchain_metadata_add_key(envchain_exec_metadata *metadata, const char *key)
{
  char **grown;
  char *copy;
  size_t next_capacity;

  if (envchain_metadata_has_key(metadata, key)) return 1;

  if (metadata->count == metadata->capacity) {
    next_capacity = metadata->capacity == 0 ? 8 : metadata->capacity * 2;
    grown = realloc(metadata->keys, sizeof(char *) * next_capacity);
    if (grown == NULL) {
      envchain_metadata_fail(metadata,
                             "out of memory while recording injected key names");
      return 0;
    }
    metadata->keys = grown;
    metadata->capacity = next_capacity;
  }

  copy = strdup(key);
  if (copy == NULL) {
    envchain_metadata_fail(metadata,
                           "out of memory while recording injected key names");
    return 0;
  }
  metadata->keys[metadata->count++] = copy;
  return 1;
}

static int
envchain_metadata_sort_keys(const void *left, const void *right)
{
  const char *const *a = left;
  const char *const *b = right;
  return strcmp(*a, *b);
}

static int
envchain_metadata_is_unreserved(unsigned char value)
{
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') ||
         value == '_';
}

static char *
envchain_metadata_encode_keys(envchain_exec_metadata *metadata)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t i;
  size_t j;
  size_t length = 1;
  char *encoded;
  char *cursor;

  if (metadata->count > 1) {
    qsort(metadata->keys, metadata->count, sizeof(char *),
          envchain_metadata_sort_keys);
  }

  for (i = 0; i < metadata->count; i++) {
    const unsigned char *key = (const unsigned char *)metadata->keys[i];
    if (i > 0) length++;
    for (j = 0; key[j] != '\0'; j++) {
      length += envchain_metadata_is_unreserved(key[j]) ? 1 : 3;
    }
  }

  encoded = malloc(length);
  if (encoded == NULL) {
    envchain_metadata_fail(metadata,
                           "out of memory while encoding injected key names");
    return NULL;
  }

  cursor = encoded;
  for (i = 0; i < metadata->count; i++) {
    const unsigned char *key = (const unsigned char *)metadata->keys[i];
    if (i > 0) *cursor++ = ',';
    for (j = 0; key[j] != '\0'; j++) {
      if (envchain_metadata_is_unreserved(key[j])) {
        *cursor++ = (char)key[j];
      }
      else {
        *cursor++ = '%';
        *cursor++ = hex[key[j] >> 4];
        *cursor++ = hex[key[j] & 0x0f];
      }
    }
  }
  *cursor = '\0';
  return encoded;
}

void
envchain_exec_metadata_init(envchain_exec_metadata *metadata)
{
  memset(metadata, 0, sizeof(*metadata));
}

void
envchain_exec_metadata_free(envchain_exec_metadata *metadata)
{
  size_t i;

  for (i = 0; i < metadata->count; i++) free(metadata->keys[i]);
  free(metadata->keys);
  memset(metadata, 0, sizeof(*metadata));
}

int
envchain_exec_metadata_inject(envchain_exec_metadata *metadata,
                              const char *key,
                              const char *value)
{
  if (metadata->failed) return 0;
  if (envchain_metadata_key_is_reserved(key)) {
    envchain_metadata_fail(
      metadata,
      "a namespace uses a reserved launch metadata variable name");
    return 0;
  }
  if (setenv(key, value, 1) < 0) {
    envchain_metadata_fail(metadata,
                           "cannot inject an invalid or unsupported key name");
    return 0;
  }
  return envchain_metadata_add_key(metadata, key);
}

int
envchain_exec_metadata_publish(envchain_exec_metadata *metadata,
                               const char *namespaces)
{
  char *keys;

  if (metadata->failed) return 0;
  keys = envchain_metadata_encode_keys(metadata);
  if (keys == NULL) return 0;

  if (setenv("ENVCHAIN_METADATA_VERSION", "1", 1) < 0 ||
      setenv("ENVCHAIN_NAMESPACES", namespaces, 1) < 0 ||
      setenv("ENVCHAIN_KEYS_ENCODING", "percent-v1", 1) < 0 ||
      setenv("ENVCHAIN_KEYS", keys, 1) < 0) {
    free(keys);
    envchain_metadata_fail(metadata,
                           "cannot publish envchain injection metadata");
    return 0;
  }

  free(keys);
  return 1;
}

const char *
envchain_exec_metadata_error(const envchain_exec_metadata *metadata)
{
  return metadata->error[0] == '\0' ? "unknown metadata error" : metadata->error;
}
