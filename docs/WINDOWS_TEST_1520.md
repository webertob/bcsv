# Windows Validation — 1.5.20 Candidate (branch `release/1.5.20-windows-verify`)

This branch carries the full 1.5.20 work (two consumer-defect fixes from the
com.bcsv.unity 1.5.19 reports + review-round hardening) **unreleased**: no tag,
no packaging. Linux/Clang validation is complete (see "Already verified"
below). What Windows/MSVC must prove is that everything compiles and passes
with `cl.exe` — the MSVC portability shims (`BCSV_ALWAYS_INLINE` /
`BCSV_NOINLINE` in `include/bcsv/definitions.h`, the `__declspec`
paths, the `_mkdir` artifact redirection in the new C test) have never seen a
real MSVC.

**Report back**: full console output of each step, pass/fail counts, and any
compiler warnings from the C-API translation units. If everything is green,
say so explicitly — that is the go for tagging `v1.5.20`.

## 0. What changed for MSVC (motivation, so failures are diagnosable)

- `include/bcsv/definitions.h`: new `BCSV_NOINLINE` alongside the
  existing `BCSV_ALWAYS_INLINE` (`__declspec(noinline)` on MSVC,
  `__attribute__((noinline))` on GNU/Clang, empty elsewhere).
- `src/bcsv_c_api.cpp`: the 1.5.20 getter pipeline
  (`row_index_ok`, `row_cell_strict`, `row_cell_value`, `row_cell_widen`,
  `row_cell_to_double`) now uses those portable macros instead of bare
  GNU attributes — this was the reviewer's MSVC-breaker (item 1).
- New handle-registry guard `reg_contains` behind close/flush (item 2,
  use-after-free); exercised by the new test below.
- New C test `tests/bcsv_c_api_defects_1520_test.c` compiles as **C** under
  MSVC; it uses `_mkdir` via `BCSV_MKDIR` and writes all scratch files under
  `tmp/` (auto-created). The fork-based exit-children are `#ifndef _WIN32`
  and are **skipped by design on Windows** — their assertions still run
  in-process there.

## 1. Prerequisites

- Visual Studio 2022 **17.4+** with the C++ workload (any `cl.exe` >= 19.34)
  — or CI's arrangement: `ilammy/msvc-dev-cmd` + `choco install ninja`.
- Ninja on `PATH` (`choco install ninja -y --no-progress`).
- Run from a **Developer Command Prompt / PowerShell with `vcvars64`** so
  `cl.exe` is on `PATH` (the `ninja-msvc-*` presets set `CMAKE_C(XX)_COMPILER`
  to `cl`/`cl.exe` and are single-config Ninja — no `-C` config flag needed).
- Python 3.10+ with `pytest` on `PATH` (some CTest entries are pytest-driven;
  they skip cleanly if Python is not found — note which skip in your report).
- .NET SDK 8.0 **and** 10.0 (C# test project is multi-targeted).

## 2. Pull the branch

```powershell
git fetch origin
git checkout release/1.5.20-windows-verify
git log --oneline -2   # HEAD must be the "release: 1.5.20 ..." commit
```

HEAD is an untagged commit (the highest tag reachable is `v1.5.19`), so
`cmake/GetGitVersion.cmake` reports a dev version off `VERSION.txt` (1.5.20)
and the configure-time version gate passes without any local hack. If you
see `FATAL_ERROR ... VERSION.txt`, you are on a tagged commit — re-check the
checkout in step 2.

## 3. CMake / C++ build + full test suite (Debug)

```powershell
cmake --preset ninja-msvc-debug
cmake --build --preset ninja-msvc-debug-build --parallel
ctest --test-dir build/ninja-msvc-debug --output-on-failure
```

Expect **~990 tests pass** (a few pytest-driven tests may Skip if Python is
missing — report the skips). Watch compiler output for new warnings from
`src/bcsv_c_api.cpp` and the test executables.

## 4. Focused defect-regression run (the heart of the validation)

```powershell
build\ninja-msvc-debug\bin\test_c_api_defects_1520.exe
```

Expect `63/63 assertions passed`, exit code 0. Coverage includes — per the
two defect reports:

- double destroy (writer/csv-writer/reader/row/layout/sampler/csv-reader) is
  a logged no-op, never an abort;
- `bcsv_shutdown()` then late `*_destroy`/`close`/`flush` from an atexit-style
  late finalizer (`c_shutdown_close` child mode) is a logged no-op;
- wrong-typed `bcsv_row_get_*` never returns silent 0 — always fallback +
  fresh `bcsv_last_error()`; `GetDouble` widens the exactly-representable set
  (ADR-0006) and rejects int64/uint64/string;
- `bcsv_row_try_get_*` twins: `true`/`false`, `*out` untouched on failure.

On Windows the POSIX `fork()` exit-children print `SKIP` lines — that is
by design (the same assertions run in-process via the atexit child modes).

If the process aborts inside `bcsv_writer_destroy` on exit — that is exactly
Defect 1; capture the stack and stop, report as failure.

## 5. Release configuration too

```powershell
cmake --preset ninja-msvc-release
cmake --build --preset ninja-msvc-release-build --parallel
ctest --test-dir build/ninja-msvc-release --output-on-failure
build\ninja-msvc-release\bin\test_c_api_defects_1520.exe
```

(Optimization is where the `always_inline`/`noinline` pairing actually
matters — if `bcsv_row_get_double` behaves oddly or test 4's assertions fail
only here, that is signal.)

## 6. C API on the release build: DLL consumers

The `bcsv_c_api.dll` lands in `build\ninja-msvc-release\bin\` (it is
co-located there with the C API test exes by design, so steps 3-5 already
loaded it). The C API test binaries are in the same folder — if step 4/5 ran,
DLL-resolution is proven.

## 7. C# bindings

```powershell
cmake --build build/ninja-msvc-debug --target bcsv_c_api --parallel
copy build\ninja-msvc-debug\bin\bcsv_c_api.dll csharp\src\Bcsv\runtimes\win-x64\native\
dotnet test csharp\tests\Bcsv.Tests
```

(`csharp/src/Bcsv/Bcsv.csproj` copies `runtimes\**` to the test output via
`CopyToOutputDirectory=PreserveNewest`, mirroring the flat-copy trick used on
Linux. If `runtimes\win-x64\native` doesn't exist, create it. Expect
**141 passed** per targeted TFM; if only one SDK is installed, report which
TFM passed and skip the other — say so.)

Parity guard (optional but useful, runs anywhere C# builds):

```powershell
python scripts\check_csharp_parity.py
```

## 8. Unity package smoke (optional, if a Unity editor is available)

`unity-package.yml` builds `bcsv_c_api.dll` for the package directly with
cmake (the former `scripts/build-unity-windows.ps1` wrapper was dropped as a
duplicate build definition):

```powershell
python scripts\check_versions.py --skip-manifests --self-contained --native build\bcsv_c_api.dll
```

If no editor is available, skip and say so — Unity C# code is unchanged from
1.5.19 except the P/Invoke getters/`BcsvRuntime`, whose behaviour is covered by
the C# tests in step 7.

## 9. Known-good baseline (Linux, already verified on this exact tree)

- GCC 13 debug **and** release trees: 990/990 CTest, twice.
- Clang `uBSan` + `ThreadSanitizer`: `test_c_api_defects_1520` 63/63 clean.
- C# 141/141 on net8.0 and net10.0 against the 1.5.20 native lib; parity
  script OK; Unity EditMode 25/25 headless.
- A/B micro-benchmarks vs v1.5.19: matched getters +0.2 ns, `get_double`
  parity, widened `get_double` ~5 ns (was ~500 ns throw or silent 0);
  micro/macro suites no regression.

## 10. What we are NOT asking you to do

Do not tag, do not `git push` anywhere, do not edit code. Any fix you make
locally to get MSVC to compile is **gold** — make it, and report the diff;
we will fold it in before tagging. The whole point of this document is that
MSVC gets a fair chance to compile before the release exists.
