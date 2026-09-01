#ifndef ENVCHAIN_METADATA_H
#define ENVCHAIN_METADATA_H

#include <stddef.h>

#define ENVCHAIN_METADATA_ERROR_SIZE 192

typedef struct {
  char **keys;
  size_t count;
  size_t capacity;
  int failed;
  char error[ENVCHAIN_METADATA_ERROR_SIZE];
} envchain_exec_metadata;

void envchain_exec_metadata_init(envchain_exec_metadata *metadata);
void envchain_exec_metadata_free(envchain_exec_metadata *metadata);
int envchain_exec_metadata_inject(envchain_exec_metadata *metadata,
                                  const char *key,
                                  const char *value);
int envchain_exec_metadata_publish(envchain_exec_metadata *metadata,
                                   const char *namespaces);
const char *envchain_exec_metadata_error(const envchain_exec_metadata *metadata);

#endif
