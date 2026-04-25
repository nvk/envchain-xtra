#include "envchain.h"
#include <libsecret/secret.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const SecretSchema *envchain_get_schema(void) {
  static const SecretSchema the_schema = {
      .name = "envchain.EnvironmentVariable",
      .flags = SECRET_SCHEMA_NONE,
      .attributes =
          {
              {.name = "name", .type = SECRET_SCHEMA_ATTRIBUTE_STRING},
              {.name = "key", .type = SECRET_SCHEMA_ATTRIBUTE_STRING},
              {NULL, 0},
          },
  };
  return &the_schema;
}

static GList *search_unlocked_collection(const char *name, GError **error) {
  SecretService *service =
      secret_service_get_sync(SECRET_SERVICE_LOAD_COLLECTIONS, NULL, error);
  if (*error != NULL) {
    return NULL;
  }

  SecretCollection *collection = secret_collection_for_alias_sync(
      service, SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_LOAD_ITEMS, NULL,
      error);
  g_object_unref(service);
  if (*error != NULL) {
    return NULL;
  }
  if (collection == NULL) {
    // Default collection does not exist
    return NULL;
  }

  if (secret_collection_get_locked(collection)) {
    GList *objects = g_list_append(NULL, collection);
    GList *unlocked = NULL;
    const gint n =
        secret_service_unlock_sync(secret_collection_get_service(collection),
                                   objects, NULL, &unlocked, error);
    g_list_free(objects);
    g_list_free(unlocked);
    g_object_unref(collection);
    if (*error != NULL) {
      return NULL;
    }
    if (n == 0) {
      fprintf(stderr, "%s: failed to unlock collection\n", envchain_name);
    }

    /* reload */
    secret_service_disconnect();
    service =
        secret_service_get_sync(SECRET_SERVICE_LOAD_COLLECTIONS, NULL, error);
    if (*error != NULL) {
      return NULL;
    }
    collection = secret_collection_for_alias_sync(
        service, SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_LOAD_ITEMS, NULL,
        error);
    g_object_unref(service);
    if (*error != NULL) {
      return NULL;
    }
  }

  GHashTable *attributes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  if (name != NULL) {
    g_hash_table_insert(attributes, g_strdup("name"), g_strdup(name));
  }
  GList *items =
      secret_collection_search_sync(collection, envchain_get_schema(),
                                    attributes, SECRET_SEARCH_ALL, NULL, error);

  g_hash_table_unref(attributes);
  g_object_unref(collection);

  return items;
}

int envchain_search_namespaces(envchain_namespace_search_callback callback,
                               void *data) {
  GError *error = NULL;

  GList *items = search_unlocked_collection(NULL, &error);
  if (error != NULL) {
    fprintf(stderr, "%s: search_unlocked_collection failed with %d: %s\n",
            envchain_name, error->code, error->message);
    g_error_free(error);
    return 1;
  }

  GList *iter;
  GHashTable *names =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  for (iter = items; iter != NULL; iter = iter->next) {
    SecretItem *item = iter->data;
    GHashTable *attrs = secret_item_get_attributes(item);
    char *name = g_strdup(g_hash_table_lookup(attrs, "name"));
    if (g_hash_table_add(names, name)) {
      callback(name, data);
    }
    g_hash_table_unref(attrs);
  }

  g_hash_table_unref(names);
  g_list_free(items);
  return 0;
}

// Returns FALSE if the error is retryable
static gboolean try_search_items(const char *name,
                                 envchain_search_callback callback, void *data,
                                 int *result) {
  GError *error = NULL;
  GList *items = search_unlocked_collection(name, &error);
  if (error != NULL) {
    fprintf(stderr, "%s: search_unlocked_collection failed with %d: %s\n",
            envchain_name, error->code, error->message);
    g_error_free(error);
    *result = 1;
    return TRUE;
  }

  GList *iter;
  for (iter = items; iter != NULL; iter = iter->next) {
    SecretItem *item = iter->data;
    GHashTable *attrs = secret_item_get_attributes(item);
    char *key = g_hash_table_lookup(attrs, "key");
    if (!secret_item_load_secret_sync(item, NULL, &error)) {
      const int error_code = error->code;
      g_error_free(error);
      g_list_free(items);
      if (error_code == SECRET_ERROR_PROTOCOL) {
        return FALSE;
      } else {
        fprintf(stderr, "%s: secret_item_load_secret_sync failed with %d: %s\n",
                envchain_name, error->code, error->message);
        *result = 1;
        return TRUE;
      }
    }
    SecretValue *value = secret_item_get_secret(item);
    callback(key, secret_value_get_text(value), data);
    secret_value_unref(value);
    g_hash_table_unref(attrs);
  }

  g_list_free(items);
  *result = 0;
  return TRUE;
}

int envchain_search_values(const char *name, envchain_search_callback callback,
                           void *data) {
  /*
   * Retry when org.freedesktop.Secret.Item.GetSecret (secret_item_load_secret_sync)
   * fails. It occasionally fails with a message "** Message: received an
   * invalid or unencryptable secret".
   */
  for (int retry_count = 0; retry_count < 3; ++retry_count) {
    int result = -1;
    if (try_search_items(name, callback, data, &result)) {
      return result;
    }
    secret_service_disconnect();
  }
  fprintf(stderr, "%s: too many secret_item_load_secret_sync failures\n",
          envchain_name);
  return 1;
}

void envchain_save_value(const char *name, const char *key, char *value,
                         int require_passphrase) {
  if (require_passphrase == 1) {
    fprintf(
        stderr,
        "%s: Sorry, `--require-passphrase' is unsupported on this platform\n",
        envchain_name);
    return;
  }

  GError *error = NULL;
  secret_password_store_sync(envchain_get_schema(), SECRET_COLLECTION_DEFAULT,
                             key, value, NULL, &error, "name", name, "key", key,
                             NULL);
  if (error != NULL) {
    fprintf(stderr, "%s: secret_password_store_sync failed with %d: %s\n",
            envchain_name, error->code, error->message);
    g_error_free(error);
  }
}

void envchain_delete_value(const char *name, const char *key) {
  GError *error = NULL;
  secret_password_clear_sync(envchain_get_schema(), NULL, &error,
                             "name", name, "key", key, NULL);
  if (error != NULL) {
    fprintf(stderr, "%s: secret_password_clear_sync failed with %d: %s\n",
            envchain_name, error->code, error->message);
    g_error_free(error);
  }
}

int envchain_path_is_native_binary(const char *path) {
  FILE *fp = fopen(path, "rb");
  unsigned char magic[4];

  if (fp == NULL) return 0;
  if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic)) {
    fclose(fp);
    return 0;
  }
  fclose(fp);

  return magic[0] == 0x7f && magic[1] == 'E' &&
         magic[2] == 'L' && magic[3] == 'F';
}

char *envchain_binary_fingerprint(const char *path) {
  FILE *fp = fopen(path, "rb");
  GChecksum *checksum;
  char buf[8192];
  size_t nread;
  char *result = NULL;

  if (fp == NULL) return NULL;

  checksum = g_checksum_new(G_CHECKSUM_SHA256);
  if (checksum == NULL) {
    fclose(fp);
    return NULL;
  }

  while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
    g_checksum_update(checksum, (const guchar*)buf, nread);
  }
  if (ferror(fp)) {
    g_checksum_free(checksum);
    fclose(fp);
    return NULL;
  }

  asprintf(&result, "sha256-file:%s", g_checksum_get_string(checksum));
  g_checksum_free(checksum);
  fclose(fp);
  return result;
}

int envchain_save_value_biometric(const char *name, const char *key, char *value) {
  (void)name; (void)key; (void)value;
  fprintf(stderr, "%s: --biometric is unsupported on this platform\n", envchain_name);
  return 1;
}
