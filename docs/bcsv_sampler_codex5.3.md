# BCSV Sampler Plan (Codex-5.3)

Status: Planning only (no implementation in this document)
Date: 2026-03-01
Audience: Internal maintainers + external expert reviewers
Scope anchor: ToDo Item 20 (+ CLI item 22: bcsvSampler)

---

## 1) Purpose and Intent

Define a precise, review-ready architecture and delivery plan for a **streaming Sampler** that:

- evaluates row-relative conditionals,
- projects selected fields into a derived output layout,
- operates with bounded memory on very large files,
- and is exposed consistently across C++, C, Python, C#, and CLI.

This plan intentionally separates:

1. **Architecture and decisions** (this document),
2. **Implementation work** (deferred until formal go-ahead).

---

## 2) Decisions Made in This Review Cycle

These decisions were explicitly selected and are now treated as baseline assumptions for v1.

### 2.1 Scope decisions (v1)

- Include:
  - conditional expressions,
  - selection expressions,
  - row-relative addressing,
  - wildcard row selection `X[0][*]`,
  - truncate boundary mode,
  - C++ / C / Python / C# / CLI surfaces.
- Defer to v2:
  - `expand` and `wrap` boundary modes.

### 2.2 Execution engine decision

- Use a **bytecode VM** (compile once, evaluate many rows).
- Avoid machine-code JIT in v1.

### 2.3 Boundary behavior decision (v1)

- Implement **truncate only**.
- Out-of-range relative references are excluded by current-row eligibility logic (not runtime UB).

### 2.4 Python output decision

- Primary streaming surface: **chunked iterator** yielding NumPy/Pandas batches.
- Full in-memory bulk remains optional convenience, not the default recommendation for large data.

### 2.5 Expression feature decisions (v1)

- Include bitwise operators in v1 (`~`, `&`, `|`, `^`, `<<`, `>>`).
- Include column-name indexing in v1 (`X[0]["temperature"]`), resolved at compile time.

### 2.6 Runtime arithmetic fault policy (v1)

- Runtime arithmetic faults use **skip-row** policy in v1.
- Faulted rows are not emitted; evaluation continues for subsequent rows.

---

## 3) Context and Constraints

### 3.1 Product constraints

- Streaming-first architecture is a core project principle.
- Existing reader/codec pipeline already dominates runtime cost in many realistic workloads.
- Zero external dependency philosophy should be preserved for core C++ path.

### 3.2 Performance and memory constraints

Target operating envelope:

- Input scale: 100+ GB BCSV files.
- System memory: as low as ~500 MB RAM.
- Requirement: bounded peak memory independent of file size.

Implication:

- Sampler must avoid all-rows materialization in its core path.
- Memory complexity target: `O(window_rows × row_size + output_chunk_size)`.

### 3.3 Cross-API consistency constraints

- Core semantics must be defined in C++ once and wrapped consistently.
- C API remains ABI-conscious and error-string based.
- Python and C# should not fork semantics; they should expose the same evaluator behavior.

---

## 4) What “Compilation” Means Here

Question addressed: does this require on-the-fly compilation?

Answer: **yes, but not machine-code JIT**.

Compilation model for v1:

1. Parse expression strings (conditional/selection) into AST.
2. Validate against source layout and syntax rules.
3. Lower AST to typed bytecode program.
4. Execute bytecode per row window in a lightweight VM.

This is effectively **runtime AOT-to-bytecode**, not LLVM-style native codegen.

Why this choice:

- keeps startup and packaging simple,
- avoids new hard dependencies,
- remains portable across C++, C API, Python wheels, and Unity/C# usage,
- still delivers compile-once/evaluate-many performance characteristics.

---

## 5) Approaches Considered and Tradeoffs

### Approach A — AST Interpreter

Pros:

- lowest implementation complexity,
- fastest to prototype and iterate grammar.

Cons:

- higher per-row dispatch overhead,
- lower throughput headroom for complex expressions.

### Approach B — Typed Bytecode VM (Selected)

Pros:

- good throughput/complexity balance,
- deterministic and portable,
- allows one-time validation and prebinding,
- naturally supports explain/disassembly/debug outputs.

Cons:

- more engineering than an AST walker,
- requires careful opcode/type design.

### Approach C — Native JIT (LLVM/libgccjit/etc.)

Pros:

- highest theoretical expression-eval performance.

Cons:

- high integration + maintenance burden,
- larger attack/safety surface (executable memory/codegen),
- packaging friction across Python and Unity,
- likely poor cost/benefit while I/O+codec remains dominant.

### Decision rationale

Selected Approach B because it maximizes practical throughput while remaining aligned with project constraints (streaming-first, low dependency, cross-platform wrappers).

---

## 6) Functional Specification (v1)

### 6.1 Input language — conditional

Supported operators:

- arithmetic: `+ - * / %`
- comparison: `> >= == != <= <`
- boolean: `&& ||`
- bitwise: `~ & | ^ << >>` (integer domain only)
- unary: `!` (logical not)
- grouping: `(...)`

Supported terminals:

- numeric literals: integer + float
- string literals: `"..."`
- row/column references:
  - `X[rel_row][col]`
  - `X[0][0]`, `X[-1][3]`, etc.
  - `X[0]["temperature"]` (column-name indexing)

### 6.2 Input language — selection

Selection is a comma-separated list of projection expressions:

- scalar references (e.g., `X[0][0]`),
- arithmetic expressions (e.g., `X[0][2] - X[-1][2]`),
- wildcard row expansion `X[0][*]`.

### 6.3 Relative indexing semantics (v1)

- `X[0]` = current row.
- `X[-k]` = k rows before current row.
- `X[+k]` = k rows after current row.

In **truncate mode**, a current row `i` is evaluable iff all referenced offsets are in bounds.

### 6.4 Output layout derivation

- Output schema is derived from selection expression list.
- For wildcard, columns are expanded in source-layout order.
- Expression result type follows type inference/coercion rules (see section 8).

---

## 7) Non-Goals for v1

- No `expand` mode.
- No `wrap` mode.
- No user-defined functions in expressions.
- No regex/string function DSL in v1 grammar.
- No machine-code JIT backend.
- No implicit full-file in-memory behavior in core sampler loop.

---

## 8) Type System and Coercion Rules (v1)

Define these rules explicitly to avoid cross-language divergence.

### 8.1 Primitive domains

- Bool
- Signed/unsigned integers
- Floating point (`float`, `double` promoted to `double` in VM arithmetic)
- String (UTF-8 byte-preserving compare)

### 8.2 Arithmetic coercion

- Numeric arithmetic permitted across integer/float; promote to `double` when mixed.
- String arithmetic is invalid.
- Division/modulo by zero: runtime arithmetic fault; row skipped per v1 policy.

### 8.3 Comparison coercion

- Numeric-to-numeric allowed with promotion.
- String-to-string lexicographic byte comparison.
- Cross-domain comparison (string vs numeric) is invalid in v1.

### 8.4 Boolean interpretation

- Bool literals/expressions are direct.
- Numeric in boolean context: `0 => false`, non-zero => true.
- String in boolean context: invalid in v1 (avoid ambiguous truthiness).

### 8.5 Bitwise operators

- Supported: `~`, `&`, `|`, `^`, `<<`, `>>`.
- Allowed for integer domain only.
- Invalid for float/string/bool.
- Shift counts are clamped/validated in VM runtime rules.

---

## 9) VM Architecture (Selected Approach)

### 9.1 Pipeline

`Tokenizer -> Parser -> AST -> Semantic Validator -> Bytecode Compiler -> VM`

### 9.2 Bytecode design goals

- Compact instruction format.
- Typed opcodes for hot operations to reduce runtime branching.
- Deterministic behavior across compilers and platforms.

### 9.3 Instruction families

- Load ops:
  - load const int/double/string,
  - load cell typed (`LOAD_I32`, `LOAD_F64`, `LOAD_STR`, etc.).
- Arithmetic ops: `ADD`, `SUB`, `MUL`, `DIV` (typed variants as needed).
- Arithmetic remainder op: `MOD` (integer domain).
- Bitwise ops: `BIT_NOT`, `BIT_AND`, `BIT_OR`, `BIT_XOR`, `BIT_SHL`, `BIT_SHR`.
- Compare ops: `CMP_EQ`, `CMP_LT`, etc.
- Bool ops: `AND`, `OR`, `NOT`.
- Projection ops for selection materialization.
- Control ops: `JMP`, `JMP_IF_FALSE`, `HALT`.

### 9.4 VM state

- Evaluation stack/register array.
- Program counter.
- Current window accessor.
- Error flag/message buffer.

### 9.5 Dispatch strategy

- Default portable dispatch: `switch`.
- Optional computed-goto optimization for GCC/Clang behind compile-time guard.
- Must preserve identical semantics regardless of dispatch backend.

### 9.6 Compile-time prebinding

During semantic validation/compile:

- resolve column indices,
- resolve column names to indices,
- verify column types,
- compute required min/max row offsets,
- precompute wildcard expansion list,
- build output layout once.

---

## 10) Streaming Runtime Model

### 10.1 Window requirements

Given expression references:

- `min_offset` (e.g., `-3`)
- `max_offset` (e.g., `+2`)

eligible current-row index interval in truncate mode:

- `start = -min_offset`
- `end = row_count - 1 - max_offset`

### 10.2 Memory model

- Keep bounded row window buffer for needed lookback/lookahead.
- No full-file row cache in core sampler path.
- Optional chunk buffer only for chunked API methods.

### 10.3 Edge handling (truncate)

- If current index is outside eligibility interval, row is skipped.
- Not treated as UB and not treated as hard stop.

### 10.4 Error policy (evaluation)

Two categories:

1. Compile-time errors (syntax/type/index invalid): fail `setConditional/setSelection`.
2. Runtime arithmetic faults (e.g., divide-by-zero/modulo-by-zero): row skipped + error accounting.

v1 behavior is fixed to skip-row (not fail-fast, not configurable).

---

## 11) Public API Contracts (Planned)

### 11.1 C++ API (proposed shape)

- `setConditional(string) -> bool + errMsg`
- `getConditional() const`
- `setSelection(string) -> bool + errMsg`
- `getSelection() const`
- `setMode(Mode)` / `getMode()` (v1 supports only `TRUNCATE`, others reserved)
- `next() -> bool`
- `row() -> const Row&`
- `bulk()` convenience

Notes:

- Keep API aligned with existing bool+error-message project style.
- Keep `Mode` enum extensible to add `EXPAND/WRAP` later without ABI break in C API wrappers.

### 11.2 C API

Add sampler opaque handle and lifecycle:

- create/destroy sampler
- set conditional / selection
- set/get mode
- next / row-get
- chunked bulk helper

Error reporting:

- use existing thread-local last-error channel conventions.

### 11.3 Python API

- `Sampler` iterator over selected rows.
- `iter_numpy(chunk_size=...)`
- `iter_pandas(chunk_size=...)`
- optional `bulk_*` convenience wrappers (warn on memory for large files).

### 11.4 C# API

- P/Invoke wrapper around C sampler handle.
- Iterator-like `Next()` and current-row access pattern.
- Optional chunk API for reduced per-row interop overhead.

### 11.5 CLI tool `bcsvSampler`

Primary use:

- read one BCSV,
- apply conditional + selection,
- write resulting BCSV.

CLI requirements:

- support stdin/stdout piping where feasible,
- explicit flags for conditional, selection, mode (`truncate` in v1),
- informative parser/type errors.

---

## 12) Compatibility and Versioning Strategy

- v1 introduces sampler APIs without changing existing reader/writer semantics.
- file format unchanged.
- C API additions are additive (no symbol removals).
- Python/C# wrappers should gate sampler features by native library capability when needed.

---

## 13) Testing and Validation Plan

### 13.1 Correctness tests

- parser unit tests: precedence, parentheses, literals, unary ops.
- semantic tests: invalid indices/types/operators.
- runtime tests: conditional truth table and projections.
- truncate boundary tests across min/max offsets.
- wildcard projection tests.

### 13.2 Cross-API parity tests

- same input + expression yields identical results in C++, C, Python, C#.

### 13.3 CLI tests

- file-to-file extraction.
- pipeline mode smoke tests.
- error message behavior.

### 13.4 Performance tests

- throughput against baseline reader loop.
- peak-memory tests with large synthetic datasets.
- expression complexity scaling tests (simple vs complex predicates).

### 13.5 Regression safeguards

- add representative datasets in test fixtures.
- require benchmark smoke checks for sampler changes.

---

## 14) Delivery Phases (No Coding Yet)

### Phase 0 — Specification freeze

- finalize grammar subset and type/coercion rules,
- finalize runtime error policy,
- finalize API signatures.

Gate: expert review approval on this plan.

### Phase 1 — C++ core

- tokenizer/parser/AST,
- semantic validator,
- bytecode compiler,
- VM evaluator,
- Sampler core class.

Gate: core correctness + memory bounds validated.

### Phase 2 — C API

- opaque sampler handle,
- additive APIs + error integration,
- C tests.

Gate: ABI-safe additive integration.

### Phase 3 — Python + C# wrappers

- Python Sampler + chunked numpy/pandas iterators,
- C# wrapper parity,
- language-level parity tests.

Gate: cross-language result parity + memory profile checks.

### Phase 4 — CLI `bcsvSampler`

- arg parsing,
- stream path,
- output writing + piping,
- CLI tests and docs.

Gate: end-to-end workflow validation.

### Phase 5 — Hardening

- performance tuning,
- diagnostics/disassembly output,
- finalize docs and examples.

Gate: external review closure + final approval.

---

## 15) Risks and Mitigations

### R1: Grammar creep before v1 completion

Mitigation:

- freeze minimal grammar now,
- track all additional operators/functions as v2 backlog.

### R2: Type coercion ambiguity across languages

Mitigation:

- strict v1 coercion matrix,
- reject ambiguous cross-domain operations.

### R3: Hidden memory growth in wrapper layers

Mitigation:

- make chunked APIs first-class,
- avoid implicit full materialization in default workflows.

### R4: Performance regressions from generic VM ops

Mitigation:

- typed opcodes and prebinding,
- targeted microbenchmarks in CI/perf workflow.

### R5: Boundary mode expansion complexity (future)

Mitigation:

- isolate boundary policy behind strategy interface now,
- implement only truncate in v1.

---

## 16) Open Questions for External Review

1. **String comparison semantics**: bytewise UTF-8 compare sufficient for v1, or require locale/collation hooks?
2. **Projection naming**: how should computed selection columns be named by default in output layout?
3. **Python defaults**: should DataFrame chunking be opt-in or the default for all `bulk`-style APIs?
4. **Dispatch backend**: include computed-goto optimization in v1 or postpone until after functional stabilization?

---

## 17) Proposal Summary to Proceed After Approval

If reviewers approve the architecture direction, proposed immediate next action is:

- lock a short **Sampler v1 Spec Addendum** (grammar + coercion + boundary + error policy),
- then start Phase 1 implementation in C++ core.

No coding is proposed before this approval gate.

---

## 18) Appendix A — Evidence Notes (high-level)

Brief side-study findings used to support decisions:

- Bytecode VM model is widely used in high-performance systems where compile-once/evaluate-many is needed.
- Computed-goto dispatch has documented speedups over central-switch dispatch on GCC/Clang class toolchains.
- Database systems with JIT show value primarily when expression execution dominates runtime; for BCSV, reader/codec often remains dominant.

Interpretation for BCSV:

- bytecode VM delivers strong practical value with controlled complexity,
- native JIT is a future optimization option, not a v1 necessity.

---

## 19) Appendix B — v2 Backlog (Explicitly Deferred)

- Boundary modes `EXPAND` and `WRAP`.
- User-defined functions in expressions.
- Optional native-JIT backend behind build flag.
- Advanced string functions/collation options.
- Additional CLI unix-tool style integrations beyond `bcsvSampler` core scope.
