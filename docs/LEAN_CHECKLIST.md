# BCSV Lean Architecture Checklist

Use this checklist before implementation, during review, and before merge to keep BCSV focused and maintainable.

## How to Use (Quick)

1. **Before coding (3-5 min):** Fill sections A-C for the target item.
2. **During implementation:** Re-check D-F when adding files/functions.
3. **Before merge:** Validate G-H and record results in PR/commit notes.

Decision rule:
- **PASS:** no High items, and Medium items have explicit rationale.
- **BLOCK:** any unresolved High item.

---

## A) Scope Fit (requested item vs delivered change)

- [ ] Change is limited to the requested scope (or explicitly justified).
- [ ] New abstractions are introduced only if at least **2 current call sites** need them.
- [ ] No speculative layers for future work unless they reduce current duplication/bugs.
- [ ] Performance optimization is backed by benchmark/test evidence.

Evidence to capture:
- Item reference (e.g., ToDo id)
- Files changed
- Why each non-trivial new type/helper exists now

---

## B) Ownership Clarity (single source of truth)

- [ ] For each metadata/state concept, one owner is defined.
- [ ] No duplicated caches unless invalidation is clearly documented and tested.
- [ ] Data flow between producer/consumer is explicit (who computes, who consumes, who mutates).

Check these common BCSV hotspots:
- Wire metadata (`wireBitsSize`, `wireDataSize`, `wireStrgCount`, `wireFixedSize`)
- Column offsets / masks
- Row serialization context (flat/ZoH/delta)

---

## C) Layering and Responsibility

- [ ] `Row*` types focus on row state/access, not file/packet policy.
- [ ] `Reader/Writer` focus on stream/packet lifecycle, not encoding internals.
- [ ] Encoding logic lives behind a serializer boundary (or has a documented temporary location).
- [ ] Public API remains stable unless breaking change is explicitly approved.

---

## D) Duplication Budget

- [ ] New logic is not copy-pasted across dynamic/static/view variants without a reason.
- [ ] Repeated `switch(ColumnType)` blocks are consolidated when behavior is equivalent.
- [ ] If duplication is intentional (hot path specialization), it is marked and benchmark-justified.

Practical threshold:
- If the same bug fix must be applied in **3+ places**, extract shared helper/strategy.

---

## E) Complexity Budget

- [ ] No new mega-file growth without split plan.
- [ ] Function size remains reviewable (target: one screen for critical logic where possible).
- [ ] New template/metaprogramming is necessary for measured benefit.

Soft warning thresholds:
- File > 2500 lines
- Single class handling > 3 distinct responsibilities

---

## F) Safety and Correctness Guardrails

- [ ] Bounds checks are present at all raw buffer reads/writes.
- [ ] No UB-prone unaligned typed access in packed wire paths.
- [ ] Error messages remain actionable and consistent.
- [ ] Existing tests cover changed behavior and edge cases.

---

## G) Compatibility and Ecosystem Impact

- [ ] C API symbol changes are versioned or backward-compatible aliases are provided.
- [ ] Python/Unity/CLI implications are assessed for API or wire-format changes.
- [ ] Docs/examples updated when behavior or defaults change.

---

## H) Exit Criteria (merge readiness)

- [ ] Build passes (`Debug` + `Release`).
- [ ] `bcsv_gtest` passes.
- [ ] Relevant subsystem tests pass (`C API`, Python, Unity/CLI where applicable).
- [ ] Benchmark smoke test run for performance-sensitive changes.
- [ ] Checklist result summarized in commit/PR notes.

---

## Minimal Review Template (copy into PR/commit note)

```
Lean Checklist Summary
- Scope fit: PASS/FAIL
- Ownership clarity: PASS/FAIL
- Layering: PASS/FAIL
- Duplication budget: PASS/FAIL
- Complexity budget: PASS/FAIL
- Safety guardrails: PASS/FAIL
- Compatibility impact: PASS/FAIL

High risks:
1) ...
2) ...

Mitigations / follow-ups:
1) ...
2) ...
```
