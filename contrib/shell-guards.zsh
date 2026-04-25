#!/usr/bin/env zsh
# shell-guards.zsh - fingerprint and optional human-approval helpers for envchain
#
# Source this file from your shell to add:
# - optional Touch ID / password confirmation via an external touchid-check helper
# - binary fingerprint verification before credential injection
# - helper commands for fingerprint approval and status
#
# Expected usage:
#
#   source /path/to/contrib/shell-guards.zsh
#
#   my_tool() {
#     _require_touchid || return 1
#     _verify_binary my_tool || return 1
#     envchain my-namespace my_tool "$@"
#   }
#
# Management commands:
#   envchain-approve <binary>   - store a fingerprint for a resolved binary path
#   envchain-reapprove          - re-approve known binaries after upgrades
#   envchain-status             - show allowlist, fingerprints, and profile status

_FINGERPRINT_FILE="${ENVCHAIN_FINGERPRINTS:-$HOME/.envchain/fingerprints}"

_tool_path() {
  whence -p -- "$1" 2>/dev/null
}

_REALPATH_BIN="$(_tool_path realpath)"
_SHASUM_BIN="$(_tool_path shasum)"
_MKTEMP_BIN="$(_tool_path mktemp)"
_MV_BIN="$(_tool_path mv)"
_CAT_BIN="$(_tool_path cat)"
_MKDIR_BIN="$(_tool_path mkdir)"
_RM_BIN="$(_tool_path rm)"

_require_tool() {
  local tool_path="$1"
  local tool_name="$2"

  if [[ -z "$tool_path" ]]; then
    echo "envchain: required command not found: $tool_name" >&2
    return 1
  fi
  return 0
}

_hash_file() {
  local file_path="$1"
  local out

  _require_tool "$_SHASUM_BIN" shasum || return 1
  out="$("$_SHASUM_BIN" -a 256 -- "$file_path" 2>/dev/null)" || return 1
  out="${out%% *}"
  [[ -n "$out" ]] || return 1
  print -r -- "sha256-file:$out"
}

_resolve_binary() {
  local exe="$1"
  local cmd_path
  local resolved

  _require_tool "$_REALPATH_BIN" realpath || return 1
  cmd_path="$(whence -p -- "$exe")"
  [[ -n "$cmd_path" ]] || return 1
  resolved="$("$_REALPATH_BIN" "$cmd_path" 2>/dev/null)" || return 1
  [[ -n "$resolved" ]] || return 1
  print -r -- "$resolved"
}

_lookup_fingerprint() {
  local target_path="$1"
  local line
  local entry_path
  local entry_fp

  [[ -f "$_FINGERPRINT_FILE" ]] || return 1

  while IFS= read -r line; do
    [[ -z "$line" || "$line" == \#* ]] && continue
    IFS=$'\t' read -r entry_path entry_fp <<< "$line"
    if [[ "$entry_path" == "$target_path" ]]; then
      print -r -- "$entry_fp"
      return 0
    fi
  done < "$_FINGERPRINT_FILE"

  return 1
}

_rewrite_fingerprints_without() {
  local target_path="$1"
  local tmp_path="$2"
  local line
  local entry_path
  local entry_fp

  while IFS= read -r line; do
    if [[ -z "$line" || "$line" == \#* ]]; then
      printf '%s\n' "$line" >> "$tmp_path" || return 1
      continue
    fi

    IFS=$'\t' read -r entry_path entry_fp <<< "$line"
    [[ "$entry_path" == "$target_path" ]] && continue
    printf '%s\t%s\n' "$entry_path" "$entry_fp" >> "$tmp_path" || return 1
  done < "$_FINGERPRINT_FILE"
}

# Optional human-approval gate.
# Set ENVCHAIN_TOUCHID=1 to require an external touchid-check helper on PATH.
_require_touchid() {
  local helper

  [[ "${ENVCHAIN_TOUCHID:-0}" == "1" ]] || return 0
  helper="$(_tool_path touchid-check)"
  if [[ -z "$helper" ]]; then
    echo "envchain: touchid-check not found on PATH" >&2
    return 1
  fi
  "$helper" || {
    echo "envchain: Authentication denied" >&2
    return 1
  }
  return 0
}

_verify_binary() {
  local exe="$1"
  local resolved
  local stored
  local actual

  resolved="$(_resolve_binary "$exe")" || {
    echo "envchain: cannot resolve '$exe'" >&2
    return 1
  }

  [[ ! -f "$_FINGERPRINT_FILE" ]] && return 0

  stored="$(_lookup_fingerprint "$resolved")"
  if [[ -z "$stored" ]]; then
    echo "envchain: '$exe' ($resolved) has no stored fingerprint" >&2
    echo "  approve with: envchain-approve $exe" >&2
    return 1
  fi

  actual="$(_hash_file "$resolved")" || {
    echo "envchain: failed to hash '$resolved'" >&2
    return 1
  }

  if [[ "$stored" != "$actual" ]]; then
    echo "envchain: FINGERPRINT MISMATCH for '$exe'" >&2
    echo "  path:   $resolved" >&2
    echo "  stored: $stored" >&2
    echo "  actual: $actual" >&2
    echo "  re-approve with: envchain-approve $exe" >&2
    return 1
  fi
  return 0
}

envchain-approve() {
  local exe="$1"
  local resolved
  local fp
  local tmp

  if [[ -z "$exe" ]]; then
    echo "usage: envchain-approve <binary>" >&2
    return 1
  fi

  resolved="$(_resolve_binary "$exe")" || {
    echo "envchain-approve: cannot resolve '$exe'" >&2
    return 1
  }
  fp="$(_hash_file "$resolved")" || {
    echo "envchain-approve: failed to hash '$resolved'" >&2
    return 1
  }

  _require_tool "$_MKDIR_BIN" mkdir || return 1
  "$_MKDIR_BIN" -p "$(dirname "$_FINGERPRINT_FILE")" || return 1

  if [[ -f "$_FINGERPRINT_FILE" ]]; then
    _require_tool "$_MKTEMP_BIN" mktemp || return 1
    _require_tool "$_MV_BIN" mv || return 1
    _require_tool "$_RM_BIN" rm || return 1
    tmp="$("$_MKTEMP_BIN" "${_FINGERPRINT_FILE}.tmp.XXXXXX")" || {
      echo "envchain-approve: failed to create temp file" >&2
      return 1
    }
    if ! _rewrite_fingerprints_without "$resolved" "$tmp"; then
      "$_RM_BIN" -f "$tmp"
      echo "envchain-approve: failed to rewrite $_FINGERPRINT_FILE" >&2
      return 1
    fi
    if ! "$_MV_BIN" "$tmp" "$_FINGERPRINT_FILE"; then
      "$_RM_BIN" -f "$tmp"
      echo "envchain-approve: failed to update $_FINGERPRINT_FILE" >&2
      return 1
    fi
  fi

  if ! printf '%s\t%s\n' "$resolved" "$fp" >> "$_FINGERPRINT_FILE"; then
    echo "envchain-approve: failed to append to $_FINGERPRINT_FILE" >&2
    return 1
  fi

  echo "Approved: $resolved"
  echo "Fingerprint: $fp"
}

envchain-reapprove() {
  local tools=(${ENVCHAIN_TOOLS:-nono})
  local tool

  echo "Re-approving binaries..."
  for tool in "${tools[@]}"; do
    if whence -p -- "$tool" &>/dev/null; then
      envchain-approve "$tool"
    else
      echo "  skip: $tool (not installed)"
    fi
  done
}

envchain-status() {
  local entry_path
  local fp
  local actual
  local file_path

  echo "=== envchain allowlist ==="
  _require_tool "$_CAT_BIN" cat || return 1
  "$_CAT_BIN" "$HOME/.envchain/allowed" 2>/dev/null || echo "  (no allowlist)"
  echo ""
  echo "=== Binary fingerprints ==="
  if [[ -f "$_FINGERPRINT_FILE" ]]; then
    while IFS= read -r file_path; do
      [[ -z "$file_path" || "$file_path" == \#* ]] && continue
      IFS=$'\t' read -r entry_path fp <<< "$file_path"
      actual="$(_hash_file "$entry_path")" || actual=""
      if [[ "$fp" == "$actual" ]]; then
        echo "  ✓ ${entry_path##*/} ($fp)"
      else
        echo "  ✗ ${entry_path##*/} — MISMATCH (run: envchain-reapprove)"
      fi
    done < "$_FINGERPRINT_FILE"
  else
    echo "  (no fingerprints — run: envchain-reapprove)"
  fi
}
