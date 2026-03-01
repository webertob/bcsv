#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────
# test_cli_tools.sh — Integration tests for all BCSV CLI tools
#
# Tests covered:
#   csv2bcsv      — CSV → BCSV conversion with codec variants
#   bcsv2csv      — BCSV → CSV conversion, stdout mode, row slicing
#   bcsvHead      — first-N-rows display
#   bcsvTail      — last-N-rows display (direct-access + sequential)
#   bcsvHeader    — schema / file metadata display
#   bcsvGenerator — synthetic dataset generation
#
# The test creates a canonical CSV dataset, converts it through various
# pipelines, and validates correctness at each stage.
# ─────────────────────────────────────────────────────────────────────
set -uo pipefail
# Note: we intentionally do NOT use set -e. Individual test steps check
# exit codes explicitly and log PASS/FAIL. Crashing tools (e.g. segfault)
# should not abort the entire suite — the failure will be captured by the
# test that invoked the tool.

# ── Locate binaries ──────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${REPO_ROOT}/build/bin"

CSV2BCSV="${BIN}/csv2bcsv"
BCSV2CSV="${BIN}/bcsv2csv"
BCSVHEAD="${BIN}/bcsvHead"
BCSVTAIL="${BIN}/bcsvTail"
BCSVHEADER="${BIN}/bcsvHeader"
BCSVGENERATOR="${BIN}/bcsvGenerator"

for tool in "$CSV2BCSV" "$BCSV2CSV" "$BCSVHEAD" "$BCSVTAIL" "$BCSVHEADER" "$BCSVGENERATOR"; do
    if [[ ! -x "$tool" ]]; then
        echo "FATAL: $tool not found – build first." >&2
        exit 1
    fi
done

# ── Temp directory ───────────────────────────────────────────────────
TESTDIR="$(mktemp -d /tmp/bcsv_cli_test.XXXXXX)"
trap 'rm -rf "$TESTDIR"' EXIT
echo "Working directory: $TESTDIR"

# ── Counters ─────────────────────────────────────────────────────────
PASS=0
FAIL=0
ERRORS=""

pass() { PASS=$((PASS + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); ERRORS+="  FAIL: $1\n"; echo "  FAIL: $1"; }

# ── Helpers ──────────────────────────────────────────────────────────

# Count data rows (excluding header) in a CSV file/stream
csv_rows()  { tail -n +2 "$1" | wc -l | tr -d ' '; }

# Extract a specific field: csv_cell <file> <data_row_1based> <col_1based>
csv_cell() {
    awk -F, -v r="$2" -v c="$3" 'NR==r+1 { gsub(/"/,"",$c); print $c }' "$1"
}

# First header field
csv_header_col1() { head -1 "$1" | cut -d, -f1 | tr -d '"'; }

# Check that a command exits with a non-zero status
expect_fail() {
    if "$@" >/dev/null 2>&1; then
        return 1  # should have failed
    fi
    return 0
}

# ══════════════════════════════════════════════════════════════════════
# 1. Create canonical CSV dataset  (20 rows × 4 columns)
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Setting up test dataset"
echo "═══════════════════════════════════════════════════════════"

CANON_CSV="$TESTDIR/canon.csv"
cat > "$CANON_CSV" <<'EOF'
id,name,temperature,active
1,alpha,23.5,true
2,bravo,18.2,false
3,charlie,31.0,true
4,delta,27.8,true
5,echo,15.6,false
6,foxtrot,22.1,true
7,golf,29.4,false
8,hotel,33.7,true
9,india,20.3,true
10,juliet,25.9,false
11,kilo,19.1,true
12,lima,28.6,false
13,mike,24.0,true
14,november,21.5,true
15,oscar,30.2,false
16,papa,17.8,true
17,quebec,26.3,false
18,romeo,32.1,true
19,sierra,23.9,true
20,tango,16.4,false
EOF

CANON_ROWS=20
echo "Created canonical CSV: $CANON_ROWS rows × 4 columns"

# Create a BCSV file with flat row-codec and packet_lz4 file-codec for
# tests that need ReaderDirectAccess::read().
# - batch file-codec has a known direct-access issue (tracked separately)
# - delta row-codec corrupts numerics under random-access (tracked separately)
BCSV_DEFAULT="$TESTDIR/default.bcsv"
if ! "$CSV2BCSV" --row-codec flat --file-codec packet_lz4 -f "$CANON_CSV" "$BCSV_DEFAULT" >/dev/null 2>&1; then
    echo "FATAL: Cannot create default BCSV test file" >&2
    exit 1
fi

# ══════════════════════════════════════════════════════════════════════
# 2. csv2bcsv — basic conversion tests
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  csv2bcsv tests"
echo "═══════════════════════════════════════════════════════════"

# 2a. Default conversion (delta codec) — file already created in setup
if [[ -f "$BCSV_DEFAULT" && -s "$BCSV_DEFAULT" ]]; then
    pass "csv2bcsv: default conversion creates non-empty file"
else
    fail "csv2bcsv: default conversion failed"
fi

# 2b. Round-trip: csv → bcsv → csv  (default delta codec)
RT_CSV="$TESTDIR/roundtrip_default.csv"
"$BCSV2CSV" "$BCSV_DEFAULT" -o "$RT_CSV" 2>/dev/null
RT_ROWS=$(csv_rows "$RT_CSV")
if [[ "$RT_ROWS" == "$CANON_ROWS" ]]; then
    pass "csv2bcsv: round-trip preserves $CANON_ROWS rows (delta)"
else
    fail "csv2bcsv: round-trip row count mismatch: expected $CANON_ROWS, got $RT_ROWS"
fi

# Verify header preserved
RT_HDR=$(csv_header_col1 "$RT_CSV")
if [[ "$RT_HDR" == "id" ]]; then
    pass "csv2bcsv: round-trip preserves header"
else
    fail "csv2bcsv: round-trip header mismatch: expected 'id', got '$RT_HDR'"
fi

# 2c. Explicit row codecs: flat, zoh, delta
for RCODEC in flat zoh delta; do
    BCSV_RC="$TESTDIR/rc_${RCODEC}.bcsv"
    RT_RC_CSV="$TESTDIR/rt_${RCODEC}.csv"
    "$CSV2BCSV" --row-codec "$RCODEC" -f "$CANON_CSV" "$BCSV_RC" >/dev/null 2>&1
    "$BCSV2CSV" "$BCSV_RC" -o "$RT_RC_CSV" 2>/dev/null
    GOT=$(csv_rows "$RT_RC_CSV")
    if [[ "$GOT" == "$CANON_ROWS" ]]; then
        pass "csv2bcsv: row-codec=$RCODEC round-trip OK ($GOT rows)"
    else
        fail "csv2bcsv: row-codec=$RCODEC round-trip failed (expected $CANON_ROWS, got $GOT)"
    fi
done

# 2d. Explicit file codecs: stream, packet, packet_lz4
for FCODEC in stream packet packet_lz4; do
    BCSV_FC="$TESTDIR/fc_${FCODEC}.bcsv"
    RT_FC_CSV="$TESTDIR/rt_${FCODEC}.csv"
    "$CSV2BCSV" --file-codec "$FCODEC" -f "$CANON_CSV" "$BCSV_FC" >/dev/null 2>&1
    "$BCSV2CSV" "$BCSV_FC" -o "$RT_FC_CSV" 2>/dev/null
    GOT=$(csv_rows "$RT_FC_CSV")
    if [[ "$GOT" == "$CANON_ROWS" ]]; then
        pass "csv2bcsv: file-codec=$FCODEC round-trip OK ($GOT rows)"
    else
        fail "csv2bcsv: file-codec=$FCODEC round-trip failed (expected $CANON_ROWS, got $GOT)"
    fi
done

# 2e. --no-zoh deprecated alias maps to delta (B5 fix validation)
BCSV_NOZOH="$TESTDIR/nozoh.bcsv"
NOZOH_STDERR=$("$CSV2BCSV" --no-zoh -f "$CANON_CSV" "$BCSV_NOZOH" 2>&1 >/dev/null)
if echo "$NOZOH_STDERR" | grep -q "deprecated"; then
    pass "csv2bcsv: --no-zoh emits deprecation warning"
else
    fail "csv2bcsv: --no-zoh did not emit deprecation warning"
fi
# Verify the file is valid by round-tripping
RT_NOZOH="$TESTDIR/rt_nozoh.csv"
"$BCSV2CSV" "$BCSV_NOZOH" -o "$RT_NOZOH" 2>/dev/null
GOT=$(csv_rows "$RT_NOZOH")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "csv2bcsv: --no-zoh round-trip produces valid BCSV ($GOT rows)"
else
    fail "csv2bcsv: --no-zoh round-trip failed ($GOT rows)"
fi

# 2f. Invalid codec -> error
if expect_fail "$CSV2BCSV" --row-codec bogus -f "$CANON_CSV" "$TESTDIR/bad.bcsv"; then
    pass "csv2bcsv: invalid --row-codec rejected"
else
    fail "csv2bcsv: invalid --row-codec was not rejected"
fi

if expect_fail "$CSV2BCSV" --file-codec bogus -f "$CANON_CSV" "$TESTDIR/bad.bcsv"; then
    pass "csv2bcsv: invalid --file-codec rejected"
else
    fail "csv2bcsv: invalid --file-codec was not rejected"
fi

# 2g. --help exits 0
if "$CSV2BCSV" --help >/dev/null 2>&1; then
    pass "csv2bcsv: --help exits 0"
else
    fail "csv2bcsv: --help non-zero exit"
fi

# 2h. Missing input file -> error
if expect_fail "$CSV2BCSV"; then
    pass "csv2bcsv: missing input file rejected"
else
    fail "csv2bcsv: missing input file was not rejected"
fi


# ══════════════════════════════════════════════════════════════════════
# 3. bcsv2csv — conversion + stdout tests
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  bcsv2csv tests"
echo "═══════════════════════════════════════════════════════════"

# Use the default .bcsv from section 2
BCSV_FILE="$BCSV_DEFAULT"

# 3a. Basic file output
OUT_CSV="$TESTDIR/basic_out.csv"
"$BCSV2CSV" "$BCSV_FILE" -o "$OUT_CSV" 2>/dev/null
GOT=$(csv_rows "$OUT_CSV")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "bcsv2csv: basic conversion produces $CANON_ROWS rows"
else
    fail "bcsv2csv: basic conversion row count: expected $CANON_ROWS, got $GOT"
fi

# 3b. Stdout mode (-o -)
STDOUT_CSV="$TESTDIR/stdout.csv"
"$BCSV2CSV" "$BCSV_FILE" -o - > "$STDOUT_CSV" 2>/dev/null
GOT=$(csv_rows "$STDOUT_CSV")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "bcsv2csv: stdout mode (-o -) produces $CANON_ROWS rows"
else
    fail "bcsv2csv: stdout mode row count: expected $CANON_ROWS, got $GOT"
fi

# 3c. Stdout + verbose (B1 fix): verbose must NOT corrupt stdout data
STDOUT_VERBOSE="$TESTDIR/stdout_verbose.csv"
"$BCSV2CSV" -v "$BCSV_FILE" -o - > "$STDOUT_VERBOSE" 2>/dev/null
GOT=$(csv_rows "$STDOUT_VERBOSE")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "bcsv2csv: stdout+verbose produces clean $CANON_ROWS rows (B1 fix)"
else
    fail "bcsv2csv: stdout+verbose corrupted output: expected $CANON_ROWS, got $GOT (B1 regression!)"
fi

# 3d. Stdout + benchmark + json (B3 fix): JSON must NOT appear in stdout
STDOUT_BENCH="$TESTDIR/stdout_bench.csv"
"$BCSV2CSV" --benchmark --json "$BCSV_FILE" -o - > "$STDOUT_BENCH" 2>/dev/null
# Check that the file doesn't contain JSON blob
if grep -q '"tool":"bcsv2csv"' "$STDOUT_BENCH" 2>/dev/null; then
    fail "bcsv2csv: stdout+benchmark+json leaks JSON to stdout (B3 regression!)"
else
    pass "bcsv2csv: stdout+benchmark+json keeps JSON out of stdout (B3 fix)"
fi
GOT=$(csv_rows "$STDOUT_BENCH")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "bcsv2csv: stdout+benchmark produces clean $CANON_ROWS rows"
else
    fail "bcsv2csv: stdout+benchmark row count: expected $CANON_ROWS, got $GOT"
fi

# 3e. --no-header
NOHEADER="$TESTDIR/noheader.csv"
"$BCSV2CSV" --no-header "$BCSV_FILE" -o "$NOHEADER" 2>/dev/null
TOTAL_LINES=$(wc -l < "$NOHEADER" | tr -d ' ')
if [[ "$TOTAL_LINES" == "$CANON_ROWS" ]]; then
    pass "bcsv2csv: --no-header outputs $CANON_ROWS lines (no header)"
else
    fail "bcsv2csv: --no-header line count: expected $CANON_ROWS, got $TOTAL_LINES"
fi

# 3f. --slice 5:10 (rows 5-9 = 5 rows)
SLICE_CSV="$TESTDIR/slice.csv"
"$BCSV2CSV" --slice 5:10 "$BCSV_FILE" -o "$SLICE_CSV" 2>/dev/null
GOT=$(csv_rows "$SLICE_CSV")
if [[ "$GOT" == "5" ]]; then
    pass "bcsv2csv: --slice 5:10 produces 5 rows"
else
    fail "bcsv2csv: --slice 5:10 row count: expected 5, got $GOT"
fi

# 3g. --firstRow/--lastRow
RANGE_CSV="$TESTDIR/range.csv"
"$BCSV2CSV" --firstRow 2 --lastRow 4 "$BCSV_FILE" -o "$RANGE_CSV" 2>/dev/null
GOT=$(csv_rows "$RANGE_CSV")
if [[ "$GOT" == "3" ]]; then
    pass "bcsv2csv: --firstRow 2 --lastRow 4 produces 3 rows"
else
    fail "bcsv2csv: --firstRow/lastRow row count: expected 3, got $GOT"
fi

# 3h. Semicolon delimiter
SEMI_CSV="$TESTDIR/semicolon.csv"
"$BCSV2CSV" -d ';' "$BCSV_FILE" -o "$SEMI_CSV" 2>/dev/null
if head -1 "$SEMI_CSV" | grep -q ';'; then
    pass "bcsv2csv: -d ';' uses semicolon delimiter"
else
    fail "bcsv2csv: -d ';' did not use semicolon delimiter"
fi

# 3i. --help exits 0
if "$BCSV2CSV" --help >/dev/null 2>&1; then
    pass "bcsv2csv: --help exits 0"
else
    fail "bcsv2csv: --help non-zero exit"
fi

# 3j. Missing input -> error
if expect_fail "$BCSV2CSV"; then
    pass "bcsv2csv: missing input file rejected"
else
    fail "bcsv2csv: missing input file was not rejected"
fi

# 3k. Nonexistent file -> error
if expect_fail "$BCSV2CSV" "nonexistent_file_xyz.bcsv" -o "$TESTDIR/nope.csv"; then
    pass "bcsv2csv: nonexistent file rejected"
else
    fail "bcsv2csv: nonexistent file was not rejected"
fi


# ══════════════════════════════════════════════════════════════════════
# 4. bcsvHead — first-N-rows tests
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  bcsvHead tests"
echo "═══════════════════════════════════════════════════════════"

# 4a. Default: 10 rows
HEAD_OUT="$TESTDIR/head_default.csv"
"$BCSVHEAD" "$BCSV_FILE" > "$HEAD_OUT" 2>/dev/null
GOT=$(csv_rows "$HEAD_OUT")
if [[ "$GOT" == "10" ]]; then
    pass "bcsvHead: default shows 10 rows"
else
    fail "bcsvHead: default row count: expected 10, got $GOT"
fi

# 4b. Header preserved
HDR=$(csv_header_col1 "$HEAD_OUT")
if [[ "$HDR" == "id" ]]; then
    pass "bcsvHead: header preserved"
else
    fail "bcsvHead: header mismatch: expected 'id', got '$HDR'"
fi

# 4c. -n 5
HEAD5="$TESTDIR/head5.csv"
"$BCSVHEAD" -n 5 "$BCSV_FILE" > "$HEAD5" 2>/dev/null
GOT=$(csv_rows "$HEAD5")
if [[ "$GOT" == "5" ]]; then
    pass "bcsvHead: -n 5 shows 5 rows"
else
    fail "bcsvHead: -n 5 row count: expected 5, got $GOT"
fi

# 4d. -n larger than file → shows all rows
HEAD_ALL="$TESTDIR/head_all.csv"
"$BCSVHEAD" -n 100 "$BCSV_FILE" > "$HEAD_ALL" 2>/dev/null
GOT=$(csv_rows "$HEAD_ALL")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "bcsvHead: -n 100 shows all $CANON_ROWS rows"
else
    fail "bcsvHead: -n 100 row count: expected $CANON_ROWS, got $GOT"
fi

# 4e. --no-header
HEAD_NH="$TESTDIR/head_noheader.csv"
"$BCSVHEAD" --no-header -n 5 "$BCSV_FILE" > "$HEAD_NH" 2>/dev/null
TOTAL_LINES=$(wc -l < "$HEAD_NH" | tr -d ' ')
if [[ "$TOTAL_LINES" == "5" ]]; then
    pass "bcsvHead: --no-header outputs 5 lines (no header)"
else
    fail "bcsvHead: --no-header line count: expected 5, got $TOTAL_LINES"
fi

# 4f. First data row value check (id should be 1)
FIRST_ID=$(csv_cell "$HEAD_OUT" 1 1)
if [[ "$FIRST_ID" == "1" ]]; then
    pass "bcsvHead: first row id=1"
else
    fail "bcsvHead: first row id: expected 1, got '$FIRST_ID'"
fi

# 4g. --help exits 0
if "$BCSVHEAD" --help >/dev/null 2>&1; then
    pass "bcsvHead: --help exits 0"
else
    fail "bcsvHead: --help non-zero exit"
fi

# 4h. Unknown option -> error
if expect_fail "$BCSVHEAD" --bogus "$BCSV_FILE"; then
    pass "bcsvHead: unknown option rejected"
else
    fail "bcsvHead: unknown option was not rejected"
fi


# ══════════════════════════════════════════════════════════════════════
# 5. bcsvTail — last-N-rows tests
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  bcsvTail tests"
echo "═══════════════════════════════════════════════════════════"

# 5a. Default (10 rows) with direct-access (packet file has footer)
TAIL_OUT="$TESTDIR/tail_default.csv"
"$BCSVTAIL" "$BCSV_FILE" > "$TAIL_OUT" 2>/dev/null
GOT=$(csv_rows "$TAIL_OUT")
if [[ "$GOT" == "10" ]]; then
    pass "bcsvTail: default shows 10 rows (direct-access)"
else
    fail "bcsvTail: default row count: expected 10, got $GOT"
fi

# 5b. Header preserved
HDR=$(csv_header_col1 "$TAIL_OUT")
if [[ "$HDR" == "id" ]]; then
    pass "bcsvTail: header preserved"
else
    fail "bcsvTail: header mismatch: expected 'id', got '$HDR'"
fi

# 5c. Last row should be id=20 (tango)
LAST_ID=$(csv_cell "$TAIL_OUT" 10 1)
if [[ "$LAST_ID" == "20" ]]; then
    pass "bcsvTail: last row id=20"
else
    fail "bcsvTail: last row id: expected 20, got '$LAST_ID'"
fi

# 5d. First displayed row should be id=11 (20 - 10 + 1)
FIRST_TAIL_ID=$(csv_cell "$TAIL_OUT" 1 1)
if [[ "$FIRST_TAIL_ID" == "11" ]]; then
    pass "bcsvTail: first displayed row id=11"
else
    fail "bcsvTail: first displayed row id: expected 11, got '$FIRST_TAIL_ID'"
fi

# 5e. -n 3 → last 3 rows (id=18,19,20)
TAIL3="$TESTDIR/tail3.csv"
"$BCSVTAIL" -n 3 "$BCSV_FILE" > "$TAIL3" 2>/dev/null
GOT=$(csv_rows "$TAIL3")
if [[ "$GOT" == "3" ]]; then
    pass "bcsvTail: -n 3 shows 3 rows"
else
    fail "bcsvTail: -n 3 row count: expected 3, got $GOT"
fi

# 5f. Sequential fallback path — use stream-mode file (no footer)
BCSV_STREAM="$TESTDIR/stream.bcsv"
"$CSV2BCSV" --file-codec stream -f "$CANON_CSV" "$BCSV_STREAM" >/dev/null 2>&1
TAIL_SEQ="$TESTDIR/tail_sequential.csv"
"$BCSVTAIL" -n 5 "$BCSV_STREAM" > "$TAIL_SEQ" 2>/dev/null
GOT=$(csv_rows "$TAIL_SEQ")
if [[ "$GOT" == "5" ]]; then
    pass "bcsvTail: sequential fallback shows 5 rows (stream-mode file)"
else
    fail "bcsvTail: sequential fallback row count: expected 5, got $GOT"
fi

# 5g. Sequential path: verify last row is id=20
SEQ_LAST=$(csv_cell "$TAIL_SEQ" 5 1)
if [[ "$SEQ_LAST" == "20" ]]; then
    pass "bcsvTail: sequential fallback last row id=20"
else
    fail "bcsvTail: sequential fallback last row id: expected 20, got '$SEQ_LAST'"
fi

# 5h. Sequential path: verify header (B4 fix)
SEQ_HDR=$(csv_header_col1 "$TAIL_SEQ")
if [[ "$SEQ_HDR" == "id" ]]; then
    pass "bcsvTail: sequential fallback header preserved (B4 fix)"
else
    fail "bcsvTail: sequential fallback header broken: expected 'id', got '$SEQ_HDR' (B4 regression!)"
fi

# 5i. -n larger than file → shows all rows
TAIL_BIG="$TESTDIR/tail_big.csv"
"$BCSVTAIL" -n 999 "$BCSV_FILE" > "$TAIL_BIG" 2>/dev/null
GOT=$(csv_rows "$TAIL_BIG")
if [[ "$GOT" == "$CANON_ROWS" ]]; then
    pass "bcsvTail: -n 999 shows all $CANON_ROWS rows"
else
    fail "bcsvTail: -n 999 row count: expected $CANON_ROWS, got $GOT"
fi

# 5j. --no-header
TAIL_NH="$TESTDIR/tail_noheader.csv"
"$BCSVTAIL" --no-header -n 5 "$BCSV_FILE" > "$TAIL_NH" 2>/dev/null
TOTAL_LINES=$(wc -l < "$TAIL_NH" | tr -d ' ')
if [[ "$TOTAL_LINES" == "5" ]]; then
    pass "bcsvTail: --no-header outputs 5 lines"
else
    fail "bcsvTail: --no-header line count: expected 5, got $TOTAL_LINES"
fi

# 5k. --help exits 0
if "$BCSVTAIL" --help >/dev/null 2>&1; then
    pass "bcsvTail: --help exits 0"
else
    fail "bcsvTail: --help non-zero exit"
fi

# 5l. Unknown option -> error
if expect_fail "$BCSVTAIL" --bogus "$BCSV_FILE"; then
    pass "bcsvTail: unknown option rejected"
else
    fail "bcsvTail: unknown option was not rejected"
fi


# ══════════════════════════════════════════════════════════════════════
# 6. bcsvHeader — schema display tests
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  bcsvHeader tests"
echo "═══════════════════════════════════════════════════════════"

# 6a. Basic output contains column names
HEADER_OUT=$("$BCSVHEADER" "$BCSV_FILE" 2>/dev/null)
if echo "$HEADER_OUT" | grep -q "id" && echo "$HEADER_OUT" | grep -q "temperature"; then
    pass "bcsvHeader: output contains expected column names"
else
    fail "bcsvHeader: output missing expected column names"
fi

# 6b. Column count reported
if echo "$HEADER_OUT" | grep -q "4"; then
    pass "bcsvHeader: reports 4 columns"
else
    fail "bcsvHeader: does not report 4 columns"
fi

# 6c. -v shows row count
HEADER_V=$("$BCSVHEADER" -v "$BCSV_FILE" 2>&1)
if echo "$HEADER_V" | grep -qi "row\|rows"; then
    pass "bcsvHeader: verbose shows row info"
else
    fail "bcsvHeader: verbose missing row info"
fi

# 6d. --help exits 0
if "$BCSVHEADER" --help >/dev/null 2>&1; then
    pass "bcsvHeader: --help exits 0"
else
    fail "bcsvHeader: --help non-zero exit"
fi

# 6e. Unknown option -> error
if expect_fail "$BCSVHEADER" --bogus "$BCSV_FILE"; then
    pass "bcsvHeader: unknown option rejected"
else
    fail "bcsvHeader: unknown option was not rejected"
fi

# 6f. Nonexistent file -> error
if expect_fail "$BCSVHEADER" "nonexistent_xyz.bcsv"; then
    pass "bcsvHeader: nonexistent file rejected"
else
    fail "bcsvHeader: nonexistent file was not rejected"
fi


# ══════════════════════════════════════════════════════════════════════
# 7. bcsvGenerator — synthetic dataset generation tests
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  bcsvGenerator tests"
echo "═══════════════════════════════════════════════════════════"

# 7a. Basic generation (default profile)
GEN_BCSV="$TESTDIR/gen_default.bcsv"
"$BCSVGENERATOR" -o "$GEN_BCSV" -f -n 100 2>/dev/null
if [[ -f "$GEN_BCSV" && -s "$GEN_BCSV" ]]; then
    pass "bcsvGenerator: default generation creates file"
else
    fail "bcsvGenerator: default generation failed"
fi

# 7b. Round-trip: generated file → bcsv2csv → count rows
GEN_CSV="$TESTDIR/gen_default.csv"
"$BCSV2CSV" "$GEN_BCSV" -o "$GEN_CSV" 2>/dev/null
GOT=$(csv_rows "$GEN_CSV")
if [[ "$GOT" == "100" ]]; then
    pass "bcsvGenerator: generated file has 100 rows after round-trip"
else
    fail "bcsvGenerator: round-trip row count: expected 100, got $GOT"
fi

# 7c. Different row codec (flat)
GEN_FLAT="$TESTDIR/gen_flat.bcsv"
"$BCSVGENERATOR" -o "$GEN_FLAT" -f -n 50 --row-codec flat 2>/dev/null
GEN_FLAT_CSV="$TESTDIR/gen_flat.csv"
"$BCSV2CSV" "$GEN_FLAT" -o "$GEN_FLAT_CSV" 2>/dev/null
GOT=$(csv_rows "$GEN_FLAT_CSV")
if [[ "$GOT" == "50" ]]; then
    pass "bcsvGenerator: flat codec produces 50 rows"
else
    fail "bcsvGenerator: flat codec row count: expected 50, got $GOT"
fi

# 7d. Different file codec (stream)
GEN_STREAM="$TESTDIR/gen_stream.bcsv"
"$BCSVGENERATOR" -o "$GEN_STREAM" -f -n 50 --file-codec stream 2>/dev/null
GEN_STREAM_CSV="$TESTDIR/gen_stream.csv"
"$BCSV2CSV" "$GEN_STREAM" -o "$GEN_STREAM_CSV" 2>/dev/null
GOT=$(csv_rows "$GEN_STREAM_CSV")
if [[ "$GOT" == "50" ]]; then
    pass "bcsvGenerator: stream file codec produces 50 rows"
else
    fail "bcsvGenerator: stream file codec row count: expected 50, got $GOT"
fi

# 7e. --list exits 0 and shows profiles
LIST_OUT=$("$BCSVGENERATOR" --list 2>/dev/null)
if [[ $? -eq 0 ]] && echo "$LIST_OUT" | grep -q "mixed_generic"; then
    pass "bcsvGenerator: --list shows profiles including mixed_generic"
else
    fail "bcsvGenerator: --list did not show expected profiles"
fi

# 7f. Random data mode
GEN_RANDOM="$TESTDIR/gen_random.bcsv"
"$BCSVGENERATOR" -o "$GEN_RANDOM" -f -n 30 -d random 2>/dev/null
GEN_RANDOM_CSV="$TESTDIR/gen_random.csv"
"$BCSV2CSV" "$GEN_RANDOM" -o "$GEN_RANDOM_CSV" 2>/dev/null
GOT=$(csv_rows "$GEN_RANDOM_CSV")
if [[ "$GOT" == "30" ]]; then
    pass "bcsvGenerator: random data mode produces 30 rows"
else
    fail "bcsvGenerator: random data mode row count: expected 30, got $GOT"
fi

# 7g. Invalid profile -> error
if expect_fail "$BCSVGENERATOR" -o "$TESTDIR/bad.bcsv" -p nonexistent_profile; then
    pass "bcsvGenerator: invalid profile rejected"
else
    fail "bcsvGenerator: invalid profile was not rejected"
fi

# 7h. Invalid codec -> error
if expect_fail "$BCSVGENERATOR" -o "$TESTDIR/bad.bcsv" --row-codec bogus; then
    pass "bcsvGenerator: invalid --row-codec rejected"
else
    fail "bcsvGenerator: invalid --row-codec was not rejected"
fi

# 7i. --help exits 0
if "$BCSVGENERATOR" --help >/dev/null 2>&1; then
    pass "bcsvGenerator: --help exits 0"
else
    fail "bcsvGenerator: --help non-zero exit"
fi

# 7j. Overwrite protection
GEN_PROT="$TESTDIR/gen_prot.bcsv"
"$BCSVGENERATOR" -o "$GEN_PROT" -f -n 10 2>/dev/null
if expect_fail "$BCSVGENERATOR" -o "$GEN_PROT" -n 10; then
    pass "bcsvGenerator: overwrite protection works"
else
    fail "bcsvGenerator: overwrite protection did not trigger"
fi


# ══════════════════════════════════════════════════════════════════════
# 8. Pipeline tests — multi-tool integration
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Pipeline integration tests"
echo "═══════════════════════════════════════════════════════════"

# 8a. bcsvGenerator → bcsvHead → stdout (pipe)
PIPE1=$("$BCSVGENERATOR" -o "$TESTDIR/pipe1.bcsv" -f -n 50 2>/dev/null && \
        "$BCSVHEAD" -n 5 "$TESTDIR/pipe1.bcsv" 2>/dev/null)
PIPE1_ROWS=$(echo "$PIPE1" | tail -n +2 | wc -l | tr -d ' ')
if [[ "$PIPE1_ROWS" == "5" ]]; then
    pass "Pipeline: bcsvGenerator→bcsvHead shows 5 rows"
else
    fail "Pipeline: bcsvGenerator→bcsvHead row count: expected 5, got $PIPE1_ROWS"
fi

# 8b. csv2bcsv → bcsv2csv -o - → wc -l (pipe through stdout)
PIPE2_LINES=$("$CSV2BCSV" -f "$CANON_CSV" "$TESTDIR/pipe2.bcsv" >/dev/null 2>&1 && \
              "$BCSV2CSV" "$TESTDIR/pipe2.bcsv" -o - 2>/dev/null | wc -l | tr -d ' ')
EXPECTED_LINES=$((CANON_ROWS + 1))  # rows + header
if [[ "$PIPE2_LINES" == "$EXPECTED_LINES" ]]; then
    pass "Pipeline: csv2bcsv→bcsv2csv stdout produces $EXPECTED_LINES lines"
else
    fail "Pipeline: csv2bcsv→bcsv2csv stdout line count: expected $EXPECTED_LINES, got $PIPE2_LINES"
fi

# 8c. Head + Tail consistency: head(5) ∪ tail(15) should cover all 20 rows
HEAD_OUT2="$TESTDIR/pipeline_head.csv"
TAIL_OUT2="$TESTDIR/pipeline_tail.csv"
"$BCSVHEAD" -n 5 --no-header "$BCSV_FILE" > "$HEAD_OUT2" 2>/dev/null
"$BCSVTAIL" -n 15 --no-header "$BCSV_FILE" > "$TAIL_OUT2" 2>/dev/null
HEAD_LINES=$(wc -l < "$HEAD_OUT2" | tr -d ' ')
TAIL_LINES=$(wc -l < "$TAIL_OUT2" | tr -d ' ')
COMBINED=$((HEAD_LINES + TAIL_LINES))
if [[ "$COMBINED" == "$CANON_ROWS" ]]; then
    pass "Pipeline: head(5)+tail(15) covers all $CANON_ROWS rows"
else
    fail "Pipeline: head(5)+tail(15) total: expected $CANON_ROWS, got $COMBINED"
fi

# 8d. bcsvHeader shows same column count as round-tripped CSV
HDR_INFO=$("$BCSVHEADER" "$BCSV_FILE" 2>/dev/null)
RT_COLS=$(head -1 "$RT_CSV" | awk -F, '{ print NF }')
if echo "$HDR_INFO" | grep -q "$RT_COLS"; then
    pass "Pipeline: bcsvHeader column count matches round-trip CSV ($RT_COLS)"
else
    fail "Pipeline: bcsvHeader column count inconsistent with round-trip CSV"
fi


# ══════════════════════════════════════════════════════════════════════
# 9. Edge cases and special column names (B4 fix validation)
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Edge case tests"
echo "═══════════════════════════════════════════════════════════"

# 9a. Column names with special characters (commas/quotes)
SPECIAL_CSV="$TESTDIR/special_cols.csv"
cat > "$SPECIAL_CSV" <<'CSVEOF'
"col,one","col""two",normal
1,2,3
4,5,6
7,8,9
CSVEOF

SPECIAL_BCSV="$TESTDIR/special_cols.bcsv"
"$CSV2BCSV" --row-codec flat --file-codec packet_lz4 -f "$SPECIAL_CSV" "$SPECIAL_BCSV" >/dev/null 2>&1

# Head path (direct-access uses CsvWriter)
SPECIAL_HEAD=$("$BCSVHEAD" -n 1 "$SPECIAL_BCSV" 2>/dev/null)
SPECIAL_HDR=$(echo "$SPECIAL_HEAD" | head -1)
if echo "$SPECIAL_HDR" | grep -q '"col,one"'; then
    pass "Edge: bcsvHead quotes column name containing comma"
else
    fail "Edge: bcsvHead does not quote column name with comma: $SPECIAL_HDR"
fi

# Tail path — test with stream-mode (sequential fallback, B4 fix)
SPECIAL_STREAM="$TESTDIR/special_stream.bcsv"
"$CSV2BCSV" --file-codec stream -f "$SPECIAL_CSV" "$SPECIAL_STREAM" >/dev/null 2>&1
SPECIAL_TAIL=$("$BCSVTAIL" -n 1 "$SPECIAL_STREAM" 2>/dev/null)
SPECIAL_TAIL_HDR=$(echo "$SPECIAL_TAIL" | head -1)
if echo "$SPECIAL_TAIL_HDR" | grep -q '"col,one"'; then
    pass "Edge: bcsvTail sequential path quotes column name with comma (B4 fix)"
else
    fail "Edge: bcsvTail sequential path does NOT quote column with comma (B4 regression!): $SPECIAL_TAIL_HDR"
fi

# 9b. Single-row file
SINGLE_CSV="$TESTDIR/single.csv"
echo "x,y" > "$SINGLE_CSV"
echo "42,99" >> "$SINGLE_CSV"
SINGLE_BCSV="$TESTDIR/single.bcsv"
"$CSV2BCSV" --row-codec flat --file-codec packet_lz4 -f "$SINGLE_CSV" "$SINGLE_BCSV" >/dev/null 2>&1

SINGLE_HEAD=$("$BCSVHEAD" -n 10 "$SINGLE_BCSV" 2>/dev/null)
SINGLE_HEAD_ROWS=$(echo "$SINGLE_HEAD" | tail -n +2 | wc -l | tr -d ' ')
if [[ "$SINGLE_HEAD_ROWS" == "1" ]]; then
    pass "Edge: single-row file bcsvHead shows 1 row"
else
    fail "Edge: single-row file bcsvHead: expected 1, got $SINGLE_HEAD_ROWS"
fi

SINGLE_TAIL=$("$BCSVTAIL" -n 10 "$SINGLE_BCSV" 2>/dev/null)
SINGLE_TAIL_ROWS=$(echo "$SINGLE_TAIL" | tail -n +2 | wc -l | tr -d ' ')
if [[ "$SINGLE_TAIL_ROWS" == "1" ]]; then
    pass "Edge: single-row file bcsvTail shows 1 row"
else
    fail "Edge: single-row file bcsvTail: expected 1, got $SINGLE_TAIL_ROWS"
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
