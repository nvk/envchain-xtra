#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "envchain_metadata.h"

int
main(void)
{
  envchain_exec_metadata metadata;
  envchain_exec_metadata reserved;

  unsetenv("ENVCHAIN_METADATA_VERSION");
  setenv("ENVCHAIN_NAMESPACES", "spoofed", 1);
  setenv("ENVCHAIN_KEYS", "spoofed", 1);
  unsetenv("ENVCHAIN_KEYS_ENCODING");

  envchain_exec_metadata_init(&metadata);
  assert(envchain_exec_metadata_inject(&metadata, "Z_TOKEN", "secret-z"));
  assert(envchain_exec_metadata_inject(&metadata, "API_KEY", "secret-a"));
  assert(envchain_exec_metadata_inject(&metadata, "API_KEY", "secret-new"));
  assert(envchain_exec_metadata_inject(&metadata, "ODD,KEY", "secret-odd"));
  assert(envchain_exec_metadata_publish(&metadata, "alpha,beta"));

  assert(strcmp(getenv("ENVCHAIN_METADATA_VERSION"), "1") == 0);
  assert(strcmp(getenv("ENVCHAIN_NAMESPACES"), "alpha,beta") == 0);
  assert(strcmp(getenv("ENVCHAIN_KEYS_ENCODING"), "percent-v1") == 0);
  assert(strcmp(getenv("ENVCHAIN_KEYS"),
                "API_KEY,ODD%2CKEY,Z_TOKEN") == 0);
  assert(strcmp(getenv("API_KEY"), "secret-new") == 0);
  envchain_exec_metadata_free(&metadata);

  unsetenv("ENVCHAIN_KEYS");
  envchain_exec_metadata_init(&reserved);
  assert(!envchain_exec_metadata_inject(&reserved,
                                        "ENVCHAIN_KEYS",
                                        "must-not-be-injected"));
  assert(getenv("ENVCHAIN_KEYS") == NULL);
  assert(strstr(envchain_exec_metadata_error(&reserved), "reserved") != NULL);
  envchain_exec_metadata_free(&reserved);

  envchain_exec_metadata_init(&reserved);
  assert(!envchain_exec_metadata_inject(&reserved,
                                        "BONDAGE_PROFILE",
                                        "must-not-spoof-launch-metadata"));
  assert(strstr(envchain_exec_metadata_error(&reserved), "reserved") != NULL);
  envchain_exec_metadata_free(&reserved);

  return 0;
}
