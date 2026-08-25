# BCSV Lean Architecture Rules

Rules of thumb for keeping BCSV focused. Apply them while writing and reviewing;
there is no form to fill in. A **High** item is a blocker, everything else needs a
one-line rationale in the commit or PR note.

## Scope

- Change stays inside the requested scope, or the widening is stated explicitly.
- A new abstraction needs **2 current call sites** — not one plus a prediction.
- Performance work needs benchmark evidence, measured per the noise-floor rules in
  `benchmark/README.md`. An unmeasured optimisation is a guess with extra code.

## Single ownership (High)

Every piece of metadata or state has exactly one owner. The BCSV hotspots that have
actually bitten:

- Wire metadata (`rowHeaderSize`, `wireDataSize`, `wireStrgCount`, `wireFixedSize`)
- Column offsets / masks
- Row serialization context (flat / ZoH / delta)
- Defaults duplicated across language bindings — reference one named constant
  (`bcsv::DEFAULT_*`, `BcsvDefaults`, `pybcsv.DEFAULT_*`), never a literal. Level 1
  vs 6 drifted this way across five entry points.

A duplicated cache is allowed only with documented invalidation and a test.

## Layering

- `Row*` holds row state and access, not file or packet policy.
- `Reader`/`Writer` own stream and packet lifecycle, not encoding internals.
- Encoding lives behind the codec boundary.
- Public API stays stable unless a break is explicitly approved.

## Duplication

- Don't copy logic across dynamic/static/view variants without a reason. Hot-path
  specialisation is a reason — mark it and back it with a benchmark.
- Consolidate repeated `switch (ColumnType)` blocks when the behaviour matches.
- If the same fix has to land in **3+ places**, extract the helper first.

## Complexity

- No new mega-file without a split plan. Soft warning: file > 2500 lines, or one
  class carrying > 3 responsibilities.
- Keep critical logic reviewable on one screen where the shape allows.
- New template metaprogramming needs a measured benefit.

## Safety (High)

- Bounds-check every raw buffer read and write.
- No unaligned typed access in packed wire paths.
- Validation that runs *after* a stateful serializer has committed must poison or
  resync the writer — see `write_poisoned_`. Throw-after-commit is a bug pattern,
  not an error path.

## Before merge

- Debug + Release build clean; `bcsv_gtest` green.
- Subsystem tests for what you touched (C API, Python, C#/Unity, CLI).
- Benchmark smoke run for anything performance-sensitive.
- C API symbol changes versioned or aliased; binding and doc impact assessed.
