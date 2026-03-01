#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────
# test_bcsvSampler.sh — Round-trip validation of the bcsvSampler CLI
#
# Pipeline: csv2bcsv → bcsvSampler → bcsv2csv
# Dataset:  Canonical 7-row dataset from the sampler plan document
# Coverage: 20 test vectors (TV-01 … TV-36) + 4 compile-error tests
#           + 8 value-level spot checks + 5 encoding variants
#           + 1 disassembly smoke test
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

# ── Locate binaries ──────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${REPO_ROOT}/build/bin"

CSV2BCSV="${BIN}/csv2bcsv"
BCSV2CSV="${BIN}/bcsv2csv"
BCSVSAMPLER="${BIN}/bcsvSampler"
BCSVHEADER="${BIN}/bcsvHeader"

for tool in "$CSV2BCSV" "$BCSV2CSV" "$BCSVSAMPLER" "$BCSVHEADER"; do
    if [[ ! -x "$tool" ]]; then
        echo "FATAL: $tool not found – build first." >&2
        exit 1
    fi
done

# ── Temp directory ───────────────────────────────────────────────────
TMPDIR="$(mktemp -d /tmp/bcsvSampler_test.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT
echo "Working directory: $TMPDIR"

# ── Counters ─────────────────────────────────────────────────────────
PASS=0
FAIL=0
ERRORS=""

pass() { PASS=$((PASS + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); ERRORS+="  FAIL: $1\n"; echo "  FAIL: $1"; }

# ── Helper: count data rows (excluding header) in a CSV file ─────────
csv_rows() { tail -n +2 "$1" | wc -l | tr -d ' '; }

# ── Helper: extract column value from a specific row (1-based) ───────
# csv_cell <file> <row> <col>   (row=1 is first data row, col=1-based)
csv_cell() {
    awk -F, -v r="$2" -v c="$3" 'NR==r+1 { gsub(/"/,"",$c); print $c }' "$1"
}

# ── Helper: float-compare with tolerance ─────────────────────────────
float_eq() {
    awk -v a="$1" -v b="$2" -v eps="${3:-0.01}" \
        'BEGIN { diff = a - b; if (diff<0) diff=-diff; exit !(diff < eps) }'
}

# ══════════════════════════════════════════════════════════════════════
# 1. Create canonical CSV dataset (7 rows × 5 columns)
# ══════════════════════════════════════════════════════════════════════
CANON_CSV="$TMPDIR/canonical.csv"
cat > "$CANON_CSV" <<'EOF'
timestamp,temperature,status,flags,counter
1.0,20.5,ok,6,0
2.0,21.0,ok,7,1
3.0,21.0,warn,3,2
4.0,55.0,alarm,5,3
5.0,55.0,alarm,5,100
6.0,22.0,ok,7,101
7.0,22.5,ok,6,102
EOF
echo "=== Canonical dataset ($CANON_CSV) ==="
cat "$CANON_CSV"
echo ""

# ══════════════════════════════════════════════════════════════════════
# 2. Convert CSV → BCSV (the source for all sampler tests)
# ══════════════════════════════════════════════════════════════════════
CANON_BCSV="$TMPDIR/canonical.bcsv"
echo "=== csv2bcsv ==="
"$CSV2BCSV" --no-zoh "$CANON_CSV" "$CANON_BCSV" 2>&1 | grep -E "rows|Columns|Layout" || true
echo ""

echo "=== Header of canonical.bcsv ==="
"$BCSVHEADER" "$CANON_BCSV"
echo ""

# Quick sanity: round-trip back to CSV to verify data integrity
ROUNDTRIP_CSV="$TMPDIR/roundtrip.csv"
"$BCSV2CSV" "$CANON_BCSV" "$ROUNDTRIP_CSV" 2>/dev/null
RT_ROWS=$(csv_rows "$ROUNDTRIP_CSV")
echo "=== Round-trip verification: $RT_ROWS rows (expected 7) ==="
if [[ "$RT_ROWS" -eq 7 ]]; then pass "Round-trip row count"; else fail "Round-trip row count: got $RT_ROWS, expected 7"; fi
echo ""

# ══════════════════════════════════════════════════════════════════════
# 3. Test Vectors
# ══════════════════════════════════════════════════════════════════════
echo "═══════════════════════════════════════════════════════════"
echo "  Running bcsvSampler test vectors"
echo "═══════════════════════════════════════════════════════════"

run_tv() {
    # run_tv <label> <expected_rows> [bcsvSampler args...]
    local label="$1"; shift
    local expected="$1"; shift
    local out_bcsv="$TMPDIR/${label}.bcsv"
    local out_csv="$TMPDIR/${label}.csv"

    if "$BCSVSAMPLER" "$@" -f "$CANON_BCSV" "$out_bcsv" 2>/dev/null; then
        "$BCSV2CSV" "$out_bcsv" "$out_csv" 2>/dev/null
        local got
        got=$(csv_rows "$out_csv")
        if [[ "$got" -eq "$expected" ]]; then
            pass "$label: $got rows (expected $expected)"
        else
            fail "$label: got $got rows, expected $expected"
        fi
    else
        if [[ "$expected" -eq 0 ]]; then
            # bcsvSampler may still write an empty file
            if [[ -f "$out_bcsv" ]]; then
                "$BCSV2CSV" "$out_bcsv" "$out_csv" 2>/dev/null
                local got
                got=$(csv_rows "$out_csv")
                if [[ "$got" -eq 0 ]]; then
                    pass "$label: 0 rows (expected 0)"
                else
                    fail "$label: got $got rows, expected 0"
                fi
            else
                fail "$label: bcsvSampler failed and no output file created"
            fi
        else
            fail "$label: bcsvSampler exited with error"
        fi
    fi
}

run_tv_expect_fail() {
    # run_tv_expect_fail <label> [bcsvSampler args...]
    local label="$1"; shift
    local out_bcsv="$TMPDIR/${label}.bcsv"

    if "$BCSVSAMPLER" "$@" "$CANON_BCSV" "$out_bcsv" 2>/dev/null; then
        fail "$label: expected compile error, but succeeded"
    else
        pass "$label: compile error detected"
    fi
}

echo ""
echo "── Baseline ──"
run_tv "TV01_passthrough"    7 -c 'true'
run_tv "TV02_false"          0 -c 'false'

echo ""
echo "── Threshold & Comparison ──"
run_tv "TV03_threshold"      2 -c 'X[0][1] > 50.0' -s 'X[0][0], X[0][1]'
run_tv "TV06_str_eq"         2 -c 'X[0][2] == "alarm"' -s 'X[0][0], X[0][2]'
run_tv "TV07_str_neq"        3 -c 'X[0][2] != "ok"' -s 'X[0][0], X[0][2]'
run_tv "TV15_not"            3 -c '!(X[0][2] == "ok")' -s 'X[0][0], X[0][2]'
run_tv "TV16_truthiness"     6 -c 'X[0][4]' -s 'X[0][0], X[0][4]'

echo ""
echo "── Edge Detection & Window ──"
run_tv "TV04_edge_trunc"     4 -c 'X[0][1] != X[-1][1]' -s 'X[0][0], X[-1][1], X[0][1]'
run_tv "TV05_edge_expand"    4 -c 'X[0][1] != X[-1][1]' -s 'X[0][0], X[-1][1], X[0][1]' -m expand
run_tv "TV17_lookahead"      3 -c 'X[+1][1] > X[0][1]' -s 'X[0][0], X[0][1], X[+1][1]'
run_tv "TV19_local_max"      0 -c 'X[0][1] > X[-1][1] && X[0][1] > X[+1][1]' -s 'X[0][0], X[0][1]'

echo ""
echo "── Boolean & Short-Circuit ──"
run_tv "TV09_or"             3 -c 'X[0][1] > 50.0 || X[0][2] == "warn"' -s 'X[0][0]'
run_tv "TV11_modulo"         4 -c 'X[0][4] % 2 == 0' -s 'X[0][0], X[0][4]'

echo ""
echo "── Bitwise ──"
run_tv "TV12_bitand"         6 -c '(X[0][3] & 4) != 0' -s 'X[0][0], X[0][3]'

echo ""
echo "── Signal Processing (Gradient / Moving Average) ──"
run_tv "TV10_gradient"       6 -c 'true' -s 'X[0][0], X[0][1] - X[-1][1]'
run_tv "TV30_interp"         6 -c 'true' -s 'X[0][0], (X[-1][1] + X[0][1]) / 2.0'
run_tv "TV32_mavg3"          5 -c 'true' -s 'X[0][0], (X[-1][1] + X[0][1] + X[+1][1]) / 3.0'
run_tv "TV34_dydx"           6 -c 'true' -s 'X[0][0], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])'

echo ""
echo "── Precedence ──"
run_tv "TV29_precedence"     3 -c 'X[0][4] % (2 + 1) == 0' -s 'X[0][0], X[0][4]'

echo ""
echo "── Compile Errors (expected failures) ──"
run_tv_expect_fail "TV22_str_arith"    -c 'X[0][2] + 1 > 0'
run_tv_expect_fail "TV23_str_order"    -c 'X[0][2] > "ok"'
run_tv_expect_fail "TV24_bad_col_idx"  -c 'X[0][99] > 0'
run_tv_expect_fail "TV25_bad_col_name" -c 'X[0]["nonexistent"] > 0'

# ══════════════════════════════════════════════════════════════════════
# 4. Value-level validation (spot checks on specific cells)
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Value-level spot checks"
echo "═══════════════════════════════════════════════════════════"

# TV-03: rows with temperature > 50 → timestamps 4.0 and 5.0
TV03_CSV="$TMPDIR/TV03_threshold.csv"
if [[ -f "$TV03_CSV" ]]; then
    ts1=$(csv_cell "$TV03_CSV" 1 1)
    ts2=$(csv_cell "$TV03_CSV" 2 1)
    if float_eq "$ts1" 4.0 && float_eq "$ts2" 5.0; then
        pass "TV03 values: timestamps = $ts1, $ts2"
    else
        fail "TV03 values: timestamps = $ts1, $ts2 (expected 4.0, 5.0)"
    fi
fi

# TV-04: edge detection → timestamps 2.0, 4.0, 6.0, 7.0
TV04_CSV="$TMPDIR/TV04_edge_trunc.csv"
if [[ -f "$TV04_CSV" ]]; then
    ts1=$(csv_cell "$TV04_CSV" 1 1)
    ts2=$(csv_cell "$TV04_CSV" 2 1)
    ts3=$(csv_cell "$TV04_CSV" 3 1)
    ts4=$(csv_cell "$TV04_CSV" 4 1)
    if float_eq "$ts1" 2.0 && float_eq "$ts2" 4.0 && float_eq "$ts3" 6.0 && float_eq "$ts4" 7.0; then
        pass "TV04 values: timestamps = $ts1, $ts2, $ts3, $ts4"
    else
        fail "TV04 values: timestamps = $ts1, $ts2, $ts3, $ts4 (expected 2,4,6,7)"
    fi
fi

# TV-06: string equality → timestamps 4.0, 5.0 with status "alarm"
TV06_CSV="$TMPDIR/TV06_str_eq.csv"
if [[ -f "$TV06_CSV" ]]; then
    s1=$(csv_cell "$TV06_CSV" 1 2)
    s2=$(csv_cell "$TV06_CSV" 2 2)
    if [[ "$s1" == "alarm" && "$s2" == "alarm" ]]; then
        pass "TV06 values: status = $s1, $s2"
    else
        fail "TV06 values: status = '$s1', '$s2' (expected alarm, alarm)"
    fi
fi

# TV-10: gradient (temperature difference) → expected: 0.5, 0.0, 34.0, 0.0, -33.0, 0.5
TV10_CSV="$TMPDIR/TV10_gradient.csv"
if [[ -f "$TV10_CSV" ]]; then
    g1=$(csv_cell "$TV10_CSV" 1 2)
    g3=$(csv_cell "$TV10_CSV" 3 2)
    g5=$(csv_cell "$TV10_CSV" 5 2)
    ok=true
    float_eq "$g1" 0.5  || ok=false
    float_eq "$g3" 34.0 || ok=false
    float_eq "$g5" -33.0 || ok=false
    if $ok; then
        pass "TV10 gradient values: g[1]=$g1, g[3]=$g3, g[5]=$g5"
    else
        fail "TV10 gradient values: g[1]=$g1, g[3]=$g3, g[5]=$g5 (expected 0.5, 34.0, -33.0)"
    fi
fi

# TV-11: modulo even → counter values 0, 2, 100, 102
TV11_CSV="$TMPDIR/TV11_modulo.csv"
if [[ -f "$TV11_CSV" ]]; then
    c1=$(csv_cell "$TV11_CSV" 1 2)
    c2=$(csv_cell "$TV11_CSV" 2 2)
    c3=$(csv_cell "$TV11_CSV" 3 2)
    c4=$(csv_cell "$TV11_CSV" 4 2)
    if [[ "$c1" == "0" && "$c2" == "2" && "$c3" == "100" && "$c4" == "102" ]]; then
        pass "TV11 values: counters = $c1, $c2, $c3, $c4"
    else
        fail "TV11 values: counters = $c1, $c2, $c3, $c4 (expected 0, 2, 100, 102)"
    fi
fi

# TV-30: interpolation (X[-1][1] + X[0][1]) / 2.0
#   Expected: 20.75, 21.0, 38.0, 55.0, 38.5, 22.25
TV30_CSV="$TMPDIR/TV30_interp.csv"
if [[ -f "$TV30_CSV" ]]; then
    v1=$(csv_cell "$TV30_CSV" 1 2)
    v3=$(csv_cell "$TV30_CSV" 3 2)
    v6=$(csv_cell "$TV30_CSV" 6 2)
    ok=true
    float_eq "$v1" 20.75 || ok=false
    float_eq "$v3" 38.0  || ok=false
    float_eq "$v6" 22.25 || ok=false
    if $ok; then
        pass "TV30 interpolation: v[1]=$v1, v[3]=$v3, v[6]=$v6"
    else
        fail "TV30 interpolation: v[1]=$v1, v[3]=$v3, v[6]=$v6 (expected 20.75, 38.0, 22.25)"
    fi
fi

# TV-32: 3-point moving average
#   Expected: ≈20.833, ≈32.333, ≈43.667, ≈44.0, ≈33.167
TV32_CSV="$TMPDIR/TV32_mavg3.csv"
if [[ -f "$TV32_CSV" ]]; then
    v1=$(csv_cell "$TV32_CSV" 1 2)
    v2=$(csv_cell "$TV32_CSV" 2 2)
    v5=$(csv_cell "$TV32_CSV" 5 2)
    ok=true
    float_eq "$v1" 20.833 0.1 || ok=false
    float_eq "$v2" 32.333 0.1 || ok=false
    float_eq "$v5" 33.167 0.1 || ok=false
    if $ok; then
        pass "TV32 moving avg: v[1]=$v1, v[2]=$v2, v[5]=$v5"
    else
        fail "TV32 moving avg: v[1]=$v1, v[2]=$v2, v[5]=$v5 (expected ≈20.83, ≈32.33, ≈33.17)"
    fi
fi

# TV-29: precedence — counter % 3 == 0 → counter values 0, 3, 102
TV29_CSV="$TMPDIR/TV29_precedence.csv"
if [[ -f "$TV29_CSV" ]]; then
    c1=$(csv_cell "$TV29_CSV" 1 2)
    c2=$(csv_cell "$TV29_CSV" 2 2)
    c3=$(csv_cell "$TV29_CSV" 3 2)
    if [[ "$c1" == "0" && "$c2" == "3" && "$c3" == "102" ]]; then
        pass "TV29 values: counters = $c1, $c2, $c3"
    else
        fail "TV29 values: counters = $c1, $c2, $c3 (expected 0, 3, 102)"
    fi
fi

# ══════════════════════════════════════════════════════════════════════
# 5. Encoding variants
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Encoding variant round-trips"
echo "═══════════════════════════════════════════════════════════"

for variant in "default" "no-delta" "no-batch" "no-lz4" "flat"; do
    out_bcsv="$TMPDIR/enc_${variant}.bcsv"
    out_csv="$TMPDIR/enc_${variant}.csv"
    extra_args=()
    case "$variant" in
        no-delta) extra_args+=(--no-delta) ;;
        no-batch) extra_args+=(--no-batch) ;;
        no-lz4)   extra_args+=(--no-lz4) ;;
        flat)     extra_args+=(--no-delta --no-lz4 --no-batch) ;;
    esac
    "$BCSVSAMPLER" -c 'X[0][1] > 50.0' "${extra_args[@]}" -f \
        "$CANON_BCSV" "$out_bcsv" 2>/dev/null
    "$BCSV2CSV" "$out_bcsv" "$out_csv" 2>/dev/null
    got=$(csv_rows "$out_csv")
    if [[ "$got" -eq 2 ]]; then
        pass "Encoding '$variant': $got rows"
    else
        fail "Encoding '$variant': got $got rows, expected 2"
    fi
done

# ══════════════════════════════════════════════════════════════════════
# 6. Disassembly smoke test
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Disassembly smoke test"
echo "═══════════════════════════════════════════════════════════"

DISASM=$("$BCSVSAMPLER" --disassemble -c 'X[0][1] > 50.0' -s 'X[0][0], X[0][1]' "$CANON_BCSV" 2>/dev/null)
if echo "$DISASM" | grep -q "HALT_COND" && echo "$DISASM" | grep -q "HALT_SEL"; then
    pass "Disassembly contains HALT_COND and HALT_SEL"
else
    fail "Disassembly missing expected opcodes"
fi

# ══════════════════════════════════════════════════════════════════════
# 7. Verbose / summary smoke test
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Summary output smoke test"
echo "═══════════════════════════════════════════════════════════"

SUMMARY=$("$BCSVSAMPLER" -v -c 'X[0][1] > 50.0' -f "$CANON_BCSV" "$TMPDIR/summary_test.bcsv" 2>&1)
if echo "$SUMMARY" | grep -q "Pass rate" && echo "$SUMMARY" | grep -q "Rows written" && echo "$SUMMARY" | grep -q "Encoding"; then
    pass "Summary output contains expected sections"
else
    fail "Summary output missing expected sections"
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
