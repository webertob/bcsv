# ADR-0009: Unified format-flexible file open in the C API

**Status:** Accepted — revised 2026-09-15 (owner call): the additive-only
decision of §7 is **superseded**; the legacy `bcsv_csv_*` family is **removed**
and the change rides the 2.0.0 MAJOR lane (see BREAKING CHANGES below).
**Date:** 2026-09-15

## Context

The C API grew two parallel function families: `bcsv_reader_*` / `bcsv_writer_*`
for `.bcsv` binary files and `bcsv_csv_reader_*` / `bcsv_csv_writer_*` for
`.csv` / `.csv.lz4` (ADR-0008). Users and binding authors must know the format
*before* they can even open a file. Verified state (1.5.22, 169 exports): the
row/layout accessor layer (~120 functions) is already format-agnostic — both
families hand out the same `bcsv_row_t`/`bcsv_layout_t` — so the real
duplication is 25 thin lifecycle functions, not the whole stack.

Two independent problems hide in that split:

1. **A silent-corruption hazard.** `bcsv_csv_reader_open` on a `.bcsv` binary
   file with `has_header=false` parses binary bytes as delimited text and
   yields garbage rows with no error (the header/column-count guard only runs
   when `has_header=true`). Format-by-caller-choice can be silently wrong;
   content detection is already the decided policy for CSV-vs-LZ4
   (ADR-0008 §2: readers decide by magic, never by filename).
2. **Binding surface.** C#/Python/C each re-express the parallel families.
   Measured: C# 166 DllImports; Python binds the C++ headers directly (the C
   API changes are invisible to it); most duplication bindings actually feel
   is in their wrapper classes, not the native layer.

Expert-panel consultation (API design, C/C++, Python, C#) established the
constraints this decision respects: exported C names are the ABI (the version
script exports `bcsv_*` by name only, and .NET `DllImport` binds by name —
changing an existing signature links silently against the wrong type), this
project's semver is fused to wire-format compatibility so an exported-function
*removal* has no honest release lane, and the magics are structurally disjoint
(`42 43 53 56` = "BCSV" vs `04 22 4D 18` = LZ4 frame).

## Decision

*(As originally accepted: additive only. The revised decision — see Revision
below — adds one new `_u` (unified) family of format-flexible handles and
removes the legacy `bcsv_csv_*` family; the legacy binary families stay
exported and unchanged.)*

1. `bcsv_format_t`: `BCSV_FORMAT_AUTO | BCSV_FORMAT_BCSV | BCSV_FORMAT_CSV |
   BCSV_FORMAT_CSV_LZ4`.
2. `bcsv_probe_format(path, &out)` — reports the magic class of an existing
   file (BCSV magic → `BCSV`; LZ4/sniffable frame → `CSV_LZ4`; else `CSV`).
   Reports classification only; it never validates.
3. Unified handles are *containers*: `bcsv_reader_create_u(void)` and
   `bcsv_writer_create_u(layout, row_codec)` allocate an inert container;
   `bcsv_reader_open_u(h, path, fmt, opts)` / `bcsv_writer_open_u(...)` resolve
   the format and construct the concrete C++ member (`ReaderDirectAccess` /
   `CsvReader`, `WriterHandle` / `CsvWriter`) inside it at open time. No C++
   class gains a virtual or a format parameter. The C++ layer is untouched.
   - **Reader AUTO** (the default): the BCSV magic bytes decide up front (the
     full binary header need not parse for the *classification* — bytes alone
     do); BCSV magic → binary reader with ADR-0001 errors, else the CSV
     reader, which self-sniffs LZ4 vs plain text (ADR-0008). Explicit `fmt`
     narrows and NEVER falls back: an explicit-format mismatch fails loudly
     with `bcsv_reader_error_msg_u` (explicit CSV on an LZ4-magic file, or
     explicit CSV_LZ4 without the magic, is an error). This closes hazard 1
     for every `_u` caller. Cost, accepted: a plain CSV whose header line
     *literally* starts with the four bytes `BCSV` is classified as binary
     and fails loudly — the fix is `fmt = BCSV_FORMAT_CSV`, which is exactly
     what the narrowing enum is for. A silently-garbage binary file is the
     worse failure; a loud misclassification of a pathological CSV is not.
   - **Writer AUTO**: formats have no magic to consult (the file is being
     created), so resolution is by extension: `.csv`/`.csv.lz4` → CSV/CSV_LZ4,
     anything else → `BCSV` (the documented default). Explicit `fmt` overrides;
     `CSV_LZ4` requires the LZ4 opts, which also make the extension irrelevant.
4. Format-specific knobs move into `bcsv_open_opts` (with `struct_size` for
   forward extension; pass NULL for today's defaults): CSV
   `layout`/`delimiter`/`decimal_sep`, CSV-reader `has_header`, CSV-writer
   `include_header`, LZ4 block-id/level, BCSV `compression`/`block_size_kb`/
   `file_flags`. Zero-valued-but-meaningful parameters are encoded as
   "unset" sentinels; C callers initialize via the provided `BCSV_OPEN_OPTS_INIT`.
   CSV opens with a NULL layout are rejected with an explicit reason (schema
   is required to parse text — it cannot be sniffed).
5. `bcsv_reader_format_u(h)` / `bcsv_writer_format_u(h)` report what AUTO
   resolved to. Per-row navigation (`*_u` next/row/index/layout/error_msg/…)
   dispatches on the container's kind tag — one load + branch, no virtuals,
   no registry lookup. Binary-only deep features (sampler, random `read`,
   `read_columns`, `count_rows`, `open_ex`) and CSV's `file_line` outside the
   unified reader stay where they are: the unified reader exposes them when
   they apply and 0/errors when they do not (e.g. `file_line_u` returns 0
   in BCSV mode).
6. **Family discipline, hardened at the point that matters.** Mixing handles
   across families was already undefined behavior (today `bcsv_reader_next`
   blind-casts any pointer); instead of adding per-row checks, every destroy
   entry point now *kind-checks* the handle registry: destroying a unified
   handle through a legacy name (or vice versa) becomes a logged no-op that
   leaves the handle alive for a correct destroy or `bcsv_shutdown()`. The
   registry already ran on this cold path — the hot navigation path is
   unchanged. Headers state the rule: use a `_u` handle with `_u` functions.
7. **The 25 `bcsv_csv_*` functions are demoted to LEGACY** — documented, not
   removed: C ABI removal has no release lane (see Context), and the NuGet/UPM
   wheels must be able to ship ahead of, or behind, managed recompile. They
   remain 1-line shims over the same members the `_u` family drives; C#/Unity
   re-point their P/Invokes to the `_u` family, after which binding work
   touches one open path for three formats. Python does not consume the C API;
   for it the same ergonomics ship as a pure-Python `pybcsv.open()` façade
   (no wheel rebuild), and C# gets `BcsvFile.OpenReader/Write` façades.
   *(Superseded by the 2026-09-15 revision below — the family is removed, and
   the C# half of this clause never shipped: see the managed-surface revision.
   The reasoning is kept as history because the C ABI facts it cites still hold;
   what changed is the owner's willingness to spend a MAJOR on them.)*

## Revision (owner call, 2026-09-15): legacy `bcsv_csv_*` family REMOVED

### Managed surface revision (same date, binding wave)

§7 promised C# `BcsvFile.OpenReader/Write` façades and this ADR's
Consequences projected "four wrapper classes → two façades + two concrete
classes". Neither shipped, by owner call: with the P/Invoke re-point done,
`BcsvCsvReader.Open()` / `BcsvCsvWriter.Open()` *are* the one-open-path
façade — the format sniff lives in native `open_u`, so a `BcsvFile` class
would add a name without capability and grow the managed surface against
the complexity-reduction mandate behind this trim. **Revised decision: the
managed surface is unchanged — the four classes keep their names; internals
unified onto the `_u` path (owner trim call 2026-09-15).** `SetLz4`'s public
signature survives by storing its parameters and carrying them in
`bcsv_open_opts.lz4_*` at open (equivalent to the retired native export per
§4). The Python `pybcsv.open()` façade is unaffected by this revision — it
was always pure-Python and did ship.

The pre-release review of the `_u` implementation concluded that §7's
"demote, never remove" position does not pay its rent:

- **No compatibility debt to buy.** `bcsv_csv_*` shipped in 1.5.x, is only a
  few releases old, and every first-party consumer (tests, benchmarks, C#,
  Unity, examples) is in-tree and re-pointed with the rename. C# was still
  legacy-only, so the "wheels ship ahead of managed recompile" argument
  argued for *eventual* removal, not against it. Two parallel CSV paths
  (shim + unified) double the surface the kind-switch and reuse bugs live in
  — exactly where B2/B3/M4 were found.
- **A MAJOR lane exists.** This library's semver has been fused to wire
  format, which is why §7 called removal "lane-less". The release plan now
  recognizes a 2.0.0 MAJOR for exported-API breaks; the *wire format stays
  untouched* (readers derive codecs from header fields that do not change —
  `resolveFileCodecId`/`resolveRowCodecId` take `fileMinor` as
  `[[maybe_unused]]` today, `include/bcsv/definitions.h`), so old files open
  with new libraries and vice versa. The honest cost is source-level for C
  callers, mechanical for managed bindings.

### BREAKING CHANGES — migration table

All 25 `bcsv_csv_reader_*` / `bcsv_csv_writer_*` exports and the
`bcsv_csv_reader_t` / `bcsv_csv_writer_t` typedefs are deleted. CSV opens go
through the existing `_u` family; ctor-frozen parameters (layout, delimiter,
decimal separator, LZ4 block-size/level) move into `bcsv_open_opts` carried by
`open_u`:

| removed | replacement |
|---|---|
| `bcsv_csv_reader_create(layout, d, s)` | `bcsv_reader_create_u()`; layout/delimiter/decimal_sep → `opts->csv_layout/.delimiter/.decimal_sep` |
| `bcsv_csv_reader_open(h, path, has_header)` | `bcsv_reader_open_u(h, path, BCSV_FORMAT_AUTO, &opts)` (`opts->has_header`; pin `BCSV_FORMAT_CSV`/`CSV_LZ4` to forbid sniffing) |
| `bcsv_csv_reader_is_open/next/row/index/file_line/layout/filename/error_msg/close/destroy` | same names + `_u` suffix on a `bcsv_reader_t` from `bcsv_reader_create_u` |
| `bcsv_csv_writer_create(layout, d, s)` | `bcsv_writer_create_u(layout, row_codec)`; delimiter/decimal_sep → `opts` |
| `bcsv_csv_writer_open(h, path, overwrite, include_header)` | `bcsv_writer_open_u(h, path, overwrite, BCSV_FORMAT_AUTO, &opts)` (`opts->include_header`) |
| `bcsv_csv_writer_set_lz4(id, level)` | `opts->lz4_block_id` / `opts->lz4_level` on every open (LZ4 tuning travels per open, never in the handle) |
| `bcsv_csv_writer_is_open/next/write/row/index/layout/filename/error_msg/close/destroy` | same names + `_u` suffix on a `bcsv_writer_t` from `bcsv_writer_create_u` |

After the removal the export surface is **173** functions (169 at v1.5.22,
+29 `_u` additions, −25 legacy CSV): below both the pre-ADR baseline and the
~195 the first version of this ADR predicted. The legacy *binary*
`bcsv_reader_*` / `bcsv_writer_*` family is untouched ABI. Passing a stale
`bcsv_csv_*` name to a new `.so` fails at load (unresolved symbol) — loud,
per the no-silent-ABI policy.

## Alternatives considered

- **Binding-layer façades only (no native change).** Cheapest and zero-risk —
  but every native consumer (e.g. any future C Rust/Java binding) keeps the
  format-blind APIs and the silent-garbage hazard stays in the native layer.
  Kept as the *companion* (the Python façade; on C# the re-pointed wrapper
  classes play that role), rejected as the whole answer.
- **Unified names reusing the legacy export names.** Impossible in C: names
  are the ABI, and an old binary silently passes an enum into a legacy call.
- **One `bcsv_file_t` replacing both handle types via type-erasure at create.**
  Rejected: pays ADR-0006's hot-path tax (per-call dispatch where the codebase
  deliberately wires function pointers once) for no extra user benefit once
  the `_u` family exists.
- **Python re-binding over the C API** (so unification would benefit it too):
  non-starter — Arrow C Data Interface, sampler policies and numpy zero-copy
  exist only in the C++ surface.

## Consequences

- **The exported count lands at 173** (169 at v1.5.22, +29 for the `_u`
  family, −25 for the removed legacy `bcsv_csv_*` family — see BREAKING
  CHANGES above). The first revision of this ADR projected 169 → ~195 under
  the additive-only rule; the superseding owner call made the surface net-smaller
  than its own pre-ADR shape while still adding format-flexibility. One open
  path serves three formats; the wrapper classes collapse (C#: 22 csv
  DllImports repointed — the four wrapper classes keep their public surface,
  internals unified onto `_u` per the managed-surface revision above; Python:
  one façade over existing classes — it binds the C++ headers directly and
  never saw the removal).
- Sniff fallback errors: an AUTO open resolves once (never tries-and-retries)
  and reports the decisive reason (per-handle `error_msg`, ADR-0001) naming
  the formats involved. No partial-success path.
- Row/layout handles obtained before an `open_u` that switched kinds are
  invalidated like any reopen; documented, bindings re-acquire after open.
- `bcsv_shutdown()`/registry grow two kinds; the CSV reader/writer kinds stay
  registered only as *borrowed-member* kinds (an adopted member impersonates
  its legacy shape for registry routing) — the owned-create paths died with
  the `bcsv_csv_*` family. Close-before-destroy phase order semantics don't
  change (unified writers close — LZ4 trailer included — in the same phase
  binary writers do).
- Versioning (as revised): exported-function **removal** rides the **2.0.0
  MAJOR** lane. Wire format untouched, so file-level compatibility is
  unaffected (`resolveFileCodecId`/`resolveRowCodecId` ignore `fileMinor`
  today — `[[maybe_unused]]`, `include/bcsv/definitions.h`): 1.x files read
  with 2.x and 2.x-written files read with 1.x (the `_u` API itself wrote
  nothing format-new). Managed bindings (C#/Unity) are rebuilt in the same
  2.0.0 wave; a 1.5.x-managed assembly against a 2.x `.so` fails loudly with
  missing-symbol errors, by design.
