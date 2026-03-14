# BCSV — Implementation Plan (Final)

**Author:** Claude Opus 4.6  
**Date:** March 14, 2026 (Revised after peer feedback validation)  
**Source:** Consolidated findings from 4 independent reviews (Opus 4.6, Sonnet 4.6, Codex 5.3, Gemini 3.1), cross-verified against source code  
**Approach:** 7 implementation cycles, each with detailed planning, implementation, tests, and commit strategy  
**Revision note:** This plan has been updated to remove 3 false findings (unknown codec dispatch, non-owning layout disposal, CI absence) and add 5 missed findings (Writer::close() silent failure, sampler string pool overflow, missing P/Invoke declarations, Arrow 2GB limit, benchmark duplicate label). All remaining items have been verified against actual source code.

---

## Guiding Principles

1. **Fix correctness bugs first, then safety, then documentation, then features.**
2. **Each cycle is independently committable** — no cycle depends on another being incomplete.
3. **Every code change has a test.** No "fix and hope."
4. **No breaking API changes** without explicit deprecation.
5. **Use the project's existing conventions** (naming, file organization, error handling patterns).
6. **Only include verified findings.** Every item below has been checked against the actual implementation. Items that were retracted during review validation are explicitly excluded.

---

## Retracted Items (Not Included in Plan)

The following items appeared in earlier drafts but were **verified as false or already handled**:

| Retracted Item | Reason | Evidence |
|---------------|--------|----------|
| Unknown codec dispatch defaults to Flat001 | `setup()` throws `std::logic_error` on unknown IDs; `selectCodec()` Flat001 default is correct behavior | row_codec_dispatch.h switch default case |
| BcsvLayout non-owning Dispose crash | `_ownsHandle` pattern already implemented; Dispose() checks `_ownsHandle && Handle != 0` | BcsvLayout.cs L15, L32-37 |
| g_columnar_state memory leak | Already fixed: `erase()` called in both `bcsv_reader_destroy()` and `bcsv_reader_close()` | bcsv_c_api.cpp L318, L324 |
| Add Python tests to CI | Already exists in `build-and-publish.yml` L186-190 | .github/workflows/build-and-publish.yml |
| Add C# tests to CI | Already exists in `csharp-nuget.yml` L224-234 | .github/workflows/csharp-nuget.yml |
| Bitset 64→65 heap-transition leak | Code is exception-safe: new allocates → copy → delete old → assign | bitset.hpp resizeStorage() L75-120 |
| CSV benchmark uses visitConst() | Benchmark uses real `CsvWriter`/`CsvReader` with file I/O | bench_macro_datasets.cpp L714-776 |

---

## Cycle Overview

| Cycle | Theme | Severity | Files Changed | Status |
|-------|-------|----------|---------------|--------|
| 1 | Critical Documentation Fixes | CRITICAL | 4 files | ✅ Done (0538feb) |
| 2 | Core C++ Correctness | CRITICAL+HIGH | 5 files + tests | ✅ Done |
| 3 | C# / Unity Safety | CRITICAL+HIGH | 8 files + tests | |
| 4 | Python Hardening | HIGH | 4 files + tests | |
| 5 | Test Coverage Gaps | HIGH | 8 test files | |
| 6 | Documentation Consolidation | MEDIUM | 10 files | |
| 7 | Benchmark, Build & Examples | MEDIUM | 10 files | |

---

## Cycle 1: Critical Documentation Fixes

**Theme:** Fix documentation that actively misleads users or sends them to broken links.  
**Severity:** CRITICAL  
**Reviewers who flagged:** Sonnet (C4, C5), Codex (Cross-1, Cross-2), Gemini (Domain 1)  
**Confidence:** All items verified against source code ✅

### Planning

These are pure documentation fixes with zero code risk. Do them first — every day these persist, users hit broken links or make wrong deployment decisions.

### Implementation

#### 1.1 Fix placeholder URLs in docs/API_OVERVIEW.md

**File:** `docs/API_OVERVIEW.md`  
**Change:** Replace `https://github.com/your-repo/bcsv/issues` with `https://github.com/webertob/bcsv/issues`  
**Search pattern:** `your-repo` — replace all instances.

#### 1.2 Fix wrong GitHub link in python/README.md

**File:** `python/README.md`  
**Change:** Replace `https://github.com/bcsv/bcsv` with `https://github.com/webertob/bcsv`  
**Search pattern:** `github.com/bcsv/bcsv` — replace all instances.

#### 1.3 Fix forward-compatibility documentation in VERSIONING.md

**File:** `VERSIONING.md`  
**Current (line ~165):**
```
- **Minor version newer**: File can be read (backward compatibility)
- **Minor version older**: File can be read (forward compatibility within major)
```
**Corrected:**
```
- **Minor version older in file**: File can be read (backward compatibility — reader is newer than file)
- **Minor version newer in file**: File is REJECTED (reader cannot guarantee correct interpretation of newer features)
```
**Rationale:** Code in reader.hpp line 167 rejects `versionMinor() > BCSV_FORMAT_VERSION_MINOR`. Documentation must match code. If the intent is to change the code to be more permissive, that's a separate design decision — document it here but implement it as a code change with tests.

#### 1.4 Add version relationship clarification to README.md

**File:** `README.md`  
**Change:** Add a note near the version badge:
```
> **Versioning:** Library version (e.g. v1.3.0) tracks API/code changes.
> File format version (e.g. v1.4.0) tracks wire format changes.
> Package versions (PyPI, NuGet) may advance independently.
> See [VERSIONING.md](VERSIONING.md) for details.
```

### Tests

No code changes — no new tests needed. Validate links with:
```bash
grep -rn "your-repo\|bcsv/bcsv" docs/ python/README.md
# Should return zero matches after fix
```

### Commit

```
docs: fix broken URLs, correct forward-compat documentation, clarify versioning

- Replace placeholder github.com/your-repo/bcsv with webertob/bcsv
  in docs/API_OVERVIEW.md
- Fix wrong github link in python/README.md (bcsv/bcsv → webertob/bcsv)
- Correct VERSIONING.md: code rejects newer minor versions, docs must match
- Add version relationship note to README.md

Verified: reader.hpp L167 rejects versionMinor() > BCSV_FORMAT_VERSION_MINOR
```

---

## Cycle 2: Core C++ Correctness

**Theme:** Fix verified bugs in the C++ core that affect correctness or safety.  
**Severity:** CRITICAL (string truncation) + HIGH (sampler, ZoH001, Writer::close, string pool)  
**Reviewers who flagged:** Opus (C4), Sonnet (M7, M9, M10), self (2nd pass: Writer::close)  
**Confidence:** All items verified against source code with exact line numbers ✅

### Planning

Five independent code fixes. Each is small and localized. Order by impact: string truncation (widespread data integrity), Writer::close() (silent data loss), sampler underflow (UB), ZoH001 reset (data correctness), sampler string pool (silent overflow).

### Implementation

#### 2.1 Fix silent string truncation → throw std::length_error

**Files:** `include/bcsv/row.hpp`  
**Change at 4 locations** (lines ~384, ~539, ~622, ~1003):

Replace:
```cpp
if (str.size() > MAX_STRING_LENGTH) {
    str.resize(MAX_STRING_LENGTH);
}
```
With:
```cpp
if (str.size() > MAX_STRING_LENGTH) {
    throw std::length_error("String exceeds MAX_STRING_LENGTH (" 
        + std::to_string(MAX_STRING_LENGTH) + " bytes): got " 
        + std::to_string(str.size()) + " bytes");
}
```

**Rationale:** Silent data loss is worse than an exception. Users who need truncation can truncate before calling set(). This is a behavior change — document in release notes.

**Risk mitigation:** Grep all call sites of `row.set()` with string type before committing. The CSV reader and CLI tools should catch this exception gracefully.

#### 2.2 Fix Writer::close() silent data loss

**File:** `include/bcsv/writer.h` and `include/bcsv/writer.hpp`  
**Current:** `close()` returns `void` and doesn't check stream state after footer write (writer.hpp L55-70).  
**Problem:** If `file_codec_.finalize()` fails to write the footer (disk full, I/O error), the error is silently swallowed. Stream `failbit`/`badbit` is set but never checked.

**Option A (preferred — non-breaking):** Add `bool close()` returning success. Keep the current `void close()` for backward compat via overload or rename.  
**Option B (simpler):** Check `stream_.good()` after `file_codec_.finalize()` and set `err_msg_` if failed. Users can call `getErrorMsg()` after close.

```cpp
void Writer<LayoutType, CodecType>::close() {
    if (!stream_.is_open()) return;
    if (file_codec_.isSetup()) {
        file_codec_.finalize(stream_, row_cnt_);
    }
    // NEW: check for I/O errors during footer write
    if (!stream_.good()) {
        err_msg_ = "Error: I/O failure during file close/footer write";
    }
    stream_.close();
    // ... existing cleanup ...
}
```

#### 2.3 Fix sampler pop() underflow

**File:** `include/bcsv/sampler/sampler_vm.h` (or .hpp)  
**Change:**
```cpp
// Current:
SamplerValue pop() { return stack_[--sp_]; }

// Fixed:
SamplerValue pop() {
    if (sp_ == 0) {
        throw std::runtime_error("Sampler VM: stack underflow");
    }
    return stack_[--sp_];
}
```

#### 2.4 Fix ZoH001 reset() to clear reference state

**File:** `include/bcsv/codec_row/row_codec_zoh001.hpp`  
**Change in `reset()` method:**
```cpp
// Current:
void RowCodecZoH001<LayoutType>::reset() noexcept {
    first_row_in_packet_ = true;
}

// Fixed — clear comparison state so next packet starts fresh:
void RowCodecZoH001<LayoutType>::reset() noexcept {
    first_row_in_packet_ = true;
    data_.clear();
    strg_.clear();
}
```

#### 2.5 Fix sampler string pool uint16_t overflow

**File:** `include/bcsv/sampler/sampler_compiler.h`  
**Change at line ~348:**
```cpp
// Current:
uint16_t idx = static_cast<uint16_t>(bc_.string_pool.size());

// Fixed:
if (bc_.string_pool.size() >= 65535) {
    throw std::runtime_error("Sampler compiler: string pool exceeds maximum (65535 entries)");
}
uint16_t idx = static_cast<uint16_t>(bc_.string_pool.size());
```

Also apply same guard in the VM interning path (`sampler_vm.hpp` ~L491).

### Tests

#### 2.1 Tests: String length boundary
```cpp
TEST(RowStringBoundary, ExactMaxLengthAccepted) {
    // String of exactly MAX_STRING_LENGTH — should succeed
}
TEST(RowStringBoundary, OverMaxLengthThrows) {
    // String of MAX_STRING_LENGTH + 1 — should throw std::length_error
}
```

#### 2.2 Tests: Writer close error detection
```cpp
TEST(WriterClose, DetectsWriteFailure) {
    // Write to a file on a full filesystem (or mock stream with badbit)
    // After close(), err_msg_ should be non-empty
}
```

#### 2.3 Tests: Sampler underflow
```cpp
TEST(SamplerVM, PopUnderflowThrows) {
    // Attempt pop() on empty stack — should throw
}
```

#### 2.4 Tests: ZoH001 reset state
```cpp
TEST(ZoH001Reset, ResetClearsReferenceState) {
    // Write rows, reset(), verify data_/strg_ are cleared
    // Write new rows — should encode correctly against fresh state
}
```

#### 2.5 Tests: String pool overflow
```cpp
TEST(SamplerCompiler, StringPoolOverflowThrows) {
    // Compile an expression with >65535 string literals — should throw
}
```

### Commit

```
fix: correctness bugs in string truncation, Writer::close, sampler, ZoH001

- row.hpp: throw std::length_error on string > MAX_STRING_LENGTH instead
  of silent truncation (4 locations)
- writer.hpp: check stream_.good() after finalize() to detect I/O errors
- sampler_vm.h: add stack underflow guard to pop()
- row_codec_zoh001.hpp: clear data_/strg_ in reset()
- sampler_compiler.h: guard against uint16_t overflow in string pool

Includes boundary tests for all five fixes.
```

---

## Cycle 3: C# / Unity Safety

**Theme:** Fix resource leaks and data corruption in C# and Unity bindings.  
**Severity:** CRITICAL (finalizers) + HIGH (UTF-8, IL2CPP, P/Invoke gaps)  
**Reviewers who flagged:** Opus (#1-3), Sonnet (M16-M18), Codex (Domain 6)  
**Confidence:** All items verified ✅

### Planning

Four fix areas. Note: non-owning layout disposal (formerly item 3.4) has been **removed** — the `_ownsHandle` pattern is already correctly implemented. BcsvRow is a `readonly struct` without native ownership — it does NOT need IDisposable.

### Implementation

#### 3.1 Add finalizers to all IDisposable C# classes

**Files:** 6 classes in `csharp/src/Bcsv/`:
- BcsvLayout.cs
- BcsvReader.cs
- BcsvWriter.cs
- BcsvCsvReader.cs
- BcsvCsvWriter.cs
- BcsvSampler.cs

**NOT included:** BcsvRow (readonly struct, non-owning — adding IDisposable would be harmful).

**Pattern for each class:**
```csharp
~BcsvLayout()
{
    Dispose(false);
}

public void Dispose()
{
    Dispose(true);
    GC.SuppressFinalize(this);
}

// Ensure Dispose(bool) exists and correctly handles unmanaged cleanup
```

**Pre-check:** Verify each class has `Dispose(bool disposing)`. If not, add the standard pattern.

#### 3.2 Fix Unity PtrToStringAnsi → PtrToStringUTF8

**Files:** All 6 confirmed locations in `unity/Scripts/`:
- BcsvLayout.cs L108, L229
- BcsvReader.cs L233
- BcsvWriter.cs L287
- BcsvRow.cs L190
- BcsvNative.cs L354, L372

**Change:** Replace all `Marshal.PtrToStringAnsi(ptr)` with `Marshal.PtrToStringUTF8(ptr)`.

#### 3.3 Add [Preserve] attributes to Unity P/Invoke methods

**File:** `unity/Scripts/BcsvNative.cs`  
**Change:** Add `[Preserve]` attribute above every `[DllImport]` method.  
**Also add:** `using UnityEngine.Scripting;` at top of file.

#### 3.4 Add missing P/Invoke declarations

**File:** `csharp/src/Bcsv/NativeMethods.cs`  
**Add 10 missing array function P/Invoke declarations:**
- `bcsv_row_get_bool_array` / `bcsv_row_set_bool_array`
- `bcsv_row_get_uint8_array` / `bcsv_row_set_uint8_array`
- `bcsv_row_get_uint16_array` / `bcsv_row_set_uint16_array`
- `bcsv_row_get_int8_array` / `bcsv_row_set_int8_array`
- `bcsv_row_get_int16_array` / `bcsv_row_set_int16_array`

Also add corresponding public methods in `BcsvRow.cs` or the appropriate accessor class.

### Tests

#### 3.1 Test: Finalizer prevents leak
```csharp
[Fact]
public void Layout_Finalizer_Prevents_Leak()
{
    WeakReference weakRef;
    {
        var layout = new BcsvLayout();
        layout.AddColumn("test", BcsvColumnType.Int32);
        weakRef = new WeakReference(layout);
    }
    GC.Collect();
    GC.WaitForPendingFinalizers();
    Assert.False(weakRef.IsAlive);
}
```

#### 3.4 Test: Missing P/Invoke type accessors
```csharp
[Fact]
public void UInt8_Array_RoundTrip()
{
    // Write uint8 array column, read back, verify values match
}
// Similar tests for bool, uint16, int8, int16 arrays
```

### Commit

```
fix(csharp): add finalizers, fix UTF-8 marshaling, add IL2CPP safety, complete P/Invoke

- Add ~Destructor() finalizer to all 6 IDisposable classes
- Add GC.SuppressFinalize to Dispose() methods
- Unity: replace PtrToStringAnsi with PtrToStringUTF8 (6 locations)
- Unity: add [Preserve] attributes to all P/Invoke methods
- Add 10 missing P/Invoke array declarations (bool, uint8, uint16, int8, int16)

Note: BcsvLayout already has correct _ownsHandle disposal (no change needed).
Note: BcsvRow is a readonly struct — correctly does NOT implement IDisposable.
```

---

## Cycle 4: Python Hardening

**Theme:** Improve Python binding safety and developer experience.  
**Severity:** HIGH  
**Reviewers who flagged:** Opus (#7, 6.2.1-6.2.5), Sonnet (C9, C10, M14-M15)  
**Confidence:** All items verified ✅

### Planning

Five items: type stubs (highest impact), NaN handling policy, Arrow 2GB documentation, pathlib support, __repr__ methods.

### Implementation

#### 4.1 Generate .pyi type stubs

**Method:** Use `nanobind.stubgen` to generate initial stubs, then manually review.

```bash
cd python
python -m nanobind.stubgen pybcsv._pybcsv_impl -o pybcsv/_pybcsv_impl.pyi
```

Create `pybcsv/__init__.pyi` with re-exported symbols. Ensure `py.typed` marker remains.

#### 4.2 Improve NaN handling in pandas integration

**File:** `python/pybcsv/pandas_utils.py`  
**Change:** Add a `strict` parameter:
```python
def write_dataframe(writer, df, *, strict=False):
    if strict and df.isnull().any().any():
        cols = df.columns[df.isnull().any()].tolist()
        raise ValueError(f"NaN/None values in columns: {cols}")
```

#### 4.3 Document Arrow utf8() 2GB string limit

**File:** `python/pybcsv/bindings.cpp` (add comment) and `python/README.md` (add docs)

The Arrow integration uses format string `"u"` (utf8 with int32 offsets) at bindings.cpp L378, which limits total string data per batch to ~2GB. Options:
- **Document the limitation** in README and docstrings (minimal change)
- **Switch to `"U"` (large_utf8)** — uses int64 offsets, no 2GB limit, but slightly more memory

Recommendation: Document now, switch to large_utf8 in a future release if users hit the limit.

#### 4.4 Add pathlib.Path support

Handle in Python wrapper code in `__init__.py` — convert `os.PathLike` to `str` before passing to the C++ binding.

#### 4.5 Add __repr__ methods

Add `__repr__` for Writer, Reader, ReaderDirectAccess in `bindings.cpp`.

### Tests

```python
def test_pyi_stubs_exist():
    assert (importlib.resources.files("pybcsv") / "__init__.pyi").is_file()

def test_strict_nan_raises():
    df = pd.DataFrame({"x": [1.0, float('nan')]})
    with pytest.raises(ValueError):
        pybcsv.write_dataframe(writer, df, strict=True)

def test_pathlib_accepted():
    from pathlib import Path
    assert writer.open(Path("/tmp/test.bcsv"))
```

### Commit

```
feat(python): add type stubs, strict NaN mode, Arrow docs, pathlib, __repr__

- Generate .pyi type stubs via nanobind.stubgen
- pandas_utils: add strict=False parameter to write_dataframe()
- Document Arrow utf8() 2GB string column limit
- Accept pathlib.Path in all file path parameters
- Add __repr__ to Writer, Reader, ReaderDirectAccess
```

---

## Cycle 5: Test Coverage Gaps

**Theme:** Fill verified test blind spots.  
**Severity:** HIGH  
**Reviewers who flagged:** Opus (#12-14), Sonnet (C8, M19-M22), Codex (Domain 7)  
**Confidence:** All gaps verified ✅

### Planning

**Important context:** Crash resilience tests DO exist (15 tests across 4 files — verified), contrary to Sonnet's "zero" claim. This cycle focuses on **genuine** gaps: wire format golden files, special float handling, boundary tests, and version compatibility tests.

### Implementation

All changes are in `tests/`. No production code changes.

#### 5.1 Forward-compatibility rejection test

```cpp
TEST(VersionCompatibility, RejectsNewerMinorVersion) {
    // Write valid file, patch minor version to current + 1, verify rejection
}
TEST(VersionCompatibility, AcceptsOlderMinorVersion) {
    // Write valid file, patch minor version to current - 1, verify acceptance
}
```

#### 5.2 Architecture boundary tests

```cpp
// MAX_COLUMNS boundary
TEST(LayoutBoundary, MaxColumnsAccepted) { ... }
TEST(LayoutBoundary, MaxColumnsPlusOneThrows) { ... }

// MAX_STRING_LENGTH boundary (validates Cycle 2 fix)
TEST(RowStringBoundary, ExactMaxLengthAccepted) { ... }
TEST(RowStringBoundary, OverMaxLengthThrows) { ... }

// Bitset 64→65 transition (code IS exception-safe, but functional correctness untested)
TEST(BitsetBoundary, Transition64To65Columns) {
    // 64 bool columns (inline) → add 65th (heap) → write/read → all values correct
}
```

#### 5.3 Unicode / UTF-8 tests

```cpp
TEST(UnicodeRoundTrip, ColumnNamesUTF8) {
    // Chinese, Nordic, emoji column names — write, read, verify
}
TEST(UnicodeRoundTrip, StringValuesUTF8) {
    // Multi-byte UTF-8 string values — byte-exact match
}
```

#### 5.4 Delta002 special float tests

```cpp
TEST(Delta002SpecialFloats, NaNRoundTrip) { ... }      // isnan check
TEST(Delta002SpecialFloats, InfinityRoundTrip) { ... }  // +Inf, -Inf
TEST(Delta002SpecialFloats, SubnormalRoundTrip) { ... } // denormalized
```

#### 5.5 Golden-file wire format test

```cpp
TEST(WireFormat, GoldenFileFlat001_KnownLayout) {
    // Write 3 rows with known values using Flat001
    // Compare output bytes against hardcoded expected byte sequence
    // Catches encoding regressions invisible to round-trip tests
}
```

Store expected bytes as `const uint8_t[]` with structure comments.

#### 5.6 VLE malformed encoding tests

```cpp
TEST(VLEMalformed, CorruptLengthBytesDoNotCrash) { ... }
TEST(VLEMalformed, MaxVLELengthHandled) { ... }
```

#### 5.7 Crash resilience expansion

Existing crash tests are adequate for basic coverage. Add targeted expansion:
```cpp
TEST(CrashResilience, TruncatedMidPacket_RecoversPriorRows) {
    // Truncate at specific byte offset mid-packet
    // Verify rows from complete packets are readable
}
```

### Tests

```bash
./build/ninja-debug/bin/bcsv_gtest --gtest_filter="*Boundary*:*Unicode*:*VLE*:*Delta002Special*:*CrashResilience*:*WireFormat*:*VersionCompat*"
```

### Commit

```
test: fill coverage gaps — boundaries, Unicode, special floats, wire format

- Add version compatibility tests (newer/older minor version)
- Add architecture boundary tests (MAX_COLUMNS, MAX_STRING_LENGTH, bitset 64→65)
- Add UTF-8/Unicode round-trip tests
- Add Delta002 NaN/Inf/subnormal round-trip tests
- Add golden-file wire format test for Flat001
- Add VLE malformed encoding tests
- Expand crash resilience with mid-packet truncation test

Note: 15 crash-resilience tests already exist across 4 files.
```

---

## Cycle 6: Documentation Consolidation

**Theme:** Reduce duplication, fill governance gaps, update stale content.  
**Severity:** MEDIUM  
**Reviewers who flagged:** Opus (#15-17), Sonnet (M2-M4, M18), Codex (Cross-2/3)

### Implementation

#### 6.1 Deduplicate AGENTS.md / copilot-instructions.md

Keep `.github/copilot-instructions.md` as the single source. Make AGENTS.md a redirect:
```markdown
# BCSV Agent Instructions
See [.github/copilot-instructions.md](.github/copilot-instructions.md) for all agent instructions.
```

#### 6.2 Create CONTRIBUTING.md

Build steps, PR checklist, coding conventions reference (point to SKILLS.md).

#### 6.3 Create SECURITY.md

Vulnerability disclosure process, supported versions table.

#### 6.4 Create CHANGELOG.md

Start from current version, populated via git log.

#### 6.5 Update docs/API_OVERVIEW.md feature matrix

Update to reflect actual implementation state. Key corrections:
- Python: sampler ✅, CSV ✅, Polars ✅ (not ❌)
- C#: sampler ✅, CSV ✅, random access ✅, typed accessors ✅ (not "stubs")

#### 6.6 Expand csharp/README.md

Current ~60 lines does not reflect the 15-file, sampler+CSV+typed-accessor library. Expand to document: all features, NuGet installation, version compatibility.

#### 6.7 Fix OWNERSHIP_SEMANTICS.md phantom types

Remove references to `BcsvRowRef`/`BcsvRowRefConst` (don't exist). Document the actual ownership model: `_ownsHandle` pattern in BcsvLayout, BcsvRow as non-owning readonly struct.

#### 6.8 Document Polars integration in python/README.md

Add section with code example. This is a working feature that's currently invisible.

#### 6.9 Add C# and Unity as separate README entries

```markdown
- **C# (.NET 8/10):** [csharp/README.md](csharp/README.md) — NuGet package, full P/Invoke bindings
- **Unity:** [unity/README.md](unity/README.md) — Unity-specific integration (experimental)
```

### Commit

```
docs: consolidate documentation, add governance files, update stale content

- Deduplicate AGENTS.md → redirect to copilot-instructions.md
- Add CONTRIBUTING.md, SECURITY.md, CHANGELOG.md
- Update API_OVERVIEW.md feature matrix to match actual implementation
- Expand csharp/README.md to reflect full library capabilities
- Fix OWNERSHIP_SEMANTICS.md phantom type references
- Document Polars integration in python/README.md
```

---

## Cycle 7: Benchmark, Build & Examples

**Theme:** Fix benchmark bugs, add build presets, fill example gaps.  
**Severity:** MEDIUM  
**Reviewers who flagged:** Opus (#8-11), Codex (Domain 4/8), Sonnet (M11-M13, M24)

### Planning

Three areas merged into one cycle: benchmark fixes, build system improvements, and example gaps. CI already exists — this cycle focuses on build presets and quality improvements.

### Implementation

#### 7.1 Fix benchmark duplicate static-mode labels

**File:** `benchmark/src/bench_macro_datasets.cpp`  
**Problem:** Lines 1263-1276: `benchmarkBCSVStatic()` and `benchmarkBCSVStaticTracked()` both pass identical label `"BCSV Static"` and suffix `"_static"`. Copy-paste bug.  
**Fix:** Change tracked variant to `"BCSV Static Tracked"` / `"_static_tracked"`.

#### 7.2 Add ASAN/UBSan CMake preset

**File:** `CMakePresets.json`  
```json
{
    "name": "ninja-asan",
    "displayName": "Ninja Debug + ASAN/UBSan",
    "inherits": "ninja-debug",
    "cacheVariables": {
        "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
        "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"
    }
}
```

#### 7.3 Add code coverage preset

**File:** `CMakePresets.json` and `CMakeLists.txt`  
Add `ninja-coverage` preset with `--coverage` flags and lcov custom target.

#### 7.4 Add Delta002 codec example

**File:** `examples/example_delta.cpp`  
Demonstrate time-series with slowly varying data, Delta002 codec, file size comparison.

#### 7.5 Add ReaderDirectAccess example

**File:** `examples/example_direct_access.cpp`  
Demonstrate O(1) random access: single row by index, slicing, backward iteration.

#### 7.6 Add "Quick Start" minimal example

**File:** `examples/quickstart.cpp` (≤30 lines)  
Write-then-read lifecycle with zero distractions.

#### 7.7 Add CTest integration for examples

**File:** `examples/CMakeLists.txt`  
```cmake
add_test(NAME example_static COMMAND example_static)
add_test(NAME example_delta COMMAND example_delta)
# ... all examples
```

#### 7.8 Add error handling example

Demonstrate `getErrorMsg()` and error recovery patterns.

### Tests

```bash
# Benchmark label fix — verify via run
./build/ninja-release/bin/bench_macro_datasets --profile=mixed_generic --rows=100 --summary=compact | grep -i "static"

# ASAN build
cmake --preset ninja-asan && cmake --build --preset ninja-asan-build -j$(nproc)
./build/ninja-asan/bin/bcsv_gtest

# Examples as CTest
ctest --test-dir build/ninja-debug -R "example_"
```

### Commit

```
feat: fix benchmark labels, add ASAN/coverage presets, add examples

- Fix duplicate "BCSV Static" benchmark label (copy-paste bug)
- Add ninja-asan CMake preset (ASAN + UBSan)
- Add ninja-coverage CMake preset with lcov target
- Add example_delta.cpp, example_direct_access.cpp, quickstart.cpp
- Register all examples as CTest tests
- Add error handling demonstration example
```

---

## Dependency Graph

```
Cycle 1 (Docs) ─────────────────→ Cycle 6 (Docs Consolidation)
    ↓
Cycle 2 (C++ Fixes) ────────────→ Cycle 7 (Benchmarks + Examples)
    ↓
Cycle 3 (C# Fixes)
    ↓
Cycle 4 (Python)
    ↓
Cycle 5 (Tests) ←── depends on Cycle 2 (tests for string truncation fix)
```

- Cycles 1-4: Sequential (each addresses progressively lower severity)
- Cycle 5: After Cycle 2 (tests validate Cycle 2 fixes)
- Cycles 6-7: Independent, can run in parallel with Cycles 3-5

## Tracking

After each cycle, run:
```bash
# Full C++ test suite
cmake --build --preset ninja-debug-build -j$(nproc)
ctest --test-dir build/ninja-debug --output-on-failure

# Python tests (already in CI)
cd python && python -m pytest tests/ -v

# C# tests (already in CI)
cd csharp && dotnet test
```

---

## Summary: What Gets Fixed

| Issue | Cycle | Verified | Status After |
|-------|-------|----------|-------------|
| Placeholder URLs | 1 | ✅ | Fixed |
| Forward-compat docs/code mismatch | 1 | ✅ | Fixed |
| Version confusion | 1 | ✅ | Clarified |
| Silent string truncation (4 locs) | 2 | ✅ | Throws exception |
| Writer::close() silent data loss | 2 | ✅ | Error detection added |
| Sampler pop() underflow | 2 | ✅ | Guarded |
| ZoH001 reset() state leak | 2 | ✅ | Fixed |
| Sampler string pool uint16_t overflow | 2 | ✅ | Guarded |
| C# missing finalizers (6 classes) | 3 | ✅ | Added |
| Unity PtrToStringAnsi (6 locs) | 3 | ✅ | Fixed to UTF-8 |
| Unity missing [Preserve] | 3 | ✅ | Added |
| 10 missing P/Invoke declarations | 3 | ✅ | Added |
| Python type stubs | 4 | ✅ | Generated |
| NaN handling | 4 | ✅ | Strict mode available |
| Arrow 2GB limit | 4 | ✅ | Documented |
| pathlib support | 4 | ✅ | Added |
| Version compatibility tests | 5 | ✅ | Added |
| Boundary tests | 5 | ✅ | Added |
| Unicode tests | 5 | ✅ | Added |
| Delta002 special float tests | 5 | ✅ | Added |
| Wire format golden tests | 5 | ✅ | Added |
| VLE corruption tests | 5 | ✅ | Added |
| CONTRIBUTING.md | 6 | ✅ | Created |
| CHANGELOG.md | 6 | ✅ | Created |
| Documentation dedup | 6 | ✅ | AGENTS.md → redirect |
| C# docs expanded | 6 | ✅ | Completed |
| Polars documented | 6 | ✅ | Added |
| Benchmark duplicate label | 7 | ✅ | Fixed |
| ASAN preset | 7 | ✅ | Added |
| Code coverage | 7 | ✅ | Added |
| Delta002 example | 7 | ✅ | Added |
| DirectAccess example | 7 | ✅ | Added |
| Examples in CTest | 7 | ✅ | Added |

**Total: 33 verified issues resolved across 7 cycles.**

---

## What We Explicitly Chose NOT to Fix

| Item | Reason |
|------|--------|
| C API void* watermark/registry | Trade-off: adds complexity to C API for defense against misuse. Document the pattern instead. Consider for v2. |
| Arrow large_utf8 switch | Document limit first. Switch only if users report hitting 2GB string columns. |
| Sampler sliding window test vectors (TV20-29) | These test vectors are marked as future work in the test framework. Not blocking. |
| Row::get\<bool\>() thread-local aliasing | Document the behavior rather than redesign. The pattern is unusual but functionally correct for single-expression use. |
| WebAssembly / cloud streaming (roadmap) | Quality consolidation before scope expansion. |
