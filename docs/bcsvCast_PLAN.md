# bcsvCast — Implementation Plan (DRAFT)

**Status:** Draft — for review
**Date:** 2026-07-09
**Spec:** [docs/bcsvCast_SPEC.md](bcsvCast_SPEC.md) (v0.2, all decisions settled)
**Scope:** Replace `bcsvNarrowType` with `bcsvCast`; patch version bump 1.5.8 → 1.5.9 (library and wire format unchanged).

> This plan translates the settled spec into concrete code changes, in an order that keeps
> the tree building at every step. It is for review **before** any code is written. Section
> numbers below reference the spec (e.g. spec §9 = loss model).

---

## 1. Guiding approach

`bcsvCast` is `bcsvNarrowType` with (a) a mode/plan layer in front of the existing scan,
and (b) a corrected, force-capable coercion layer behind it. The proven parts — streaming
scan, early-stabilization, atomic in-place write, codec inheritance — are **reused
verbatim**. The new surface is: argument/mode parsing, the SPEC → plan mapping, per-target
loss detection, clamp/skip application, and JSON reporting.

Net: ~40% reuse of `bcsvNarrowType.cpp`, ~60% new, plus small shared-helper refactors.

## 2. File & module layout

| File | Action |
|------|--------|
| `src/tools/bcsvCast.cpp` | **New** — the tool (seeded by copying `bcsvNarrowType.cpp`, then refactored). |
| `src/tools/bcsvNarrowType.cpp` | **Delete** (spec D5). |
| `src/tools/cli_common.h` | **Extend** — add `jsonStr()`, a type-name+alias parser, and type-classification predicates (see §6). |
| `src/tools/CMakeLists.txt` | Replace `bcsvNarrowType` target with `bcsvCast` (add_executable + `set_target_properties` list). |
| `CMakeLists.txt` (root) | Replace `bcsvNarrowType` in the tools list at ~L257. |
| `tests/test_narrow_type.cpp` | **Rename/rework** → `tests/test_cast.cpp` (keep narrowing cases as the `--optimize` regression suite; add new-mode cases). |
| `tests/integration/test_narrow_type.py` | **Rename/rework** → `tests/integration/test_cast.py`. |
| `tests/integration/conftest.py` | Update the `tools` fixture (`bcsvNarrowType` → `bcsvCast`). |
| `tests/CMakeLists.txt` | Register `test_cast` instead of `test_narrow_type`. |
| `src/tools/CLI_TOOLS.md` | Replace the `bcsvNarrowType` prose section (L595–673) **and** the file-inventory row (L84) and the "all 11 tools" line (L86). |
| `tests/CMakeLists.txt` | **Two** edits: the gtest source at L53 (`test_narrow_type`→`test_cast`) **and** the separate `pytest_narrow_type` `add_test` block at L105–110 (test name, hardcoded `integration/test_narrow_type.py` path, `set_tests_properties`). |
| `CHANGELOG.md` | **Add-only** under the existing `[Unreleased]` heading (L13): added `bcsvCast`, removed `bcsvNarrowType` (breaking, tooling). Do **not** edit historical entries. |
| `VERSION.txt` **and** a `v1.5.9` git tag | Bump `1.5.8`→`1.5.9` in `VERSION.txt` (the non-git fallback, also parsed by `python/CMakeLists.txt`), **and** tag `v1.5.9` at release — the compiled version derives from `cmake/GetGitVersion.cmake` (git tag overrides `VERSION.txt`), so the tag is what actually bumps it (§9). |
| Tool-inventory docs — **counts already stale; reconcile, don't just rename** | `README.md:202`, `.github/copilot-instructions.md:52,61,123`, `SKILLS.md:40,91,143,348`, `ARCHITECTURE.md:435`, `.claude.md:48,97`. Some say "9" and omit `bcsvNarrowType`/`bcsvCompare` (real count is higher); fix each for the post-change list. Broader count audit tracked separately (§12.5). |

Decision to confirm: **factor shared helpers into `cli_common.h` vs. keep local.** Plan
assumes `cli_common.h` so `bcsvCompare`/`bcsvValidate` can later dedupe onto them (§6).

## 3. Reuse vs. rewrite of `bcsvNarrowType.cpp`

**Reuse ~verbatim:**
- `doubleIsWholeNumber()` — IEEE bit test (correct; keep).
- `scanFile()` streaming loop and metadata capture (flags/comp/packet).
- In-place temp-file + atomic-rename block in `main()` (mkstemp/_mktemp_s).
- Output-path safety (same-path rejection, `--overwrite` guard).
- `convertFile()` reader→writer streaming skeleton and the `withWriter` dispatch.

**Reuse for auto modes only (`--scan`/`--optimize`):**
- `ColumnProbeState` narrowing logic (integer ladder, float round-trip, string probe),
  with tolerance-aware comparisons (§5.3). **Not verbatim for explicit modes:**
  `checkStabilization` (stops when `optimal_type==original_type`) and the zero-row
  "empty-column guard" (forces `optimal_type=original_type`) are auto-narrowing-specific.
  `--static`/`--dynamic` get a **separate probe/plan path** (§5.3): a fixed target + loss
  verdict, and `--static` must apply its target even to a **zero-row** column (the
  empty-column guard must not veto it; a zero-row column has no values to lose, so
  `--dynamic` applies too).

**Rewrite / extend:**
- `coerceValue()` / `coerceInt()` → split into `coerceChecked` (lossless/throws) and
  `coerceForce` (clamp/round/NaN, counts clamps) with the `2⁶³`/`2⁶⁴` boundary fix (§5.4).
- `convertFile()` builds `dst_layout` from `probes[i].optimal_type` today → generalize to
  build it from the **resolved plan** (auto or explicit target) per column.
- `Config` + `parseArgs()` → mode state machine + conflict matrix (§4).
- `printAnalysis()` → mode-aware reporter + JSON (§7).
- New: SPEC parser, alias parser, per-mode plan builder.

## 4. Argument parsing & mode resolution

New `Config`:

```
enum class Mode { Scan, Optimize, Dynamic, Static };  // resolved, never "Default"
struct Config {
  std::string input, output, spec;      // spec set only for Dynamic/Static
  Mode mode; bool mode_explicit=false;
  bool in_place=false, overwrite=false, json=false, verbose=false, string_to_value=false;
  double tolerance=0.0;
  std::string cols;                      // auto modes only
};
```

Parsing rules (spec §3.2, §3.3), implemented with `bcsvNarrowType`'s explicit
per-flag argument checks (not `bcsvCompare`'s `i+1<argc` fall-through, which the review
flagged as producing "unknown option" for a missing SPEC):

1. Recognize `--scan/--optimize/--static/--dynamic`; `--static`/`--dynamic`
   consume the **next token verbatim** as SPEC (so `-1=bool` is safe). ≥2 distinct modes →
   `ArgError`.
2. `--tolerance`, `--cols`, `-o` require an argument (explicit `ArgError` if absent).
   Reject attached `--flag=value` form (document; the hand parser only does space-separated).
3. Positionals: first = input, second = output. **A positional containing `=` →
   `ArgError`** ("looks like a SPEC; quote it") — guards brace-expansion (spec §7).
4. Post-parse resolution:
   - No mode flag: `mode = output-present ? Optimize : Scan` (spec N1).
   - `Scan` + any output selector (`-o`/`--in-place`/2nd positional) → `ArgError`
     ("--scan is read-only; use --optimize").
   - `--cols` with `Static`/`Dynamic` → `ArgError`.
   - `--in-place` with output/`-o`, or output + `-o` → `ArgError`.
   - `--string-to-value` with `Static`/`Dynamic` → ignored (documented no-op; not an error).
   - `convert = (mode != Scan) && has_output`.

Exit codes unchanged: `ArgError`→2, `std::exception`→1, success→0 (spec §11.1).

## 5. Types, SPEC, loss, and force

### 5.1 Type-name + alias parser (in `cli_common.h`)

`bcsv_cli::parseColumnType(std::string s, bool allow_void=false) -> bcsv::ColumnType`:
- lower-case `s`; map the full alias table (spec §8) to canonical, then delegate to
  `bcsv::toColumnType()` for the canonical name; throw `std::runtime_error` listing valid
  names on miss.
- **Guard VOID explicitly:** `bcsv::toColumnType()` maps both `""` and `"void"` → VOID
  (`definitions.h:328`), so `parseColumnType` must (a) reject the empty string, and
  (b) accept `"void"` only when `allow_void` (a positional-list slot for an already-VOID
  column); otherwise VOID is not a valid cast type.
- Companion `bcsv_cli::isCastableType()` (everything except VOID).

### 5.2 SPEC parser → explicit plan

`parseSpec(spec, layout) -> std::vector<ColumnType> targets` (size = columnCount; entry ==
current type where unspecified):

1. Split on `,` (respecting optional outer `{}`); trim each entry. Reject empty entries and
   trailing commas.
2. If **any** entry contains `=` → **map form**: split each entry on the **first** `=` into
   `KEY`/`TYPE`; parse `KEY` via `parseIndexRanges(KEY, n)`, `TYPE` via §5.1. `parseSpec`
   owns these checks — `parseIndexRanges` does **not** give them for free:
   - **Empty KEY → error.** `parseIndexRanges("", n)` returns *match-all*, not an error
     (`cli_common.h:337-339`), so `=int32` would silently target every column. Reject an
     empty key before calling it.
   - **Cross-key duplicates → error.** parseIndexRanges sorts/merges *within* one key only;
     track a `seen` bitset across keys and throw on any index assigned twice (spec §7.1).
   - **VOID source or target → error.** Reject a KEY naming a currently-VOID column, and a
     `TYPE` of VOID (spec §7.4; target VOID is already blocked by §5.1's guard).
   - Out-of-range indices are thrown by parseIndexRanges.
3. Else → **positional-list form**: entry count must equal `columnCount`; assign in order;
   on mismatch throw with expected/received/first-differing-index and a "use map form" hint
   (spec §7.2). A `void` slot (parsed with `allow_void`) must match a currently-VOID column.
4. Unspecified columns keep their current type (map form only).

### 5.3 Loss detection (scan pass, explicit modes)

Extend the scan to also evaluate a **fixed target** per in-scope column. Add to
`ColumnProbeState` (or a sibling struct) an explicit-target path:

`bool castLoses(ColumnType src, ColumnType dst, <typed value>, double tol, uint64_t& oor)`
— returns whether this cell loses data; increments `oor` when out of range. Built from the
existing primitives:
- int→int / int→bool: range/{0,1} test (integer, exact).
- int→float/double: **round-trip** (`(Src)(Dst)v == v`) — *not* a magnitude bound
  (spec §9, review R2#3).
- float/double→int: `isfinite ∧ |v−std::round(v)|≤tol ∧ round(v)` in range, with the strict
  `2⁶³/2⁶⁴` exclusion (spec §9.3).
- **float/double→bool: value ∈ {0,1}; NaN/±Inf ⇒ loss** (spec §9; was missing from the list).
- double→float: non-finite ⇒ lossless-with-caveat; else `|v−(double)(float)v|≤tol`.
- →string / string→ (parse then numeric rule; non-finite ⇒ §9.2; unparseable ⇒ mark
  lossy+flag for the force/skip decision).

Per column the scan accumulates `lossy = OR over cells`, `oor_count`, and (for static) a
sample value, so `--dynamic` can skip and `--static` can report.

**TOL≥0.5 warning:** the float→int wholeness test is vacuous at `TOL≥0.5` (spec §9.4). When
`--tolerance ≥ 0.5` reaches a float→int wholeness test, emit the one-time stderr warning
here (loss layer), gated by tolerance — the review noted this was tested but had no emitter.

### 5.4 Coercion (convert pass)

Two functions, replacing today's `coerceValue`/`coerceInt`:

- `coerceChecked(dst, value) -> ValueType` — lossless path (narrow, dynamic-applied,
  static-lossless columns). Same as today's coerce **plus** the strict `2⁶³/2⁶⁴` guard.
  Throws on unexpected overflow (defensive; scan guaranteed lossless) → exit 1.
- `coerceForce(dst, value, uint64_t& clamped) -> ValueType` — static lossy path
  (spec §10): `NaN→0`; `±Inf`/overflow → type max/min; else `std::round` then clamp to
  `[min,max]`; `→bool` = `value!=0` within tol; increments `clamped`. String source:
  `strtod`; finite→rules; non-finite→NaN/Inf rule; **unparseable→throw** (exit 1).

Per-column dispatch in `convertFile`: `unchanged` → verbatim `visitConst` copy (today's
path); `lossless` → `coerceChecked`; `static & lossy` → `coerceForce` with clamp counting.

### 5.5 Numeric→string formatting

Add `bcsv_cli::toRoundTripString(double, is_float)` using **`bcsv::compat::to_chars`**
(`include/bcsv/std_charconv_compat.h` — the repo's portability shim; float `std::to_chars`
is **not** guaranteed at C++20 and is deliberately wrapped, cf. `csv_writer.hpp:246`) with
`max_digits10` 17/9, so numeric→string is genuinely lossless (spec §9.1). String *targets*
only arise from explicit modes; a numeric value passed straight to a STRING column
**throws** in `Row::set` (`row.hpp:368-373`), so always format to `std::string` first, then
`set` (the `std::visit`→`set` path accepts a `std::string` in `ValueType`).

## 6. Shared-helper refactors (`cli_common.h`)

- **`jsonStr()`** — lift from `bcsvValidate.cpp` (currently file-local) into `cli_common.h`;
  update `bcsvValidate` to use it (no behavior change).
- **`parseColumnType()` / alias table** — new (§5.1).
- **type predicates** — `isInteger/isSignedInt/isFloat` exist in `src/shared/comparison.h`;
  rather than couple `bcsvCast` to that header, add lean equivalents (or a tiny
  `type_traits` inline set) to `cli_common.h`. Future cleanup: converge `comparison.h`
  onto them (out of scope here; noted).
- **type min/max bounds** — a `constexpr` table for clamp ranges (int8…uint64), keyed by
  `ColumnType`.

## 7. Reporting

Single reporter driven by `Config::mode`, `json`, and the plan:
- **Human (stdout):** the `printAnalysis` table generalized to a `Note` column
  (`(unchanged)`, `→ narrowed`, `→ narrowed (within tol=T)`, `skipped: …`, `forced: N
  clamped`), tolerance echoed in the header, savings footer retained.
- **Warnings (stderr):** one line per clamped/skipped column, exact format per spec §11.2.
- **JSON (stdout, `--json`):** hand-rolled object per spec §11.3 using `jsonStr`; includes
  `num_columns`, per-column `bytes_original/target`, `applied/lossy/clamped_cells/
  values_out_of_range/skipped/reason`, and a `suggested_spec` built from the plan (map
  form). `--scan --json` enumerates all in-scope columns.
- **Channel rule:** table/JSON → stdout; progress/warnings/errors → stderr.

## 8. CMake, tests, docs, version

- **CMake:** swap the target in `src/tools/CMakeLists.txt` (add_executable + properties
  list) and root `CMakeLists.txt` (~L257). `bcsvCast` links `bcsv`; add
  `target_include_directories(... src/shared)` only if we reuse `comparison.h` (plan avoids
  it — so not needed).
- **Unit tests (`tests/test_cast.cpp`):** carry over every `NarrowTypeTest` case retargeted
  to `bcsvCast --optimize`; add: SPEC parser table (map/list/ranges/aliases/errors, empty-KEY
  rejected, cross-key duplicate rejected, VOID as **source and** target rejected),
  loss-detection edges (2³⁰/2⁶⁰ round-trip, `2⁶³` boundary, NaN/Inf, subnormal, tol
  boundary, tol≥0.5 warning **emitted**), **float/double→bool loss** (`{2.0, NaN}`→bool
  skipped under `--dynamic`), **numeric→string fidelity** (`3.141592653589793` double→string
  and a 9-digit float case → read back, assert exact text), force clamps (`300→int8=127`,
  `−5→uint8=0`, `3.7→int=4`, `NaN→0`, `1e300→INT32_MAX`, unparseable→exit 1), default-mode
  inference, apply-always-writes (no-op still writes), all §3.2 conflicts → exit 2, `--json`
  shape + `suggested_spec` reuse.
- **Integration (`tests/integration/test_cast.py`):** per-mode end-to-end vs.
  `bcsvGenerator` profiles, value round-trips via `bcsvCompare --mode values`, unquoted-
  `{}` footgun caught, `bcsvNarrowType` target gone.
- **Docs:** `CLI_TOOLS.md` new section (usage, modes, SPEC grammar, examples from spec
  §13); `CHANGELOG.md` Added/Removed entries; version bump.
- **Version:** wire format & library API unchanged (only a CLI tool changes) ⇒ **PATCH**
  (`1.5.9`); realized via a `v1.5.9` **git tag** (§2), not just `VERSION.txt`.

## 9. Build sequencing (each step compiles & tests green)

1. **Rename only, no behavior change.** Copy `bcsvNarrowType.cpp`→`bcsvCast.cpp` **verbatim**
   (no new flags yet); swap CMake targets; delete old target. Update the tests' *harness* to
   the new binary — `findNarrowBin()` paths, the `HelpFlag` assertion string, the conftest
   `tools` fixture — but keep **bare** invocations (`bcsvCast in out`), which the verbatim
   tool accepts. Do **not** add `--optimize`/`--string-to-value` to tests yet (the verbatim
   tool rejects unknown flags → exit 2). Gate: existing narrow suite passes against
   `bcsvCast` in its bare form.
2. **Shared helpers.** Add `jsonStr`, `parseColumnType`+aliases, type predicates, min/max
   table to `cli_common.h`; migrate `bcsvValidate` to shared `jsonStr`. Gate: full build +
   existing suites green.
3. **Mode/arg layer + test retarget.** Introduce `Mode`, the conflict matrix, and
   default-mode inference; `--scan`/`--optimize` behave as before. **In the same commit,**
   retarget tests to `--optimize` and rename `--stringsToValue`→`--string-to-value` in the ~8
   string tests (both flags exist only from here on). Add the arg-conflict unit tests.
   Gate: green.
4. **Coercion split + `2⁶³` fix.** Replace coerce fns with `coerceChecked`/`coerceForce`;
   generalize `dst_layout` to the resolved plan; `--optimize` routes through `coerceChecked`.
   Gate: narrow value round-trips.
5. **SPEC parser + explicit plan (parser-only gate).** Add `parseSpec`; wire `--dynamic`/
   `--static` plan build. Gate: **parser/plan unit tests only** — a `--dynamic`/`--static`
   dry-run report needs the loss scan from step 6, so do not gate on skip/`forced:` output.
6. **Explicit convert + force.** Loss scan for explicit targets (incl. float→bool);
   `--dynamic`/`--static` dry-run reports **and** apply with clamp/skip; apply-always-writes.
   Gate: force/skip unit + integration, `bcsvCompare --mode values` round-trips.
7. **Reporting + JSON.** Generalized table, stderr warnings, `--json` + `suggested_spec`.
   **Preserve the "narrowest" wording** the ~6 retargeted scan tests assert (or update those
   assertions in this commit and re-run its gate). Gate: JSON shape + scan-wording tests.
8. **Docs/version/changelog.** Update all migration targets (§2), bump `VERSION.txt`, and
   note the release must be tagged `v1.5.9` for the compiled version to change. Gate:
   `ctest` full, `--help` reviewed, doc links valid.

## 10. Risks & tricky bits

- **`ValueType` variant dispatch** for arbitrary src→dst at runtime: keep the existing
  `visitConst<T>` per-source-type switch (compile-time typed) feeding `coerce*`; do **not**
  attempt a fully dynamic matrix. Well-trodden in today's `convertFile`.
- **String targets** require the writer to accept `std::string` via `dst_row.set` — verify
  in step 6 with a `*→string` case (an existing code path for STRING columns exists).
- **Tolerance labeling** must thread `tol` into both the scan verdict and the report so a
  relaxed narrow never prints bit-exact wording (spec §9.4).
- **VOID columns** are rare but must round-trip untouched and be rejected as cast targets;
  covered by a dedicated parser test.
- **Early stabilization vs. explicit targets:** an explicit-target column can also stop
  early once it has seen a lossy cell (no need to keep scanning it) — a small optimization,
  not required for correctness; implement only if free.

## 11. Estimate

~900–1100 LOC for `bcsvCast.cpp` (vs. 1147 today), ~+150 LOC `cli_common.h`, ~+400 LOC
tests. 8 sequenced steps; each independently reviewable and revertible.

## 12. Open items for the plan review

1. Confirm shared helpers land in `cli_common.h` (vs. a new `src/tools/type_spec.h`).
2. Confirm we do **not** couple to `src/shared/comparison.h` (avoids an include-dir change);
   accept minor predicate duplication now, converge later.
3. Confirmed: `1.5.9` (**patch** — library/wire format unchanged), realized via a `v1.5.9`
   **git tag** at release (VERSION.txt is only the non-git fallback).
4. Confirm `--string-to-value` with `--static/--dynamic` is a silent no-op (not exit 2).
5. Pre-existing **stale tool counts** (docs say "9"/"11" inconsistently, omit `bcsvCompare`
   and the parquet tools) are broader than this change — fix the `bcsvNarrowType` references
   here; the full inventory reconciliation is a **separate cleanup** (candidate for its own
   task).
