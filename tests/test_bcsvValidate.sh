#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────
# test_bcsvValidate.sh — Integration tests for the bcsvValidate CLI
#
# Coverage:
#   Mode 1 — Structure validation (text + JSON, --deep)
#   Mode 2 — Pattern validation (pass, fail, wrong profile)
#   Mode 3 — File comparison (bcsv-vs-bcsv, bcsv-vs-csv, mismatch)
#   Misc   — Error handling, --list, --help
# ─────────────────────────────────────────────────────────────────────
set -uo pipefail

# ── Locate binaries ──────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${REPO_ROOT}/build/bin"

VALIDATE="${BIN}/bcsvValidate"
GENERATOR="${BIN}/bcsvGenerator"
CSV2BCSV="${BIN}/csv2bcsv"
BCSV2CSV="${BIN}/bcsv2csv"

for tool in "$VALIDATE" "$GENERATOR" "$CSV2BCSV" "$BCSV2CSV"; do
    if [[ ! -x "$tool" ]]; then
        echo "FATAL: $tool not found – build first." >&2
        exit 1
    fi
done

# ── Temp directory ───────────────────────────────────────────────────
TMPDIR="$(mktemp -d /tmp/bcsvValidate_test.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT
echo "Working directory: $TMPDIR"

# ── Counters ─────────────────────────────────────────────────────────
PASS=0
FAIL=0
ERRORS=""

pass() { PASS=$((PASS + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); ERRORS+="  FAIL: $1\n"; echo "  FAIL: $1"; }

# ══════════════════════════════════════════════════════════════════════
# 0. Prepare test files
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Preparing test files"
echo "═══════════════════════════════════════════════════════════"

# Generate a BCSV from a known profile
SENSOR_BCSV="$TMPDIR/sensor.bcsv"
"$GENERATOR" -p sensor_noisy -n 500 -o "$SENSOR_BCSV" 2>/dev/null
if [[ -f "$SENSOR_BCSV" ]]; then pass "Generate sensor_noisy (500 rows)"; else fail "Generate sensor_noisy"; fi

# Generate a second copy for identical comparison
SENSOR_COPY="$TMPDIR/sensor_copy.bcsv"
"$GENERATOR" -p sensor_noisy -n 500 -o "$SENSOR_COPY" 2>/dev/null
if [[ -f "$SENSOR_COPY" ]]; then pass "Generate sensor_noisy copy"; else fail "Generate sensor_noisy copy"; fi

# Generate a different profile for mismatch test
MIXED_BCSV="$TMPDIR/mixed.bcsv"
"$GENERATOR" -p mixed_generic -n 200 -o "$MIXED_BCSV" 2>/dev/null
if [[ -f "$MIXED_BCSV" ]]; then pass "Generate mixed_generic (200 rows)"; else fail "Generate mixed_generic"; fi

# Export sensor to CSV for bcsv-vs-csv tests
SENSOR_CSV="$TMPDIR/sensor.csv"
"$BCSV2CSV" "$SENSOR_BCSV" "$SENSOR_CSV" 2>/dev/null
if [[ -f "$SENSOR_CSV" ]]; then pass "Export sensor to CSV"; else fail "Export sensor to CSV"; fi

# Create a small CSV for conversion test
SMALL_CSV="$TMPDIR/small.csv"
cat > "$SMALL_CSV" <<'EOF'
time,value,label
1.0,10.5,foo
2.0,20.1,bar
3.0,30.9,baz
EOF
SMALL_BCSV="$TMPDIR/small.bcsv"
"$CSV2BCSV" "$SMALL_CSV" "$SMALL_BCSV" 2>/dev/null
if [[ -f "$SMALL_BCSV" ]]; then pass "csv2bcsv small dataset"; else fail "csv2bcsv small dataset"; fi

# ══════════════════════════════════════════════════════════════════════
# 1. --help and --list
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  1. Help and list"
echo "═══════════════════════════════════════════════════════════"

# --help should exit 0 and contain usage text
HELP=$("$VALIDATE" --help 2>&1)
if [[ $? -eq 0 ]] && echo "$HELP" | grep -q "Validate BCSV files"; then
    pass "--help exits 0 with usage text"
else
    fail "--help"
fi

# --list should show profiles
LIST=$("$VALIDATE" --list 2>&1)
if [[ $? -eq 0 ]] && echo "$LIST" | grep -q "sensor_noisy"; then
    pass "--list shows sensor_noisy profile"
else
    fail "--list"
fi

# ══════════════════════════════════════════════════════════════════════
# 2. Mode 1 — Structure validation
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  2. Mode 1: Structure validation"
echo "═══════════════════════════════════════════════════════════"

# Basic structure check
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" 2>&1)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 1 basic: exit 0"; else fail "Mode 1 basic: exit $RC"; fi
if echo "$OUT" | grep -q "PASSED"; then pass "Mode 1 basic: PASSED"; else fail "Mode 1 basic: no PASSED"; fi
if echo "$OUT" | grep -q "Row count:.*500"; then pass "Mode 1: row count 500"; else fail "Mode 1: row count"; fi
if echo "$OUT" | grep -q "Format version:.*1\.4\.0"; then pass "Mode 1: format version"; else fail "Mode 1: format version"; fi
if echo "$OUT" | grep -q "Packet size:"; then pass "Mode 1: packet size shown"; else fail "Mode 1: packet size"; fi
if echo "$OUT" | grep -q "Total packets:"; then pass "Mode 1: total packets shown"; else fail "Mode 1: total packets"; fi
if echo "$OUT" | grep -q "Avg rows/packet:"; then pass "Mode 1: avg rows/packet shown"; else fail "Mode 1: avg rows/packet"; fi
if echo "$OUT" | grep -q "Footer present:.*yes"; then pass "Mode 1: footer present"; else fail "Mode 1: footer present"; fi

# --deep flag
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" --deep 2>&1)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 1 --deep: exit 0"; else fail "Mode 1 --deep: exit $RC"; fi
if echo "$OUT" | grep -q "Deep check:.*PASSED"; then pass "Mode 1 --deep: PASSED"; else fail "Mode 1 --deep: no PASSED"; fi

# JSON output
JSON=$("$VALIDATE" -i "$SENSOR_BCSV" --json 2>/dev/null)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 1 --json: exit 0"; else fail "Mode 1 --json: exit $RC"; fi
if echo "$JSON" | grep -q '"valid": true'; then pass "Mode 1 JSON: valid=true"; else fail "Mode 1 JSON: valid"; fi
if echo "$JSON" | grep -q '"format_version": "1.4.0"'; then pass "Mode 1 JSON: format_version"; else fail "Mode 1 JSON: format_version"; fi
if echo "$JSON" | grep -q '"row_count": 500'; then pass "Mode 1 JSON: row_count"; else fail "Mode 1 JSON: row_count"; fi
if echo "$JSON" | grep -q '"packet_size":'; then pass "Mode 1 JSON: packet_size"; else fail "Mode 1 JSON: packet_size"; fi
if echo "$JSON" | grep -q '"total_packets":'; then pass "Mode 1 JSON: total_packets"; else fail "Mode 1 JSON: total_packets"; fi
if echo "$JSON" | grep -q '"avg_rows_per_packet":'; then pass "Mode 1 JSON: avg_rows_per_packet"; else fail "Mode 1 JSON: avg_rows_per_packet"; fi
if echo "$JSON" | grep -q '"columns":'; then pass "Mode 1 JSON: columns array"; else fail "Mode 1 JSON: columns"; fi

# ══════════════════════════════════════════════════════════════════════
# 3. Mode 2 — Pattern validation
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  3. Mode 2: Pattern validation"
echo "═══════════════════════════════════════════════════════════"

# Pattern match should pass (same data, same rows)
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" -p sensor_noisy -n 500 2>&1)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 2 pattern match: exit 0"; else fail "Mode 2 pattern match: exit $RC"; fi
if echo "$OUT" | grep -q "PASSED"; then pass "Mode 2 pattern: PASSED"; else fail "Mode 2 pattern: no PASSED"; fi

# Pattern match with JSON
JSON=$("$VALIDATE" -i "$SENSOR_BCSV" -p sensor_noisy -n 500 --json 2>/dev/null)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 2 JSON: exit 0"; else fail "Mode 2 JSON: exit $RC"; fi
if echo "$JSON" | grep -q '"valid": true'; then pass "Mode 2 JSON: valid=true"; else fail "Mode 2 JSON: valid"; fi

# Wrong row count should fail
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" -p sensor_noisy -n 999 2>&1)
RC=$?
if [[ $RC -eq 1 ]]; then pass "Mode 2 wrong rowcount: exit 1"; else fail "Mode 2 wrong rowcount: exit $RC (expected 1)"; fi
if echo "$OUT" | grep -q "MISMATCH\|FAILED"; then pass "Mode 2 wrong rowcount: FAILED"; else fail "Mode 2 wrong rowcount: no FAILED"; fi

# Wrong profile should fail (layout mismatch)
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" -p mixed_generic 2>&1)
RC=$?
if [[ $RC -ne 0 ]]; then pass "Mode 2 wrong profile: non-zero exit"; else fail "Mode 2 wrong profile: exit 0 (expected non-zero)"; fi

# Unknown profile → exit 2
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" -p nonexistent_profile 2>&1)
RC=$?
if [[ $RC -eq 2 ]]; then pass "Mode 2 unknown profile: exit 2"; else fail "Mode 2 unknown profile: exit $RC (expected 2)"; fi
if echo "$OUT" | grep -q "Unknown profile\|--list"; then pass "Mode 2 unknown profile: helpful message"; else fail "Mode 2 unknown profile: message"; fi

# ══════════════════════════════════════════════════════════════════════
# 4. Mode 3 — File comparison (BCSV vs BCSV)
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  4. Mode 3: BCSV vs BCSV comparison"
echo "═══════════════════════════════════════════════════════════"

# Identical files should pass
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" --compare "$SENSOR_COPY" 2>&1)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 3 identical: exit 0"; else fail "Mode 3 identical: exit $RC"; fi
if echo "$OUT" | grep -q "PASSED"; then pass "Mode 3 identical: PASSED"; else fail "Mode 3 identical: no PASSED"; fi

# Different layout should fail
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" --compare "$MIXED_BCSV" 2>&1)
RC=$?
if [[ $RC -eq 1 ]]; then pass "Mode 3 layout mismatch: exit 1"; else fail "Mode 3 layout mismatch: exit $RC (expected 1)"; fi
if echo "$OUT" | grep -q "MISMATCH\|FAILED"; then pass "Mode 3 layout mismatch: FAILED"; else fail "Mode 3 layout mismatch: no FAILED"; fi

# JSON output for comparison
JSON=$("$VALIDATE" -i "$SENSOR_BCSV" --compare "$SENSOR_COPY" --json 2>/dev/null)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 3 JSON identical: exit 0"; else fail "Mode 3 JSON identical: exit $RC"; fi
if echo "$JSON" | grep -q '"valid": true'; then pass "Mode 3 JSON: valid=true"; else fail "Mode 3 JSON: valid"; fi

# ══════════════════════════════════════════════════════════════════════
# 5. Mode 3 — BCSV vs CSV comparison
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  5. Mode 3: BCSV vs CSV comparison"
echo "═══════════════════════════════════════════════════════════"

# Round-trip: bcsv → csv → compare should pass
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" --compare "$SENSOR_CSV" 2>&1)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 3 bcsv-vs-csv: exit 0"; else fail "Mode 3 bcsv-vs-csv: exit $RC"; fi
if echo "$OUT" | grep -q "PASSED"; then pass "Mode 3 bcsv-vs-csv: PASSED"; else fail "Mode 3 bcsv-vs-csv: no PASSED"; fi

# JSON output for bcsv-vs-csv
JSON=$("$VALIDATE" -i "$SENSOR_BCSV" --compare "$SENSOR_CSV" --json 2>/dev/null)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 3 bcsv-vs-csv JSON: exit 0"; else fail "Mode 3 bcsv-vs-csv JSON: exit $RC"; fi

# Small dataset: csv → bcsv → validate against original csv
OUT=$("$VALIDATE" -i "$SMALL_BCSV" --compare "$SMALL_CSV" 2>&1)
RC=$?
if [[ $RC -eq 0 ]]; then pass "Mode 3 small bcsv-vs-csv: exit 0"; else fail "Mode 3 small bcsv-vs-csv: exit $RC"; fi
if echo "$OUT" | grep -q "PASSED"; then pass "Mode 3 small bcsv-vs-csv: PASSED"; else fail "Mode 3 small bcsv-vs-csv: no PASSED"; fi

# ══════════════════════════════════════════════════════════════════════
# 6. Error handling
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  6. Error handling"
echo "═══════════════════════════════════════════════════════════"

# Missing file → exit 2
OUT=$("$VALIDATE" -i /tmp/nonexistent_file_12345.bcsv 2>&1)
RC=$?
if [[ $RC -eq 2 ]]; then pass "Missing file: exit 2"; else fail "Missing file: exit $RC (expected 2)"; fi

# No input file → exit 2
OUT=$("$VALIDATE" 2>&1)
RC=$?
if [[ $RC -eq 2 ]]; then pass "No input: exit 2"; else fail "No input: exit $RC (expected 2)"; fi

# Unknown option → exit 2
OUT=$("$VALIDATE" --unknown-option 2>&1)
RC=$?
if [[ $RC -eq 2 ]]; then pass "Unknown option: exit 2"; else fail "Unknown option: exit $RC (expected 2)"; fi

# Compare with missing file B → exit 2
OUT=$("$VALIDATE" -i "$SENSOR_BCSV" --compare /tmp/nonexistent_12345.bcsv 2>&1)
RC=$?
if [[ $RC -eq 2 ]]; then pass "Compare missing file B: exit 2"; else fail "Compare missing file B: exit $RC (expected 2)"; fi

# ══════════════════════════════════════════════════════════════════════
# 7. Pipeline test: generate → validate structure → validate pattern
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  7. Pipeline: generate → validate"
echo "═══════════════════════════════════════════════════════════"

PIPE_BCSV="$TMPDIR/pipeline.bcsv"
"$GENERATOR" -p mixed_generic -n 1000 -o "$PIPE_BCSV" 2>/dev/null

# Structure check
"$VALIDATE" -i "$PIPE_BCSV" --deep 2>&1 | grep -q "PASSED"
if [[ $? -eq 0 ]]; then pass "Pipeline: structure + deep"; else fail "Pipeline: structure + deep"; fi

# Pattern check
"$VALIDATE" -i "$PIPE_BCSV" -p mixed_generic -n 1000 2>&1 | grep -q "PASSED"
if [[ $? -eq 0 ]]; then pass "Pipeline: pattern match"; else fail "Pipeline: pattern match"; fi

# Export to CSV and compare back
PIPE_CSV="$TMPDIR/pipeline.csv"
if "$BCSV2CSV" "$PIPE_BCSV" "$PIPE_CSV" 2>/dev/null && [[ -f "$PIPE_CSV" ]]; then
    OUT=$("$VALIDATE" -i "$PIPE_BCSV" --compare "$PIPE_CSV" 2>&1)
    if echo "$OUT" | grep -q "PASSED"; then pass "Pipeline: bcsv-vs-csv roundtrip"; else fail "Pipeline: bcsv-vs-csv roundtrip"; fi
else
    fail "Pipeline: bcsv-vs-csv roundtrip (bcsv2csv export failed)"
fi

# ══════════════════════════════════════════════════════════════════════
# Summary
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  Results: $PASS / $TOTAL passed"
if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo -e "$ERRORS"
    echo "═══════════════════════════════════════════════════════════"
    exit 1
else
    echo "  All tests passed!"
    echo "═══════════════════════════════════════════════════════════"
    exit 0
fi
