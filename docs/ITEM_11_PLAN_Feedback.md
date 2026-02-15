# Feedback on Item 11 Plan (Serializer / Deserializer Extraction)

Date: 2026-02-14  
Reviewer: GitHub Copilot (GPT-5.3-Codex)

## 1) Overall assessment

The plan is strong, detailed, and implementation-oriented. It has clear checkpoints (A–E), explicit migration phases, and strong focus on parity and benchmark validation. The architecture direction is also sound: separating row data model concerns from wire-format concerns is the right long-term move, especially for Item 12 (CSV) and Item 14 (delta).

My critical feedback is mostly about **de-risking scope and sequencing** so the extraction does not become a long, fragile refactor.

## 2) What is especially good

- Clear architectural target state and rationale (Row/Reader/Writer ownership boundaries are well articulated).
- Good compatibility mindset: preserving public API while moving internals.
- Practical checkpoints with shippable states after each phase.
- Explicit risk table and interruption-recovery strategy.
- Correct focus on packet-boundary `reset()` semantics for future stateful encoders.

## 3) Critical concerns and suggested improvements

### A) Scope may still be too large for one item

The plan combines three major refactors:
1. Serialization extraction,
2. Reader/Writer migration,
3. RowView decoder extraction + Layout wire metadata cleanup.

These are each meaningful tasks on their own. Doing all in one pass increases blast radius.

**Recommendation:** split Item 11 into two internal milestones:
- **11a**: RowSerializer/RowDeserializer extraction + Reader/Writer migration (no RowView/Layout cleanup yet).
- **11b**: RowView `FlatDecoder` + Layout wire-metadata removal + dead code deletion.

This keeps the critical path shorter and improves revertability.

### B) “Byte-identical parity” can over-constrain future improvements

Byte-identical outputs are excellent for confidence, but they can become a trap if tiny harmless normalization changes occur.

**Recommendation:** define parity in two levels:
- **Strict parity mode** for migration tests (same input → same bytes) while legacy path still exists.
- **Semantic parity mode** (roundtrip equivalence + stable packet invariants) after old path removal.

This prevents blocking on non-functional byte deltas later.

### C) Serializer state model needs one explicit contract

Plan mentions `reset()` and ZoH context ownership, but not a formal lifecycle contract.

**Recommendation:** document and enforce invariants:
- `setup(layout)` must be called before first serialize/deserialize. (Setup and or Constructor)
- `reset()` must be called at packet start.
- Serializer is single-stream stateful (not thread-safe by default).
- Switching ZoH mode after setup is either forbidden or clearly defined. (Forbidden!)

A short state machine section in the plan would avoid subtle bugs.

### D) Access strategy to Row internals is undecided too late

The plan leaves a key access decision open (friend vs load API). Delaying this to Step 1.3 may cause rework.

**Recommendation:** choose now:
- Prefer a **narrow internal friend boundary** (`RowSerializer`/`RowDeserializer`) over broad mutable public accessors.
- If maintainability is preferred over friend usage, add minimal internal adapter methods in Row (not general-purpose mutation APIs).

This is foundational and should be fixed before coding starts.

### E) Layout metadata removal should happen only after migration settles

Removing wire metadata from Layout is architecturally correct, but high-risk if done too early because many call sites and assumptions may still exist.

**Recommendation:** gate metadata removal behind:
- full green tests,
- passing benchmark smoke,
- at least one dedicated static/dynamic layout compatibility test matrix.

Treat this as the final cleanup milestone, not part of the core extraction path.

### F) Performance acceptance criteria need tighter definitions

Current “±3% noise” target is reasonable, but could be noisy across machines/runs.

**Recommendation:** define reproducibility protocol:
- fixed benchmark size and iteration count,
- median of N runs (e.g., 5),
- compare only same build type + same machine profile,
- include p95 latency for microbench where applicable.

This avoids false regressions.

### G) Missing explicit compile-time budget checks

Template-heavy serializer extraction can increase compile time and binary size.

**Recommendation:** add acceptance checks:
- build time delta threshold,
- binary size diff for key executables,
- no meaningful increase in template instantiation hotspots (if measurable).

## 4) Suggested implementation order (revised)

1. **Prework (small):** finalize Row access strategy and serializer lifecycle contract.
2. **Phase A:** introduce `RowSerializer`/`RowDeserializer` flat path, keep old row methods active.
3. **Phase B:** add ZoH path and parity tests.
4. **Phase C:** migrate Writer/Reader to serializer layer.
5. **Stabilization gate:** run full tests + benchmark + quick perf sanity before cleanup.
6. **Phase D:** extract `FlatDecoder` for RowView/RowViewStatic.
7. **Phase E:** remove old row serialization methods + remove wire metadata from Layout.
8. **Closeout:** docs, checklist, ToDo update.

This sequencing keeps operational risk low and isolates breakpoints.

## 5) Additional tests I strongly recommend

- **Cross-implementation matrix:** Dynamic layout + Static layout × Flat + ZoH × packet boundary reset behavior.
- **Corruption resilience tests:** malformed row lengths / truncated packet rows in new deserializer path.
- **State reset tests:** ensure first row after `reset()` always serializes full context in ZoH mode.
- **Mixed workload tests:** alternating repeated and changing rows to validate repeat elision correctness.
- **Backward compatibility samples:** read a small corpus of old files (if available) as golden fixtures.

## 6) Questions to resolve before implementation

1. Should serializer/deserializer be strictly internal for Item 11, or do you want a semi-public extension seam already now for Item 12 prototypes?
2. Is the preferred Row access model `friend` (tight coupling, minimal API surface) or explicit internal adapter methods?
3. Do you want Item 11 split into 11a/11b in ToDo tracking, or kept as one item with internal checkpoints only?
4. For benchmark gating, should acceptance be absolute (`<=3%`) or confidence-based (median across repeated runs)?

## 7) Final verdict

The plan is high quality and directionally correct. With the sequencing and contract clarifications above, it should be implementable with lower risk and clearer acceptance criteria.
