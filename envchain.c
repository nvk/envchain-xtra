/* envchain
 *
 * Copyright (c) 2024 Sorah Fukumori
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>

#include <readline/readline.h>

#include "envchain.h"


static const char version[] = "1.3.0";
const char *envchain_name;

/* for help */

static void
envchain_abort_with_help(void)
{
  fprintf(
    stderr,
    "%s version %s\n\n"
    "Usage:\n"
    "  Add variables\n"
    "    %s (--set|-s) [--[no-]require-passphrase|-p|-P] [--noecho|-n] NAMESPACE ENV [ENV ..]\n"
    "  Execute with variables\n"
    "    %s NAMESPACE CMD [ARG ...]\n"
    "  List namespaces\n"
    "    %s --list\n"
    "  Approve a binary for credential injection\n"
    "    %s --approve BINARY\n"
    "  List approved binaries\n"
    "    %s --approved\n"
    "  Revoke a binary approval\n"
    "    %s --revoke BINARY\n"
    "  Remove variables\n"
    "    %s --unset NAMESPACE ENV [ENV ..]\n"
    "\n"
    "Options:\n"
    "  --set (-s):\n"
    "    Add keychain item of environment variable +ENV+ for namespace +NAMESPACE+.\n"
    "\n"
    "  --noecho (-n):\n"
    "    Enable noecho mode when prompting values. Requires stdin to be a terminal.\n"
    "\n"
    "  --require-passphrase (-p), --no-require-passphrase (-P):\n"
    "    Configure whether the item prompts for access.\n"
    "    Leave as is when both options are omitted.\n"
    "\n"
    "  --approve:\n"
    "    Resolve BINARY and store a fingerprinted allowlist entry.\n"
    "\n"
    "  --approved:\n"
    "    List allowlist entries. Legacy path-only entries are marked.\n"
    "\n"
    "  --revoke:\n"
    "    Remove the allowlist entry for BINARY.\n"
    ,
    envchain_name, version,
    envchain_name, envchain_name, envchain_name, envchain_name,
    envchain_name, envchain_name, envchain_name
  );
  exit(2);
}

/* functions for --set */

char*
envchain_noecho_read(char* prompt)
{
  struct termios term, term_orig;
  char* str = NULL;
  ssize_t len;
  size_t n;

  if (tcgetattr(STDIN_FILENO, &term) < 0) {
    if (errno == ENOTTY) {
      fprintf(stderr, "--noecho (-n) requires stdin to be a terminal\n");
    }
    else {
      fprintf(stderr, "oops when attempted to read: %s\n", strerror(errno));
    }
    return NULL;
  }

  term_orig = term;
  term.c_lflag &= ~ECHO;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) < 0) {
    fprintf(stderr, "tcsetattr failed\n");
    exit(10);
  }

  printf("%s (noecho):", prompt);
  len = getline(&str, &n, stdin);

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_orig) < 0) {
    fprintf(stderr, "tcsetattr restore failed\n");
    exit(10);
  }

  if (0 < len && str[len-1] == '\n')
    str[len - 1] = '\0';

  printf("\n");

  return str;
}


static char*
envchain_ask_value(const char* name, const char* key, int noecho)
{
  char *prompt, *line;
  asprintf(&prompt, "%s.%s", name, key);

  if (noecho) {
    line = envchain_noecho_read(prompt);
  }
  else {
    printf("%s", prompt);
    line = readline(": ");
  }

  free(prompt);
  return line;
}

int
envchain_set(int argc, const char **argv)
{
  int noecho = 0;
  int require_passphrase = -1;
  const char *name, *key;
  char *value;

  while (2 < argc) {
    if (argv[0][0] != '-') break;

    if (strcmp(argv[0], "-n") == 0 || strcmp(argv[0], "--noecho") == 0) {
      argv++; argc--;
      noecho = 1;
    }
    else if (strcmp(argv[0], "-p") == 0 || strcmp(argv[0], "--require-passphrase") == 0) {
      argv++; argc--;
      require_passphrase = 1;
    }
    else if (strcmp(argv[0], "-P") == 0 || strcmp(argv[0], "--no-require-passphrase") == 0) {
      argv++; argc--;
      require_passphrase = 0;
    }
    else {
      fprintf(stderr, "Unknown option: %s\n", argv[0]);
      return 1;
    }
  }
  if (argc < 2) envchain_abort_with_help();

  name = argv[0];
  argv++; argc--;

  while(0 < argc) {
    key = argv[0];
    argv++; argc--;

    value = envchain_ask_value(name, key, noecho);
    if (value == NULL) return 1;

    envchain_save_value(name, key, value, require_passphrase);
  }

  return 0;
}

/* functions for list */

static void
envchain_list_value_callback(const char *key, const char* value, void *raw_context)
{
  envchain_list_context* context = (envchain_list_context*)raw_context;

  if (context->show_value) {
    printf("%s=%s\n", key, value);
  }
  else {
    printf("%s\n", key);
  }
}

static void
envchain_list_namespace_callback(const char *name, void *raw_context)
{
  (void)raw_context; /* silence warning */

  printf("%s\n", name);
}

int
envchain_list(int argc, const char **argv)
{
  envchain_list_context context = {NULL,0};

  while (0 < argc) {
    if (strcmp(argv[0], "--show-value") == 0 || strcmp(argv[0], "-v") == 0) {
      argv++; argc--;
      context.show_value = 1;
    }
    else {
      if (context.target) envchain_abort_with_help();
      context.target = argv[0];
      argv++; argc--;
    }
  }

  if (context.target) {
    envchain_search_values(
      context.target, &envchain_list_value_callback, &context);
  }
  else {
    if (context.show_value) envchain_abort_with_help();

    envchain_search_namespaces(&envchain_list_namespace_callback, &context);
  }
  return 0;
}

/* functions for --unset */

int
envchain_unset(int argc, const char **argv)
{
  const char *name, *key;

  if (argc < 2) envchain_abort_with_help();

  name = argv[0];
  argv++; argc--;

  while (0 < argc) {
    key = argv[0];
    argv++; argc--;

    envchain_delete_value(name, key);
  }

  return 0;
}

/* functions for exec mode */

static void
envchain_exec_value_callback(const char* key, const char* value, void *context)
{
  (void)context; /* silence warning */

  setenv(key, value, 1);
}

/* allowlist: ~/.envchain/allowed
 *
 * Entries live under the real account home directory and are stored as either:
 *
 *   /absolute/path
 *   /absolute/path<TAB>algorithm-tagged-fingerprint
 *
 * Path-only entries are legacy and still work, but warn at runtime so they can
 * be upgraded with --approve. Fingerprinted entries validate the resolved path
 * and the current fingerprint before secrets are passed to the target binary.
 *
 * If the file does not exist (ENOENT), all binaries are allowed (original
 * behavior). Other read errors (EACCES, broken symlink, etc.) deny with a
 * diagnostic.
 */

static void
trim_line_endings(char *line)
{
  size_t len = strlen(line);
  while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
    line[--len] = '\0';
}

static int
split_allowlist_line(char *line, char **entry_path, char **stored_fp)
{
  char *tab;

  trim_line_endings(line);
  if (line[0] == '\0' || line[0] == '#') return 0;

  tab = strchr(line, '\t');
  if (tab != NULL) {
    *tab = '\0';
    *entry_path = line;
    *stored_fp = tab + 1;
  }
  else {
    *entry_path = line;
    *stored_fp = NULL;
  }

  return (*entry_path)[0] != '\0';
}

static int
path_contains_allowlist_delimiter(const char *path)
{
  return strchr(path, '\t') != NULL ||
         strchr(path, '\n') != NULL ||
         strchr(path, '\r') != NULL;
}

static char *
resolve_exe_path(const char *exe)
{
  /* If exe contains a slash, resolve directly */
  if (strchr(exe, '/') != NULL) {
    return realpath(exe, NULL);
  }

  /* Search PATH, matching execvp() semantics */
  char *path_env = getenv("PATH");
  if (!path_env) return NULL;

  char *path_copy = strdup(path_env);
  char *dir, *search = path_copy;
  char buf[4096];

  while ((dir = strsep(&search, ":")) != NULL) {
    /* Empty PATH segment means current directory (execvp semantics) */
    if (*dir == '\0') dir = ".";
    snprintf(buf, sizeof(buf), "%s/%s", dir, exe);
    if (access(buf, X_OK) == 0) {
      free(path_copy);
      return realpath(buf, NULL);
    }
  }
  free(path_copy);
  return NULL;
}

static int
build_allowlist_path(char *allowlist_path, size_t allowlist_path_len)
{
  struct passwd *pw = getpwuid(getuid());
  if (!pw || !pw->pw_dir || pw->pw_dir[0] == '\0') {
    fprintf(stderr, "envchain: cannot determine home directory for allowlist\n");
    return 0;
  }

  if ((size_t)snprintf(allowlist_path, allowlist_path_len,
                       "%s/.envchain/allowed", pw->pw_dir) >= allowlist_path_len) {
    fprintf(stderr, "envchain: allowlist path too long\n");
    return 0;
  }

  return 1;
}

static int
ensure_allowlist_parent_dir(const char *allowlist_path)
{
  char dirpath[4096];
  char *slash;

  if ((size_t)snprintf(dirpath, sizeof(dirpath), "%s", allowlist_path) >= sizeof(dirpath)) {
    fprintf(stderr, "envchain: allowlist directory path too long\n");
    return 0;
  }

  slash = strrchr(dirpath, '/');
  if (slash == NULL) return 1;
  *slash = '\0';

  if (mkdir(dirpath, 0700) == 0 || errno == EEXIST) return 1;

  fprintf(stderr, "envchain: cannot create allowlist directory %s: %s\n",
          dirpath, strerror(errno));
  return 0;
}

static int
allowlist_line_matches_path(const char *line, const char *resolved_path)
{
  char *copy = strdup(line);
  char *entry_path, *stored_fp;
  char *allowed_resolved;
  int matched = 0;

  if (copy == NULL) return 0;
  if (!split_allowlist_line(copy, &entry_path, &stored_fp)) {
    free(copy);
    return 0;
  }
  (void)stored_fp; /* silence warning */

  allowed_resolved = realpath(entry_path, NULL);
  if (allowed_resolved != NULL) {
    matched = strcmp(resolved_path, allowed_resolved) == 0;
    free(allowed_resolved);
  }

  free(copy);
  return matched;
}

static int
rewrite_allowlist(const char *allowlist_path, const char *resolved_path,
                  const char *replacement_line, int *removed_any)
{
  FILE *in = NULL;
  FILE *out = NULL;
  char line[4096];
  char temp_path[4096];
  int fd = -1;
  int removed = 0;

  if (!ensure_allowlist_parent_dir(allowlist_path)) return 0;

  in = fopen(allowlist_path, "r");
  if (in == NULL && errno != ENOENT) {
    fprintf(stderr, "envchain: cannot read allowlist %s: %s\n",
            allowlist_path, strerror(errno));
    return 0;
  }

  if ((size_t)snprintf(temp_path, sizeof(temp_path), "%s.tmp.XXXXXX", allowlist_path) >= sizeof(temp_path)) {
    fprintf(stderr, "envchain: temporary allowlist path too long\n");
    if (in != NULL) fclose(in);
    return 0;
  }

  fd = mkstemp(temp_path);
  if (fd < 0) {
    fprintf(stderr, "envchain: cannot create temporary allowlist %s: %s\n",
            temp_path, strerror(errno));
    if (in != NULL) fclose(in);
    return 0;
  }
  if (fchmod(fd, S_IRUSR | S_IWUSR) < 0) {
    fprintf(stderr, "envchain: cannot set permissions on %s: %s\n",
            temp_path, strerror(errno));
    close(fd);
    unlink(temp_path);
    if (in != NULL) fclose(in);
    return 0;
  }

  out = fdopen(fd, "w");
  if (out == NULL) {
    fprintf(stderr, "envchain: cannot open temporary allowlist %s: %s\n",
            temp_path, strerror(errno));
    close(fd);
    unlink(temp_path);
    if (in != NULL) fclose(in);
    return 0;
  }
  fd = -1;

  if (in != NULL) {
    while (fgets(line, sizeof(line), in)) {
      if (allowlist_line_matches_path(line, resolved_path)) {
        removed = 1;
        continue;
      }
      if (fputs(line, out) == EOF) goto io_fail;
    }
  }

  if (replacement_line != NULL) {
    if (fprintf(out, "%s\n", replacement_line) < 0) goto io_fail;
  }

  if (fflush(out) != 0) goto io_fail;
  if (ferror(out)) goto io_fail;

  if (in != NULL) fclose(in);
  in = NULL;
  if (fclose(out) != 0) {
    out = NULL;
    goto io_fail_unlinked;
  }
  out = NULL;

  if (rename(temp_path, allowlist_path) < 0) {
    fprintf(stderr, "envchain: cannot replace allowlist %s: %s\n",
            allowlist_path, strerror(errno));
    unlink(temp_path);
    return 0;
  }

  if (removed_any != NULL) *removed_any = removed;
  return 1;

io_fail:
  fprintf(stderr, "envchain: failed writing allowlist %s: %s\n",
          temp_path, strerror(errno));
  if (in != NULL) fclose(in);
  if (out != NULL) fclose(out);
  else if (fd >= 0) close(fd);
  unlink(temp_path);
  return 0;

io_fail_unlinked:
  fprintf(stderr, "envchain: failed writing allowlist %s: %s\n",
          temp_path, strerror(errno));
  unlink(temp_path);
  return 0;
}

static char *
dup_token(const char *start, size_t len)
{
  char *token = malloc(len + 1);
  if (token == NULL) return NULL;
  memcpy(token, start, len);
  token[len] = '\0';
  return token;
}

static char *
dup_basename(const char *path)
{
  const char *base = strrchr(path, '/');
  if (base != NULL) base++;
  else base = path;
  return strdup(base);
}

static char *
interpreter_hint_from_shebang(const char *line)
{
  const char *cursor = line;
  const char *start;
  size_t len;
  char *interpreter;
  char *base;

  while (*cursor == ' ' || *cursor == '\t') cursor++;
  start = cursor;
  while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
         *cursor != '\n' && *cursor != '\r')
    cursor++;
  len = (size_t)(cursor - start);
  if (len == 0) return NULL;

  interpreter = dup_token(start, len);
  if (interpreter == NULL) return NULL;

  base = dup_basename(interpreter);
  if (base == NULL) {
    free(interpreter);
    return NULL;
  }

  if (strcmp(base, "env") == 0) {
    free(base);
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    while (*cursor == '-') {
      while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
             *cursor != '\n' && *cursor != '\r')
        cursor++;
      while (*cursor == ' ' || *cursor == '\t') cursor++;
    }
    start = cursor;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
           *cursor != '\n' && *cursor != '\r')
      cursor++;
    len = (size_t)(cursor - start);
    free(interpreter);
    if (len == 0) return NULL;
    return dup_token(start, len);
  }

  free(interpreter);
  return base;
}

static int
path_is_script(const char *path, char **interpreter_hint)
{
  FILE *fp = fopen(path, "rb");
  char line[4096];

  if (interpreter_hint != NULL) *interpreter_hint = NULL;
  if (fp == NULL) return 0;
  if (fgets(line, sizeof(line), fp) == NULL) {
    fclose(fp);
    return 0;
  }
  fclose(fp);

  if (strncmp(line, "#!", 2) != 0) return 0;
  if (interpreter_hint != NULL) {
    *interpreter_hint = interpreter_hint_from_shebang(line + 2);
  }
  return 1;
}

static int
check_allowlist(const char *allowlist_path, const char *resolved_path,
                const char *original_exe)
{
  FILE *fp;
  char line[4096];
  int matched = 0;

  fp = fopen(allowlist_path, "r");
  if (!fp) {
    if (errno == ENOENT) {
      return 1; /* no allowlist file = allow all (backward compatible) */
    }
    /* Other errors (EACCES, broken symlink, etc.) = deny with diagnostic */
    fprintf(stderr, "envchain: cannot read allowlist %s: %s\n",
            allowlist_path, strerror(errno));
    return 0;
  }

  while (fgets(line, sizeof(line), fp)) {
    char *copy = strdup(line);
    char *entry_path, *stored_fp;
    char *allowed_resolved;
    int allowed = 0;

    if (copy == NULL) continue;
    if (!split_allowlist_line(copy, &entry_path, &stored_fp)) {
      free(copy);
      continue;
    }

    allowed_resolved = realpath(entry_path, NULL);
    if (allowed_resolved == NULL) {
      free(copy);
      continue;
    }

    if (strcmp(resolved_path, allowed_resolved) != 0) {
      free(allowed_resolved);
      free(copy);
      continue;
    }

    matched = 1;

    if (stored_fp == NULL) {
      fprintf(stderr, "envchain: warning: path-only allowlist entry for '%s'\n",
              resolved_path);
      fprintf(stderr, "  upgrade to fingerprint verification with:\n");
      fprintf(stderr, "    %s --approve %s\n", envchain_name, original_exe);
      allowed = 1;
    }
    else {
      char *actual_fp = envchain_binary_fingerprint(resolved_path);

      if (actual_fp == NULL) {
        fprintf(stderr, "envchain: cannot fingerprint '%s' for allowlist verification\n",
                resolved_path);
      }
      else if (strcmp(stored_fp, actual_fp) == 0) {
        allowed = 1;
      }
      else {
        fprintf(stderr, "envchain: FINGERPRINT MISMATCH for '%s'\n", original_exe);
        fprintf(stderr, "  path:    %s\n", resolved_path);
        fprintf(stderr, "  stored:  %s\n", stored_fp);
        fprintf(stderr, "  actual:  %s\n", actual_fp);
        fprintf(stderr, "\n");
        fprintf(stderr, "  The binary has changed since it was approved.\n");
        fprintf(stderr, "  If this is expected, re-approve with:\n");
        fprintf(stderr, "    %s --approve %s\n", envchain_name, original_exe);
      }

      free(actual_fp);
    }

    free(allowed_resolved);
    free(copy);
    fclose(fp);
    return allowed;
  }

  fclose(fp);

  if (!matched) {
    fprintf(stderr, "envchain: binary '%s' (%s) not in allowlist %s\n",
            original_exe, resolved_path, allowlist_path);
  }

  return 0;
}

static int
envchain_approve(int argc, const char **argv)
{
  char allowlist_path[4096];
  char *resolved;
  char *interpreter_hint = NULL;
  char *fingerprint;
  char *entry_line;

  if (argc != 1) envchain_abort_with_help();
  if (!build_allowlist_path(allowlist_path, sizeof(allowlist_path))) return 1;

  resolved = resolve_exe_path(argv[0]);
  if (resolved == NULL) {
    fprintf(stderr, "envchain: cannot resolve path for '%s'\n", argv[0]);
    return 1;
  }
  if (path_contains_allowlist_delimiter(resolved)) {
    fprintf(stderr, "envchain: cannot approve paths containing tabs or newlines: %s\n",
            resolved);
    free(resolved);
    return 1;
  }

  if (path_is_script(resolved, &interpreter_hint)) {
    fprintf(stderr, "envchain: '%s' is a script, not a native binary.\n", resolved);
    if (interpreter_hint != NULL) {
      fprintf(stderr, "Approve the interpreter instead: %s --approve %s\n",
              envchain_name, interpreter_hint);
    }
    free(interpreter_hint);
    free(resolved);
    return 1;
  }

  if (!envchain_path_is_native_binary(resolved)) {
    fprintf(stderr, "envchain: '%s' is not a supported native binary.\n", resolved);
    free(resolved);
    return 1;
  }

  fingerprint = envchain_binary_fingerprint(resolved);
  if (fingerprint == NULL) {
    fprintf(stderr, "envchain: cannot fingerprint '%s'\n", resolved);
    free(resolved);
    return 1;
  }

  if (asprintf(&entry_line, "%s\t%s", resolved, fingerprint) < 0) {
    fprintf(stderr, "envchain: failed to format allowlist entry\n");
    free(fingerprint);
    free(resolved);
    return 1;
  }

  if (!rewrite_allowlist(allowlist_path, resolved, entry_line, NULL)) {
    free(entry_line);
    free(fingerprint);
    free(resolved);
    return 1;
  }

  fprintf(stderr, "Approved: %s\n", resolved);
  fprintf(stderr, "Fingerprint: %s\n", fingerprint);

  free(entry_line);
  free(fingerprint);
  free(resolved);
  return 0;
}

static int
envchain_approved(int argc, const char **argv)
{
  char allowlist_path[4096];
  FILE *fp;
  char line[4096];

  (void)argv; /* silence warning */
  if (argc != 0) envchain_abort_with_help();
  if (!build_allowlist_path(allowlist_path, sizeof(allowlist_path))) return 1;

  fp = fopen(allowlist_path, "r");
  if (fp == NULL) {
    if (errno == ENOENT) return 0;
    fprintf(stderr, "envchain: cannot read allowlist %s: %s\n",
            allowlist_path, strerror(errno));
    return 1;
  }

  while (fgets(line, sizeof(line), fp)) {
    char *copy = strdup(line);
    char *entry_path, *stored_fp;

    if (copy == NULL) continue;
    if (!split_allowlist_line(copy, &entry_path, &stored_fp)) {
      free(copy);
      continue;
    }

    if (stored_fp == NULL) {
      printf("%s\tlegacy-path-only\n", entry_path);
    }
    else {
      printf("%s\t%s\n", entry_path, stored_fp);
    }

    free(copy);
  }

  fclose(fp);
  return 0;
}

static int
envchain_revoke(int argc, const char **argv)
{
  char allowlist_path[4096];
  char *resolved;
  int removed = 0;

  if (argc != 1) envchain_abort_with_help();
  if (!build_allowlist_path(allowlist_path, sizeof(allowlist_path))) return 1;

  resolved = resolve_exe_path(argv[0]);
  if (resolved == NULL) {
    fprintf(stderr, "envchain: cannot resolve path for '%s'\n", argv[0]);
    return 1;
  }

  if (!rewrite_allowlist(allowlist_path, resolved, NULL, &removed)) {
    free(resolved);
    return 1;
  }

  if (!removed) {
    fprintf(stderr, "envchain: '%s' is not currently approved\n", resolved);
    free(resolved);
    return 1;
  }

  fprintf(stderr, "Revoked: %s\n", resolved);
  free(resolved);
  return 0;
}

int
envchain_exec(int argc, const char **argv)
{
  if (argc < 2) envchain_abort_with_help();

  char *name, *names, *exe;
  char **args;
  char allowlist_path[4096];

  names = (char*)argv[0];
  exe = (char*)argv[1];
  argv++; argc--;
  argv++; argc--;

  if (!build_allowlist_path(allowlist_path, sizeof(allowlist_path))) {
    return 1;
  }

  /* 1. Load secrets first — they may mutate PATH */
  while ((name = strsep(&names, ",")) != NULL) {
    envchain_search_values(name, &envchain_exec_value_callback, NULL);
  }

  /* 2. Resolve exe against the final PATH (post-secret-injection) */
  char *resolved = resolve_exe_path(exe);
  if (!resolved) {
    fprintf(stderr, "envchain: cannot resolve path for '%s'\n", exe);
    return 1;
  }

  /* 3. Validate against allowlist */
  if (!check_allowlist(allowlist_path, resolved, exe)) {
    free(resolved);
    return 1;
  }

  /* 4. execv() the resolved absolute path — no second PATH lookup */
  int len = (2+argc);
  args = malloc(sizeof(char*) * len);
  args[0] = (char*)exe;
  args[len-1] = NULL;
  if (0 < argc) memcpy(args+1, argv, sizeof(char*) * argc);

  if (execv(resolved, args) < 0) {
    fprintf(stderr, "execv failed: %s\n", strerror(errno));
    free(resolved);
    return 1;
  }
  free(resolved);
  return 0;
}

/* entry point */

int
main(int argc, const char **argv)
{
  envchain_name = argv[0];
  if (argc < 2) envchain_abort_with_help();
  argv++; argc--;

  if (strcmp(argv[0], "--set") == 0 || strcmp(argv[0], "-s") == 0) {
    argv++; argc--;
    return envchain_set(argc, argv);
  }
  else if (strcmp(argv[0], "--list") == 0 || strcmp(argv[0], "-l") == 0) {
    argv++; argc--;
    return envchain_list(argc, argv);
  }
  else if (strcmp(argv[0], "--unset") == 0) {
    argv++; argc--;
    return envchain_unset(argc, argv);
  }
  else if (strcmp(argv[0], "--approve") == 0) {
    argv++; argc--;
    return envchain_approve(argc, argv);
  }
  else if (strcmp(argv[0], "--approved") == 0) {
    argv++; argc--;
    return envchain_approved(argc, argv);
  }
  else if (strcmp(argv[0], "--revoke") == 0) {
    argv++; argc--;
    return envchain_revoke(argc, argv);
  }
  else if (argv[0][0] == '-') {
    fprintf(stderr, "Unknown option %s\n", argv[0]);
    return 2;
  }
  else {
    return envchain_exec(argc, argv);
  }
}
