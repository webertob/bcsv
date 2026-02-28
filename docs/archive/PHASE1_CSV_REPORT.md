# Phase 1: CSV Reader/Writer Quality Report

## 1. Test Coverage

### Baseline: 22 existing tests — all PASS

### New tests added: 10 → total 32 — all PASS

| Test | What it covers |
|------|---------------|
| `NanInf_RoundTrip` | NaN, +Inf, -Inf float/double round-trip |
| `UTF8_BOM` | BOM stripping on CsvReader.open() |
| `MultiLineQuotedField_External` | Quoted fields containing embedded newlines |
| `ScientificNotation_CommaDecimal` | Scientific notation (`1.23e4`) with `,` decimal separator |
| `WriteExternalRow` | `write(row)` API path (vs `writeRow()`) |
| `LargeString` | 10 000-char string with embedded `"`, `,`, `\n` |
| `PathologicalQuoting` | Double-quote escaping, consecutive quotes, quote-in-middle |
| `SubnormalFloat_RoundTrip` | Subnormal float values round-trip correctly |
| `SingleColumnCSV` | Single-column layout (no delimiters in output) |
| `CloseAndVerify` | close() flushes all data; re-read verifies integrity |

### Discovered behavior: `close()` resets `rowCount()` to 0 (by design).

---

## 2. CLI Round-Trip Validation

### csv2bcsv → bcsv2csv round-trip

| Test | Status | Notes |
|------|--------|-------|
| Basic CSV (5 rows, 4 cols) | **PASS** | Semantically identical; strings always quoted in output |
| Challenging (embedded commas, quotes, empty strings) | **PASS** | All special chars preserved |
| `--no-zoh` mode | **PASS** | Identical output to ZoH mode |
| Semicolon delimiter | **PASS** | Delimiter preserved through round-trip |
| `--slice 1:3` | **PASS** | Correctly selects rows |

### Expected formatting differences (not bugs):
- CsvWriter always quotes string columns (RFC 4180 compliant)
- Float values use shortest representation (`1.0` → `1`, `1e10` → `1e+10`)
- Type re-detection on round-trip may change types (e.g., `float` → `uint8` when values happen to be integers)

### bcsvHead / bcsvTail formatting inconsistency:
- These tools use printf-style `%f` (6 decimal places) for floats
- `bcsv2csv` (CsvWriter) uses `std::to_chars` (shortest representation)
- Both selectively vs always quote strings differently

---

## 3. Dual CSV Parser Audit (csv2bcsv.cpp)

**Architecture:** csv2bcsv uses two separate CSV parsers:
- **Pass 1 (type detection):** Hand-rolled `parseCSVLine()` + `std::ifstream` + `std::getline`
- **Pass 2 (conversion):** `bcsv::CsvReader<Layout>` (library API)

### Inconsistencies found:

| Feature | Pass 1 (`parseCSVLine`) | Pass 2 (`CsvReader`) | Risk |
|---------|------------------------|---------------------|------|
| Multi-line quoted fields | **NOT supported** | Supported | **HIGH** — type detection breaks on multi-line fields |
| UTF-8 BOM | **NOT handled** | Strips BOM | **MEDIUM** — BOM bytes in column name from Pass 1 |
| CRLF handling | Manual `\r` strip | Built-in | Low |
| Sample limit | 1000 rows | Full file | Low — type may be wrong for late-appearing data |

### Recommendation:
Refactor csv2bcsv to use CsvReader for both passes, or at minimum extract the type-detection logic into a shared function that uses CsvReader's parser.

---

## 4. Benchmark Results

### Codec Comparison (14 profiles × 10,000 rows × 5 iterations)

| Codec | Write (Krow/s) | Read (Krow/s) | Size Ratio |
|-------|----------------|---------------|------------|
| **CSV** | **187** | **401** | **1.000** |
| PktRaw | 417 | 808 | 0.557 |
| PktLZ4 | 406 | 780 | 0.467 |
| BatchLZ4 | 714 | 897 | 0.453 |
| PktRaw+ZoH | 1206 | 2409 | 0.034 |
| BatchLZ4+ZoH | 2072 | 2827 | 0.021 |

CSV write is **11× slower** than BatchLZ4+ZoH write, and **7× slower** on read. This is expected — text serialization has inherent overhead.

### External CSV Parser Comparison (14 profiles × 500,000 rows)

BCSV CsvReader is ~30% slower than `vincentlaucsb/csv-parser` (which uses memory-mapped I/O). String-heavy profiles show largest gap (2.6× slower). This is acceptable for a secondary I/O path.

---

## 5. Summary

| Category | Status |
|----------|--------|
| Test coverage | ✅ 32/32 pass (10 new tests covering edge cases) |
| CLI round-trip | ✅ All modes verified (ZoH, no-ZoH, semicolon, slice) |
| Dual parser | ⚠️ 2 inconsistencies found (multi-line, BOM) |
| Benchmarks | ✅ CSV performance baselined across 14 profiles |
| Data integrity | ✅ Semantic equivalence confirmed in all round-trips |
