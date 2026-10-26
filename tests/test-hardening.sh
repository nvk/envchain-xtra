#!/usr/bin/env bash
# Regression tests for the security-hardening changes:
#   LOW-4  namespace/key character validation
#   MEDIUM-2 --list redaction and --unsafe-plain gating
#   (pre-existing) --approve refuses non-native scripts
set -euo pipefail

cd "$(dirname "$0")/.."
make >/dev/null

EC="$PWD/envchain"
fail=0

expect_fail() {
  local desc="$1"; shift
  if "$@" >/dev/null 2>&1; then
    echo "FAIL: $desc (expected non-zero exit)" >&2
    fail=1
  else
    echo "ok: $desc"
  fi
}

expect_ok() {
  local desc="$1"; shift
  if "$@" >/dev/null 2>&1; then
    echo "ok: $desc"
  else
    echo "FAIL: $desc (expected zero exit)" >&2
    fail=1
  fi
}

expect_fail_contains() {
  local desc="$1" expected="$2" output
  shift 2
  if output=$("$@" 2>&1); then
    echo "FAIL: $desc (expected non-zero exit)" >&2
    fail=1
  elif [[ "$output" == *"$expected"* ]]; then
    echo "ok: $desc"
  else
    echo "FAIL: $desc (missing expected error: $expected)" >&2
    printf '%s\n' "$output" >&2
    fail=1
  fi
}

# LOW-4: namespace and key validation
expect_fail "namespace with space is rejected" "$EC" --set "bad ns" SOME_KEY </dev/null
expect_fail "namespace with '=' is rejected" "$EC" --set "bad=ns" SOME_KEY </dev/null
expect_fail_contains "key with space is rejected as a key" \
  "invalid key 'BAD KEY'" "$EC" --set goodns "BAD KEY" </dev/null
expect_fail_contains "key with '=' is rejected as a key" \
  "invalid key 'BAD=KEY'" "$EC" --set goodns "BAD=KEY" </dev/null
expect_fail_contains "unset validates its key rather than a null namespace" \
  "invalid key 'BAD KEY'" "$EC" --unset goodns "BAD KEY"
expect_fail "exec namespace with space is rejected" "$EC" "bad ns" /bin/true

# MEDIUM-2: --unsafe-plain requires --show-value
expect_fail "--unsafe-plain without --show-value is rejected" \
  "$EC" --list --unsafe-plain

# Pre-existing guarantee: --approve refuses non-native scripts
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cat > "$tmpdir/script.sh" <<'EOS'
#!/bin/sh
echo hi
EOS
chmod +x "$tmpdir/script.sh"
expect_fail "script cannot be approved" "$EC" --approve "$tmpdir/script.sh"

if [ "$fail" -ne 0 ]; then
  echo "test-hardening: FAILED" >&2
  exit 1
fi
echo "test-hardening: ok"
