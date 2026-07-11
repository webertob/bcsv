# bcsvCast — Specification (DRAFT v0.2)

**Status:** Draft — for review (v0.2, incorporates three independent spec reviews)
**Date:** 2026-07-09
**Supersedes:** `bcsvNarrowType` (removed — see §14)
**Related:** `src/tools/bcsvNarrowType.cpp`, `src/tools/bcsvCompare.cpp`, `src/tools/cli_common.h`, `include/bcsv/definitions.h`

> Design specification for review, **not** an implementation plan. All CLI/semantic
> decisions are settled (§16), including the v0.2 review-round items (§16.2). A separate
> implementation plan follows spec approval.

**v0.2 change log (from review):** default-mode + output path now applies `--optimize`
(migration fix); mode-conflict / flag-conflict / missing-argument errors enumerated;
loss model corrected (round-trip is normative, strict 2⁶³/2⁶⁴ bounds, NaN/Inf rules,
round-trip string formatting, `std::round` named, TOL<0.5 caveat); force semantics define
NaN/±Inf ordering; apply-modes always write output; JSON schema enriched for agents;
`--string-to-value` kebab-cased; `ch`/`char`→`uint8`.

---

## 1. Motivation

`bcsvNarrowType` scans a BCSV file to find the smallest lossless type per column and
optionally rewrites with those types — it only ever *shrinks*, and only *losslessly*.
We want one tool that can also apply **caller-chosen** types (lossy or not) while keeping
the existing auto-narrowing. `bcsvCast` generalizes the proven two-pass architecture
(scan → optional convert) into a mode-driven column re-typing tool.

## 2. Terminology

- **Plan** — a per-column mapping `original_type → target_type`. Every mode produces a
  plan; applying it rewrites the file.
- **Auto-derived plan** — target types computed by scanning the data (smallest type that
  holds every value within tolerance). What `bcsvNarrowType` computes today.
- **Explicit plan** — target types supplied by the caller as a *type spec* (§7).
- **Loss** — a cast where at least one cell's value cannot be reproduced from the target
  type within `--tolerance` (§9).
- **Dry-run** — produce and print the plan; write no output file.

## 3. Command synopsis

```
bcsvCast [MODE] [OPTIONS] INPUT_FILE [OUTPUT_FILE]
```

Exactly one **mode** selects where target types come from and the loss policy:

| Mode | Target types | Writes output? | Loss policy |
|------|--------------|----------------|-------------|
| `--scan` | auto-derived | **never** (read-only) | reports smallest lossless types |
| `--optimize` | auto-derived | if output path given | lossless by construction |
| `--dynamic SPEC` | explicit (SPEC) | if output path given | skip any column whose cast would lose data |
| `--static SPEC` | explicit (SPEC) | if output path given | apply anyway; lossy cells clamped (§10) |

**Default mode (no mode flag):** behaves as `--optimize` when an output
path is present, and as `--scan` when none is. This keeps the muscle-memory
`bcsvNarrowType in.bcsv out.bcsv` working (it now narrows instead of erroring). An
*explicit* `--scan` is always read-only (see §3.2).

### 3.1 Dry-run vs. apply

One uniform rule for every writing mode (`--optimize`, `--dynamic`, `--static`, and the
default):

- **No output path** → dry-run: print the plan, write nothing, exit 0.
- **Output path present** (positional `OUTPUT_FILE`, `-o/--output`, or `--in-place`) →
  apply the plan (and always write the file, even for a no-op — §12).

`bcsvCast --static '{...}' in.bcsv` previews the forced cast; add `out.bcsv` to apply it.

### 3.2 Mode & option conflicts (all → argument error, exit 2)

- **Two or more modes** (`--scan --optimize`, `--static X --dynamic Y`, …).
- **`--scan` with any output path** (`-o`, `--in-place`, or a second positional). Message:
  `"--scan is read-only; use --optimize to apply."`
- **`--cols` with `--static`/`--dynamic`** (the SPEC already defines scope). Rejected, not
  ignored.
- **More than one output selector** — `--in-place` with `OUTPUT_FILE` or `-o`; `OUTPUT_FILE`
  and `-o` both given (`"output specified more than once"`).
- **A mode flag missing its SPEC** (`--static` / `--dynamic`), or `--tolerance` / `--cols`
  / `-o` missing its argument → explicit per-flag error (not a generic "unknown option").
- **A positional argument containing `=`** (e.g. a brace-expanded `3=float`) is rejected as
  an output path with a hint to quote the SPEC — guards the fish/bash `{...}` footgun (§7).
- Attached forms (`--static=SPEC`, `--tolerance=1e-9`) are **not** supported by the
  hand-rolled parser; use space-separated (`--static SPEC`). Documented in `--help`.

### 3.3 Options

```
Modes (choose one; default = optimize-if-output-else-scan):
  --scan                 Report smallest lossless type per column (read-only)
  --optimize             Auto-derive smallest lossless types and apply
  --dynamic SPEC         Apply SPEC per column, skipping any lossy column
  --static  SPEC         Apply SPEC, clamping lossy values (forced)

Scope (auto modes only):
  --cols RANGES          Restrict --scan/--optimize to these column indices
                         (e.g. '0:3,5,7:-1'); illegal with --static/--dynamic

Loss control:
  --tolerance TOL        Absolute epsilon for float/int loss tests (default: 0.0)
  --string-to-value      Let --scan/--optimize consider STRING→numeric (auto modes only)

Output:
  -o, --output FILE      Output BCSV file (enables apply)
  --in-place             Rewrite INPUT_FILE atomically (temp + rename)
  --overwrite            Allow overwriting an existing OUTPUT_FILE

Reporting:
  --json                 Emit the plan/result as JSON on stdout (§11.3)
  -v, --verbose          Per-column detail and progress to stderr
  -h, --help             Show help

Exit codes: 0 success · 1 runtime error · 2 argument error
```

## 4. Modes in detail

### 4.1 `--scan` (read-only)

Scans the (optionally `--cols`-restricted) columns and prints, per column: index, name,
original type, smallest lossless target within `--tolerance`, and whether it changes.
Identical in spirit to today's `bcsvNarrowType` analyze. Never writes. This is `--optimize`'s
first pass without the second.

### 4.2 `--optimize` (auto-derive + apply)

Same scan as `--scan`; if an output path is given, applies the auto-derived plan.
Lossless within `--tolerance` (see the labeling caveat in §9).

### 4.3 `--dynamic SPEC` (explicit + skip-on-loss)

1. Parse SPEC into an explicit plan (§7).
2. Scan the affected columns to decide, per column, whether the requested cast is lossless
   within `--tolerance`.
3. Lossless → apply the requested type. Would-lose-data → **keep original type**, report
   the skip. Columns not named in SPEC are copied unchanged.

"Make these columns int32 *if it fits*, else leave them alone."

### 4.4 `--static SPEC` (explicit + force)

1. Parse SPEC into an explicit plan (§7).
2. Scan the affected columns to *report* what will be lost (§9).
3. Apply the requested type to **every** named column, even when lossy; non-representable
   cells are clamped per §10. Columns not named are copied unchanged.

"I know what I want and accept the loss."

## 5. Column scoping

- `--scan` / `--optimize`: all columns by default; narrow with `--cols RANGES` (reusing
  `bcsv_cli::parseIndexRanges`: inclusive ranges, negative-from-end, `0:3,5,7:-1`).
- `--static` / `--dynamic`: the SPEC defines scope; unnamed columns are copied verbatim.
  `--cols` is rejected (§3.2).

## 6. Execution model (two passes)

Every mode runs the same **scan pass** first, then apply modes with an output path run a
**convert pass** — mirroring today's `bcsvNarrowType`:

1. **Scan pass** (always): stream the file row-by-row and build the per-column plan.
   `--scan`/`--optimize` derive target types from the data; `--static`/`--dynamic` take
   targets from the SPEC and use the scan only to detect loss (clamp counts for `--static`,
   skip decisions for `--dynamic`). Columns can "stabilize" early (once a probe can no
   longer narrow) exactly as today, so a fully-decided scan may stop before EOF.
2. **Convert pass** (apply modes with an output path): stream reader → writer, coercing each
   cell per the plan; unchanged columns are copied verbatim. Output codec/flags/packet size
   are inherited from the input (§12). `--in-place` writes to a temp file and atomically
   renames.

The scan is `O(rows)`; agents needing only the current schema should use `bcsvHeader`
(header-only) instead of a scan.

## 7. Type spec grammar (`SPEC`)

Two mutually exclusive forms. Braces are **optional** and, when present, **must be
quoted** (fish/bash brace-expand `{a,b}`): `--static '{0=int32,3=float}'` or
`--static 0=int32,3=float`. An unquoted brace spec that splits into multiple argv words is
caught by the "positional contains `=`" rule (§3.2).

### 7.1 Map form (subset of columns)

Comma-separated `KEY=TYPE`, `KEY` a column index or inclusive index range (same grammar as
`--cols`), `TYPE` a type name (§8):

```
0=int32, 1=uint64, 7:8=float, -1=bool
```

- Ranges use `:` (`7:8` = columns 7 and 8); negative-from-end indices allowed (`-1`).
- Whitespace around `=`, `,`, and `:` is trimmed (`0 = int32` is valid).
- A column named more than once (incl. overlapping ranges) → argument error.
- An index or range out of `[0, columnCount)` → argument error (never silently clamped).

### 7.2 Positional-list form (all columns)

Comma-separated type names with **no** `=`, one per column in order, covering **every**
column, including any non-castable ones:

```
int32,uint64,bool,bool,string,float,float,float
```

- The count must equal the file's column count. On mismatch the error names the expected
  count, the received count, and the first index that differs from the current layout, and
  suggests the map form. (Profiles here reach ~290 columns; a bare "count mismatch" is
  hostile.)
- A type equal to a column's current type is a no-op for that column.
- Empty entries / trailing commas → argument error.

### 7.3 Disambiguation

Any entry containing `=` → map form (**all** entries must be `KEY=TYPE`). Otherwise →
positional list. Mixing the two → argument error.

### 7.4 `VOID` and non-castable columns

`ColumnType::VOID` (the enum sentinel) is not a castable type. A VOID column is copied
verbatim; naming it as a cast source or target (in either form) → argument error. In the
positional-list form its slot must still be present and must equal its current type
(conventionally written `void`), so the one-per-column count stays unambiguous.

## 8. Type names and aliases

**Canonical names** (from `bcsv::toColumnType`, used in all output): `bool`, `int8`,
`int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64`, `float` (32-bit),
`double` (64-bit), `string`.

**Aliases** (case-insensitive; normalized to canonical in reports and output):

| Canonical | Aliases |
|-----------|---------|
| `bool`   | `b` |
| `int8`   | `i8`, `sbyte` |
| `int16`  | `i16`, `short` |
| `int32`  | `i32`, `int` |
| `int64`  | `i64`, `long` |
| `uint8`  | `ui8`, `u8`, `uchar`, `char`, `ch`, `byte` |
| `uint16` | `ui16`, `u16`, `ushort` |
| `uint32` | `ui32`, `u32`, `uint` |
| `uint64` | `ui64`, `u64`, `ulong` |
| `float`  | `f`, `f32`, `single` |
| `double` | `d`, `f64` |
| `string` | `str`, `s` |

Notes / documented caveats:
- `int`→`int32`, `long`→`int64` (C-width intuition). **Windows caveat:** platform `long`
  is 32-bit; here `long` always means `int64`. Documented in `--help`.
- `ch`/`char`/`byte`→`uint8` (a byte, 0–255).
- `b`→`bool` (note: NumPy's `b` is int8; here `b` is boolean). Documented in `--help`.
- `float` = 32-bit, `double` = 64-bit.
- Unknown names → argument error listing the full valid set.

## 9. Loss model and tolerance

A cast is **lossless** for a column iff *every* cell reproduces its value from the target
type within the absolute epsilon `--tolerance TOL` (default `0.0`, bit-exact — matching
`bcsvCompare`).

**Normative test (round-trip).** For any numeric→numeric cast, losslessness is defined by
casting the value to the target type and back to the source domain and comparing within
TOL — **not** by a magnitude bound. Magnitude figures below (2²⁴, 2⁵³) are informal
intuition only; e.g. `2³⁰` is exact in `float` and `2⁶⁰` is exact in `double` despite
exceeding those figures, and the round-trip test correctly accepts them.

Per source→target category:

| Source | Target | Lossless when |
|--------|--------|---------------|
| any T | same T | always (no-op) |
| bool | int/uint/float/double | always (0/1 exact) |
| bool | string | always |
| int/uint | wider same-signedness int | always |
| int/uint | narrower / different-sign int | value ∈ target range (negative→unsigned = loss) |
| int/uint | `float` | round-trips through float32 (informally ≲2²⁴) |
| int/uint | `double` | round-trips through float64 (informally ≲2⁵³) |
| int/uint | bool | value ∈ {0,1} |
| int/uint | string | always |
| float/double | int/uint | finite ∧ `|v−std::round(v)| ≤ TOL` ∧ `std::round(v)` ∈ target range |
| double | float | non-finite → §9.2; else `|v−(double)(float)v| ≤ TOL` |
| float | double | always |
| float/double | bool | value ∈ {0,1} (NaN/±Inf = loss) |
| float/double | string | always (shortest round-trip text — §9.1) |
| string | numeric/bool | parses fully; then per the numeric rule; non-finite → §9.2; **unparseable = error** (§10) |
| any | string | always (shortest round-trip text — §9.1) |

### 9.1 numeric→string formatting

"Always lossless" for numeric→string holds **only** with shortest round-trip formatting
(`std::to_chars`, i.e. `max_digits10` = 17 for `double`, 9 for `float`). Fixed-precision
(`std::to_string`, 6 sig-figs) would silently truncate `3.141592653589793`; the
implementation must use round-trip formatting or the lossless claim is false.

### 9.2 Non-finite values (NaN, ±Inf)

Only representable in `float`, `double`, and `string`. Casting a non-finite value to any
integer or bool is **loss** (so `--dynamic` skips such a column; `--static` applies §10).
`double→float` on non-finite is treated as lossless with a documented caveat: `±Inf` is
exact, but **NaN payload/signaling bits do not survive** (52→23 mantissa bits, sNaN→qNaN,
distinct NaNs collapse). This canonicalization is accepted under D4a; it is the one
deviation from literal bit-exactness and is called out in `--help`/docs.

### 9.3 int64 / uint64 range test in the double domain

The range check for `double→int64`/`uint64` must **not** compare against
`static_cast<double>(INT64_MAX)` — that rounds up to `2⁶³ > INT64_MAX`, so a double of
exactly `2⁶³` would pass and then overflow (UB) on cast. The test must strictly exclude
values `≥ 2⁶³` (int64) and `≥ 2⁶⁴` (uint64). (Today's `coerceValue` has this latent bug;
the new code must not inherit it.)

### 9.4 Tolerance semantics and labeling

- **Absolute epsilon** (D4b). Relative tolerance is out of scope for v1.
- **Integer-rounding caveat:** `|v−round(v)| ≤ TOL` is only meaningful for `TOL < 0.5`. At
  `TOL ≥ 0.5` every finite value is within 0.5 of some integer, so the whole-number test
  becomes vacuous (only the range check bites). `TOL ≥ 0.5` is accepted but the tool warns
  that the wholeness test is disabled at that tolerance.
- **Honest labeling:** when `TOL > 0`, a cast that is not bit-exact must **not** be printed
  as plain "lossless"/"narrowed". Reports and JSON show `within tol=<TOL>` and echo the
  active tolerance in the header, so `--optimize --tolerance 1e-6` never masquerades as
  bit-exact.
- **Rounding:** `std::round` (half away from zero) is the named, deterministic mode for all
  float→int conversions (scan test and §10 force).

## 10. Force semantics (`--static` only)

`--static` produces output for every named column. Per non-representable cell, in order:

1. **float/double → int/uint:**
   - `NaN` → `0` (warn).
   - `+Inf` or value above target max → target **max** (warn).
   - `−Inf` or value below target min → target **min** (warn).
   - else → `std::round(v)`, then clamp to `[min, max]`.
2. **int/uint → narrower/different-sign int out of range** → clamp to `[min, max]`.
3. **any numeric → bool** → `false` iff the value is `0` within `TOL`, else `true`
   (`NaN` → `true`, since `NaN ≠ 0`).
4. **string → numeric/bool:** `strtod`; a finite parse follows rules 1–3; a non-finite
   parse (`"nan"`, `"inf"`) follows rule 1; an **unparseable** string → **hard error**
   (exit 1) — forcing cannot invent a number.
5. Each affected column is reported **once** with the count of clamped/rounded cells and a
   sample; see §11.2 for the exact format.

`--dynamic` never clamps: any column that would need rules 1–4 (including a string column
with any unparseable or non-finite cell) is skipped and keeps its original type.

## 11. Errors, exit codes, and reporting

### 11.1 Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success — including a no-op, and lossy `--static` (clamped) or `--dynamic` (skipped); loss is reported via stderr warnings, not the exit code |
| 1 | Runtime error (missing/invalid file, unparseable forced string, write failure) |
| 2 | Argument error (bad flags, bad SPEC, conflicts per §3.2) |

### 11.2 Human-readable output

- **Channel rule:** the plan table and `--json` go to **stdout**; progress (`-v`),
  warnings (clamp/skip notices), and errors go to **stderr**. (Lets `bcsvCast --scan ... >
  plan.txt` capture just the plan.)
- **Table:** per in-scope column — `Idx · Name · Original · Target · Note`, where Note ∈
  `(unchanged)`, `→ narrowed`, `→ narrowed (within tol=T)`, `skipped: <reason>` (dynamic),
  `forced: N clamped` (static). Footer: changed-column count, estimated flat-codec byte
  savings, and the active tolerance.
- **Clamp/skip warning format (stderr), one line per affected column:**
  `bcsvCast: col <idx> '<name>' <orig>-><target>: <N> cells clamped/rounded`
  and for dynamic: `bcsvCast: col <idx> '<name>' kept <orig> (skipped <target>: <N> lossy cells)`.
  (`clamped` counts cells whose stored value differs from the source by more than
  `--tolerance`, so a within-tolerance rounding under `--optimize`/`--dynamic` is not
  reported.)

### 11.3 JSON output (`--json`)

`--json` emits a single JSON object to stdout (nothing else on stdout; warnings stay on
stderr). `--scan --json` enumerates **all** in-scope columns (so an agent can build a
positional list). Schema:

```json
{
  "tool": "bcsvCast",
  "version": "1.5.9",
  "mode": "dynamic",
  "input": "data.bcsv",
  "output": null,
  "applied": false,
  "rows_scanned": 10000,
  "num_columns": 4,
  "tolerance": 0.0,
  "any_loss": true,
  "suggested_spec": "0=int32,1=uint16",
  "columns": [
    {"index": 0, "name": "id", "original": "int64", "target": "int32",
     "bytes_original": 8, "bytes_target": 4,
     "applied": true, "lossy": false, "clamped_cells": 0,
     "values_out_of_range": 0, "skipped": false, "reason": null},
    {"index": 3, "name": "temp", "original": "double", "target": "float",
     "bytes_original": 8, "bytes_target": 4,
     "applied": false, "lossy": true, "clamped_cells": 0,
     "values_out_of_range": 12, "skipped": true,
     "reason": "12 values exceed float32 within tol=0.0"}
  ]
}
```

- `suggested_spec` is a ready-to-copy map-form SPEC reproducing the plan.
- With an output path, `applied` becomes true and per-column `applied`/`clamped_cells`
  reflect what was written.
- Agents that only need the current schema (not a full scan) should use `bcsvHeader`
  (header-only, O(1) in rows) rather than `--scan` (O(rows)). Documented in `--help`.

## 12. Output, codecs, and safety

- **Apply always writes.** Any mode given an output path writes the output file, even when
  the plan is a pure no-op (the file is copied). This removes the current `bcsvNarrowType`
  surprise where "nothing to narrow" leaves a pipeline's expected output missing.
- **Value-preservation, not byte-identity.** A no-op or lossless apply guarantees the
  output *round-trips to the same values* (verified with `bcsvCompare --mode values`); it
  does **not** guarantee a byte-identical file (codec/packet state may differ).
- **Codec preservation:** output inherits the input's file codec, row codec, compression
  level, flags, and packet/block size (as `bcsvNarrowType` does today). No codec-override
  flags in v1.
- **`--in-place`:** temp file in the same directory, then atomic rename; temp cleaned up on
  failure (unchanged from today).
- **`--overwrite`:** required to replace an existing `OUTPUT_FILE`; input==output path is
  rejected with a hint to use `--in-place`.
- **Empty / zero-row / zero-column files:** plan is a no-op; `--scan` reports "nothing to
  change"; apply copies the file.

## 13. Worked examples

```bash
# Discover what would narrow (read-only), human + machine readable
bcsvCast --scan data.bcsv
bcsvCast --scan --json data.bcsv          # → suggested_spec for an agent to reuse

# Auto-narrow (both equivalent; default infers --optimize because an output is present)
bcsvCast --optimize data.bcsv narrow.bcsv
bcsvCast data.bcsv narrow.bcsv
bcsvCast --optimize --in-place data.bcsv

# Force specific columns, accepting loss (id → int32, temp → float32)
bcsvCast --static '0=int32,3=float' data.bcsv out.bcsv --overwrite

# Same, but skip any column that would lose data
bcsvCast --dynamic '0=int32,3=float' data.bcsv out.bcsv

# Full positional redefinition of an 8-column file
bcsvCast --static 'int32,uint64,bool,bool,string,float,float,float' in.bcsv out.bcsv
```

## 14. Backward compatibility — `bcsvNarrowType` removed (D5)

The `bcsvNarrowType` binary, its CMake target, and its help/docs are deleted in the same
change that adds `bcsvCast`. Old invocations map mechanically:

| Old (`bcsvNarrowType`) | New (`bcsvCast`) |
|------------------------|------------------|
| `bcsvNarrowType in.bcsv` | `bcsvCast in.bcsv` (default → `--scan`) |
| `bcsvNarrowType in.bcsv out.bcsv` | `bcsvCast in.bcsv out.bcsv` (default → `--optimize`) |
| `bcsvNarrowType --in-place in.bcsv` | `bcsvCast --optimize --in-place in.bcsv` |
| `--cols`, `-o`, `--overwrite`, `-v` | identical |
| `--stringsToValue` | `--string-to-value` (kebab-cased, matches `bcsvCompare`) |

Existing `bcsvNarrowType` tests retarget to `bcsvCast --optimize` as the narrowing
regression suite. `CHANGELOG.md` records the removal/rename as breaking; `CLI_TOOLS.md`
replaces the `bcsvNarrowType` section with `bcsvCast`.

## 15. Testing plan

### 15.1 Unit (GTest — new `tests/test_cast.cpp`, seeded from `test_narrow_type.cpp`)

- **Spec parser:** map, list, ranges, negative indices, aliases (incl. `ch`→uint8, case
  folding), braces present/absent, whitespace around `=`/`:`/`,`; errors: mixed forms,
  duplicate/overlapping keys, list-length mismatch (assert the helpful message), unknown
  type, out-of-range index, empty/trailing entry, VOID as source/target.
- **Loss detection** per §9 category incl. the correctness edge cases the review surfaced:
  int→float round-trip for exact large powers (2³⁰, 2⁶⁰); `double→int64` at exactly `2⁶³`
  (must be loss/clamp, not UB); NaN/±Inf into int/bool; `-0.0`; subnormals; TOL boundary
  (value at TOL, just over); TOL≥0.5 wholeness-test-disabled warning; numeric→string
  round-trip fidelity (17/9 digits).
- **Per-mode:** scan never writes; narrow reproduces today's results; dynamic skips exactly
  the lossy columns; static clamps and counts; default-mode inference (output→narrow,
  none→scan).
- **Force semantics:** 300→int8=127, −5→uint8=0, 3.7→int=4 (`std::round`: 2.5→3), NaN→int8=0,
  1e300→int32=INT32_MAX, unparseable string→exit 1.
- **Idempotence:** `--optimize` twice → identical types; applying a SPEC equal to current
  types → value-identical no-op that still writes a file.
- **Exit codes:** 0/1/2; lossy `--static`/`--dynamic` still exit 0 (loss reported on stderr).

### 15.2 Integration (pytest — extend into `tests/integration/test_cast.py`)

- Each mode end-to-end against `bcsvGenerator` profiles; values verified with
  `bcsvCompare --mode values` (and `--tolerance` where relaxed).
- Dry-run vs. apply; apply-always-writes (no-op still produces output); `--in-place`;
  `--overwrite`; input==output rejection.
- Conflict matrix from §3.2 all yield exit 2 with the specific message.
- Shell-quoting: unquoted `{...}` under fish/bash is caught (no file named `3=float`).
- `--json` shape: keys present, `suggested_spec` valid and reusable, `--scan --json`
  enumerates all columns, pure-JSON-on-stdout (warnings on stderr).
- `bcsvNarrowType` target no longer builds; `--optimize` reproduces its former results.

## 16. Decisions

### 16.1 Settled (first round, 2026-07-09)

| # | Decision | Outcome |
|---|----------|---------|
| D1 | CLI shape | Four verbs (`--scan/--optimize/--static/--dynamic`) |
| D2 | Spec separators | `=` assign, `:` range (`0=int32,7:8=float`) |
| D3 | Force overflow | Saturate/clamp + warn; unparseable string → error |
| D4a | Default tolerance | `0.0` (bit-exact) |
| D4b | Tolerance kind | Absolute epsilon |
| D5 | `bcsvNarrowType` fate | Removed now |
| D6 | Column addressing | Indices only (v1) |
| D7 | Type aliases | `int→int32`, `long→int64`, short forms; `ch/char/byte→uint8` |

### 16.2 Confirmed (v0.2 review round, 2026-07-09)

| # | Item | Outcome |
|---|------|---------|
| N1 | Default-mode + output path | Infer `--optimize` (bare `bcsvCast in out` converts) |
| N2 | `NaN → int` under `--static` | `NaN → 0` (warn) |
| N3 | `--error-on-loss` / exit 3 | **Dropped** — lossy static/dynamic exit 0; loss reported via stderr warnings |
| N4 | Flag rename | `--string-to-value` (kebab, matches `bcsvCompare`) |
| N5 | Apply on a no-op plan | Always write the output file (copy) |

All other v0.2 changes (loss-model correctness, NaN/Inf ordering, round-trip string
formatting, `std::round`, JSON enrichment, channel rules, VOID handling) are bug/clarity
fixes with no viable alternative and are treated as settled.

## 17. Out of scope (v1)

- Column add/drop/reorder/rename (types only).
- Codec/compression override on output.
- Column addressing by name.
- Relative tolerance.
- Per-column rounding-mode control.
- Textual `"true"/"false"` → bool parsing (numeric strings only in v1).
