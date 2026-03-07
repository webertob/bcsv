# Bug Report: ReaderDirectAccess — Incorrect Results with Delta Row-Codec and Batch File-Codec

**Date:** 2026-03-02  
**Discovered during:** Item 16 — CLI tool integration testing  
**Severity:** High (silent data corruption)  
**Component:** `bcsv::ReaderDirectAccess` (`include/bcsv/reader.h`)

---

## Summary

`ReaderDirectAccess::read(size_t index)` produces incorrect results in two
independent scenarios.  Both are **library-level bugs** in the BCSV core —
not in the CLI tools that exposed them.

| # | Bug | Affected codecs | Symptom |
|---|-----|----------------|---------|
| **LIB-1** | Delta row-codec + random access | `delta` + any file-codec with footer | Silent numeric corruption |
| **LIB-2** | Batch file-codec + random access | any row-codec + `packet_lz4_batch` | `read()` returns `false` for all rows |

---

## LIB-1: Delta Row-Codec Produces Corrupt Data Under Random Access

### Description

When a BCSV file is written with `delta` row encoding, each row stores
the difference from the previous row.  `ReaderDirectAccess::read(index)`
jumps directly to the requested row without first decoding prior rows.
Because the delta decoder has no previous-row state, it decodes against
an implicit zero baseline, producing incorrect numeric values.

String columns are unaffected (they use their own dictionary and are not
delta-encoded).

### Reproduction

```bash
echo "id,value
1,100
2,200
3,300" > /tmp/test.csv

# Write with delta row-codec
csv2bcsv --row-codec delta --file-codec packet_lz4 \
    -f /tmp/test.csv /tmp/delta.bcsv

# Random-access read of last row (index 2)
bcsvTail -n 1 /tmp/delta.bcsv
```

**Actual output** (wrong):
```
"id","value"
0,0
```

**Expected output:**
```
"id","value"
3,300
```

Sequential reads via `Reader::readNext()` produce correct results.

### Affected codec matrix

| Row-codec | File-codec | `bcsvTail -n 1` (last of 3 rows) | Correct? |
|-----------|------------|----------------------------------|----------|
| flat      | packet     | `3,300` | ✅ |
| flat      | packet_lz4 | `3,300` | ✅ |
| zoh       | packet     | `3,300` | ✅ |
| zoh       | packet_lz4 | `3,300` | ✅ |
| **delta** | **packet** | `0,0` | ❌ |
| **delta** | **packet_lz4** | `0,0` | ❌ |

### Root Cause Analysis

Delta decoding requires the previous row's decoded values as a baseline.
`ReaderDirectAccess::read(size_t index)` seeks directly to the packet
containing row `index` and decodes it in isolation.  The deserializer
starts from an all-zeros state, so:

- Row 0 is decoded correctly (its delta baseline is zero by definition).
- Row N (N > 0) is decoded as if it were row 0 — the deltas are applied
  to zeros instead of the actual row N-1 values.

### Possible Fix Strategies

1. **Decode from packet start:** Each packet stores multiple rows.  When
   `read(i)` is called, decode all rows from the start of the packet
   containing row `i` up to and including row `i`.  This restores the
   delta chain within a packet at the cost of O(packet_size) decoding.

2. **Store absolute snapshots:** Write an absolute (non-delta) row at
   the start of each packet.  This would make strategy (1) always
   correct without requiring cross-packet state.

3. **Reject or warn:** If `ReaderDirectAccess` detects delta encoding,
   either throw/return an error, or transparently fall back to
   sequential reading for the requested range.  This is the safest
   short-term mitigation.

---

## LIB-2: Batch File-Codec — `read()` Fails for All Rows

### Description

When a BCSV file uses the `packet_lz4_batch` file codec,
`ReaderDirectAccess::open()` succeeds and `rowCount()` returns the
correct total, but every call to `read(index)` returns `false`.  No data
can be retrieved via random access.

This affects **all row codecs** (flat, zoh, delta) when combined with
`packet_lz4_batch`.

### Reproduction

```bash
echo "id,value
1,100
2,200
3,300" > /tmp/test.csv

# Write with batch file-codec
csv2bcsv --row-codec flat --file-codec packet_lz4_batch \
    -f /tmp/test.csv /tmp/batch.bcsv

# Attempt random-access read
bcsvTail -v -n 2 /tmp/batch.bcsv
```

**Actual output:**
```
Direct-access mode: 3 total rows
"id","value"
Warning: Failed to read row 1
Warning: Failed to read row 2
Successfully displayed 0 rows
```

**Expected output:**
```
"id","value"
2,200
3,300
```

Sequential reads via `Reader::readNext()` produce correct results.

### Affected codec matrix

| Row-codec | File-codec | `read()` succeeds? |
|-----------|------------|--------------------|
| flat | packet_lz4_batch | ❌ all rows fail |
| zoh  | packet_lz4_batch | ❌ all rows fail |
| delta | packet_lz4_batch | ❌ all rows fail |
| any  | packet / packet_lz4 | ✅ |

### Root Cause Hypothesis

The batch codec groups multiple LZ4-compressed packets into a single
batch.  The footer index likely stores per-batch offsets rather than
per-packet offsets, or the decompression logic does not correctly locate
individual rows within a batch.  `open()` reads the footer metadata
correctly (row count is accurate), but seeking within the batch data
fails.

---

## Impact on CLI Tools

Both bugs affect any tool or user code that uses `ReaderDirectAccess`:

- **bcsvTail** — uses direct access for the fast path; falls back to
  sequential only when `open()` fails (not when `read()` fails or
  returns garbage).
- **bcsv2csv** with `--firstRow`/`--lastRow`/`--slice` — uses
  `ReaderDirectAccess` for efficient row slicing.
- **User C++ code** — any direct use of `ReaderDirectAccess::read()`.

### Current Workarounds in CLI Test Suite

The integration test script (`tests/test_cli_tools.sh`) explicitly uses
`--row-codec flat --file-codec packet_lz4` for test files that exercise
direct-access paths.  This avoids both bugs but does not test the
default codec combination (`delta + packet_lz4_batch`).

---

## Recommended Priority

| Bug | Priority | Rationale |
|-----|----------|-----------|
| LIB-1 | **Critical** | Silent data corruption — user gets wrong values with no error indication. The default row-codec is `delta`, so most files written with default settings are affected. |
| LIB-2 | **High** | Complete failure (no data returned) — but at least it's visible via `read()` returning `false`. The default file-codec is `packet_lz4_batch`, so most default files are affected. |

Combined: **the default codec combination (`delta + packet_lz4_batch`)
fails in both ways simultaneously**, making `ReaderDirectAccess`
effectively broken for files created with default settings.

---

## Resolution

**Status:** Fixed  
**Date:** 2026-03-02

### LIB-1 Fix: Forward-Watermark Sequential Decode

**Files changed:** `include/bcsv/reader.h`, `include/bcsv/reader.hpp`

The fix adds a watermark-based sequential decode strategy to
`deserializeCachedRow()`:

- **Flat codec (stateless):** Unchanged — direct single-row deserialize,
  O(1) per read.
- **Stateful codecs (ZoH, Delta):** The reader tracks
  `cached_decode_pos_`, the last decoded row position within the cached
  packet. Three cases:
  - **Same row re-read** (`rowInPacket == cached_decode_pos_`): Return
    cached `row_` unchanged.
  - **Forward read** (`rowInPacket > cached_decode_pos_`): Continue
    sequential decode from watermark+1 to target.
  - **Backward read** (`rowInPacket < cached_decode_pos_`): Reset row
    codec, decode sequentially from packet start to target.

The watermark is reset to `SIZE_MAX` on `close()` and whenever a new
packet is loaded via `loadPacket()`.

### LIB-2 Fix: `seekToPacket()` for Batch File-Codec

**File changed:** `include/bcsv/codec_file/file_codec_packet_lz4_batch001.h`

Added `seekToPacket(std::istream&, std::streamoff)` method to
`FileCodecPacketLZ4Batch001`. The method:

1. Stops the background decompression thread (`shutdownBgThread()`).
2. Seeks the input stream to the target packet offset.
3. Synchronously reads and decompresses the packet via the existing
   `readAndDecompressPacket()` into the current read buffer.
4. Resets the read cursor to 0.

`FileCodecDispatch` automatically detects `seekToPacket` via SFINAE and
wires it into `ReaderDirectAccess::loadPacket()`.

### Test Coverage

29 new tests added to `tests/direct_access_test.cpp` (53 total):

| Category | Tests | Coverage |
|----------|-------|----------|
| Codec matrix | 9 | flat/zoh/delta × packet/packet_lz4/packet_lz4_batch |
| Sequential parity | 3 | Full row-by-row comparison: readNext() vs read(i) |
| Central 1/3 slice | 3 | Rows [N/3..2N/3) with each row codec |
| Random access | 3 | 1000 random indices per row codec |
| Intra-packet | 3 | 5th and 16th row within each packet |
| Edge: first/last row | 2 | Packet boundary rows with delta codec |
| Edge: backward read | 1 | Forward then backward within packet |
| Edge: re-read | 1 | Same row read multiple times |
| Edge: boundary cross | 1 | Last→first row across adjacent packets |
| Edge: jump pattern | 1 | Alternating near-start/near-end |
| Edge: default combo | 1 | Delta + batch LZ4, every row verified |
| Perf: central slice | 1 | DA central 30% vs full sequential |

### Verified Codec Matrix (post-fix)

| Row-codec | File-codec | Direct access | Random access | Sequential parity |
|-----------|------------|:------------:|:-------------:|:-----------------:|
| flat      | packet            | ✅ | ✅ | ✅ |
| flat      | packet_lz4        | ✅ | ✅ | ✅ |
| flat      | packet_lz4_batch  | ✅ | ✅ | ✅ |
| zoh       | packet            | ✅ | ✅ | ✅ |
| zoh       | packet_lz4        | ✅ | ✅ | ✅ |
| zoh       | packet_lz4_batch  | ✅ | ✅ | ✅ |
| delta     | packet            | ✅ | ✅ | ✅ |
| delta     | packet_lz4        | ✅ | ✅ | ✅ |
| delta     | packet_lz4_batch  | ✅ | ✅ | ✅ |
