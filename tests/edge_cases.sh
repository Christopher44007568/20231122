#!/usr/bin/env bash
set -u

BIN="$1"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

: > "$TMP/empty.json"
"$BIN" analyze --input "$TMP/empty.json" --out "$TMP/empty-artifact" \
  > "$TMP/empty.log" 2>&1 || fail "empty input should not be a runtime error"
[ -f "$TMP/empty-artifact/analysis.json" ] || fail "empty analysis missing"
grep -q '"code": "empty"' "$TMP/empty-artifact/analysis.json" ||
  fail "empty status missing"
"$BIN" verify --artifact "$TMP/empty-artifact" > "$TMP/empty-verify.json" ||
  fail "empty artifact verification failed"
"$BIN" reload --artifact "$TMP/empty-artifact" --out "$TMP/reload" \
  > "$TMP/reload.json" || fail "empty artifact reload failed"
grep -q '"status": "pass"' "$TMP/reload.json" ||
  fail "reload did not preserve order"

cp -a "$TMP/empty-artifact" "$TMP/tampered-artifact"
sed -i 's/"empty"/"tampered"/' "$TMP/tampered-artifact/analysis.json"
set +e
"$BIN" verify --artifact "$TMP/tampered-artifact" \
  > "$TMP/tampered-verify.json" 2>&1
TAMPER_CODE=$?
set -e
[ "$TAMPER_CODE" -ne 0 ] || fail "tampered artifact passed verification"

head -c 2048 /dev/zero | tr '\0' ' ' > "$TMP/large.json"
set +e
"$BIN" analyze --input "$TMP/large.json" --out "$TMP/large-artifact" \
  --max-input-bytes 1024 > "$TMP/large.log" 2>&1
LARGE_CODE=$?
set -e
[ "$LARGE_CODE" -eq 2 ] || fail "oversized input exit code was $LARGE_CODE"
grep -q 'rejected_oversized' "$TMP/large-artifact/analysis.json" ||
  fail "oversized status missing"
[ -f "$TMP/large-artifact/input/trace.prefix.bin" ] ||
  fail "oversized evidence prefix missing"
grep -q 'prefix_sha256' "$TMP/large-artifact/evidence.jsonl" ||
  fail "oversized prefix hash missing from evidence"
"$BIN" verify --artifact "$TMP/large-artifact" > "$TMP/large-verify.json" ||
  fail "oversized artifact verification failed"

cat > "$TMP/many.json" <<'JSON'
{"schema":"rtsync.trace.v1","devices":[{"id":"a"},{"id":"b"},{"id":"c"}]}
JSON
set +e
"$BIN" analyze --input "$TMP/many.json" --out "$TMP/many-artifact" \
  --max-records 2 > "$TMP/many.log" 2>&1
MANY_CODE=$?
set -e
[ "$MANY_CODE" -eq 2 ] || fail "record limit exit code was $MANY_CODE"
grep -q 'rejected_record_limit' "$TMP/many-artifact/analysis.json" ||
  fail "record-limit status missing"

echo "edge cases passed"

