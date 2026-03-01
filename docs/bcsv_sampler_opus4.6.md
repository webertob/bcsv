# BCSV Sampler — Implementation Plan

**Item 20 · Feature Request**
**Status:** Draft for external review
**Date:** 2026-03-01
**Author:** AI-assisted (GitHub Copilot / Claude Opus 4.6)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Requirements](#2-requirements)
3. [Architecture Decision: Three Approaches](#3-architecture-decision-three-approaches)
4. [Recommended Approach: Threaded Bytecode VM](#4-recommended-approach-threaded-bytecode-vm)
5. [Expression Language Specification](#5-expression-language-specification)
6. [Bytecode VM Design](#6-bytecode-vm-design)
7. [Sliding Window Architecture](#7-sliding-window-architecture)
8. [Output Layout Derivation](#8-output-layout-derivation)
9. [C++ API Design](#9-c-api-design)
10. [C API Design](#10-c-api-design)
11. [Python API Design](#11-python-api-design)
12. [C# API Design](#12-c-api-design)
13. [CLI Tool: bcsvSampler](#13-cli-tool-bcsvsampler)
14. [Implementation Phases](#14-implementation-phases)
15. [Testing Strategy](#15-testing-strategy)
16. [Performance Budget](#16-performance-budget)
17. [Risk Analysis](#17-risk-analysis)
18. [Open Questions — Resolved](#18-open-questions--resolved)

**Appendices:**
- [A: Lean Checklist Summary](#appendix-a-lean-checklist-summary)
- [B: Related ToDo Items](#appendix-b-related-todo-items)
- [C: References](#appendix-c-references)
- [D: Specification Test Vectors](#appendix-d-specification-test-vectors)

---

## 1. Executive Summary

The Sampler is a **streaming filter-and-project operator** for BCSV files. Given
a BCSV `Reader`, it evaluates a user-defined **conditional expression** on a
sliding window of rows and, when the condition is true, constructs an **output
row** from a user-defined **selection expression**. Both expressions reference
data via 2D indexing (`X[row_offset][column_index]`) and support C++-like
syntax (comparators, boolean logic, arithmetic, string literals).

The Sampler is designed for **streaming operation** on files far larger than RAM
(100+ GB on systems with 500 MB). Memory usage is bounded by a circular buffer
whose size equals the expression's maximum row offset window — typically a few
rows.

The core engine is implemented once in C++20 as a **type-specialized threaded
bytecode VM**. It is exposed through the C++ API, wrapped by the C API (opaque
handles), bound to Python (pybind11, with pandas/numpy `bulk()` integration),
exposed to C# (P/Invoke via the C API), and shipped as a CLI tool
(`bcsvSampler`).

---

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement |
|----|-------------|
| F1 | Accept conditional expressions as strings with C++-like syntax |
| F2 | Accept selection expressions as strings defining output columns |
| F3 | Support 2D indexing: `X[row_offset][col_index]`, row offset relative to current |
| F4 | Support wildcard column selection: `X[0][*]` (all columns of a row) |
| F5 | Support arithmetic operators: `+`, `-`, `*`, `/`, `%` |
| F6 | Support comparison operators: `>`, `>=`, `==`, `!=`, `<=`, `<` |
| F7 | Support boolean operators: `&&`, `\|\|` |
| F8 | Support negation: `!`, `~` (bitwise NOT) — **v1 locked** |
| F8b | Support bitwise operators: `&`, `\|`, `^`, `<<`, `>>` — **v1 locked** |
| F9 | Support parenthesised grouping |
| F10 | Support literal constants: integers, floats, strings (`"..."`) |
| F11 | Validate and "compile" expressions at `set` time; report errors before evaluation |
| F12 | Derive output `Layout` from selection expression and source `Layout` |
| F13 | Boundary modes: `TRUNCATE`, `EXPAND` |
| F14 | Streaming row-by-row iteration via `next()` / `row()` |
| F15 | Bulk extraction via `bulk()` returning all matching rows |
| F16 | Available on C++, C, Python, C# APIs |
| F17 | Python `bulk()` returns pandas DataFrame or numpy structured array |
| F18 | CLI tool `bcsvSampler` for file-to-file extraction with pipe support |

### 2.2 Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| N1 | Streaming: peak memory = O(window_size × row_size), never load entire file |
| N2 | Expression compilation < 1 ms for typical expressions |
| N3 | Expression evaluation must not be the throughput bottleneck (Reader/codec dominates) |
| N4 | Zero external dependencies (consistent with BCSV philosophy) |
| N5 | Standard hosted C++20 environment (Linux, macOS, Windows). Embedded/bare-metal targets are not required; the expression engine uses heap allocation and floating-point arithmetic. |
| N6 | Header-only C++ implementation (consistent with existing library structure) |
| N7 | Thread safety: Sampler instances are not shared; no internal locking needed |

### 2.3 Boundary Mode Semantics

| Mode | Behaviour at file edges |
|------|------------------------|
| **TRUNCATE** | Rows where any referenced offset falls outside the file are skipped. The output dataset is smaller than the input. First output row has index `max(0, -min_offset)`, last has index `row_count - 1 - max_offset`. |
| **EXPAND** | Missing rows at file boundaries are filled by replicating the nearest edge row. `X[-1]` before the first row uses a copy of the first row. `X[+2]` beyond the last row uses a copy of the last row. |

`WRAP` mode (out-of-bounds indices wrap modulo `row_count`) was considered and
rejected: it fundamentally conflicts with streaming semantics by requiring
either a full file pre-scan or random-access seeks to read the last row for
`X[-1]` at position 0. Architecturally incompatible with forward-only processing.

---

## 3. Architecture Decision: Three Approaches

Three implementation strategies were considered for the expression evaluation
engine. All three share the same front-end (Tokenizer → Parser → AST) and the
same Sampler API — they differ only in the back-end execution model.

### 3.1 Approach A: Switch-Dispatch Bytecode VM

**How it works:** The AST is compiled to a compact bytecode stream (opcode +
operands). At evaluation time, a `while(true) { switch(opcode) { ... } }` loop
interprets the bytecode.

| Criterion | Assessment |
|-----------|------------|
| Eval speed | ~20–60 M evals/sec (3-op conditional) |
| Compile latency | < 1 μs |
| External deps | None |
| Portability | All platforms, all compilers |
| Complexity | Low (~500 LOC for VM core) |
| Maintenance | Low |
| BCSV fit | Fully consistent |

**Pros:**
- Simplest implementation; well-understood pattern (SQLite, Lua, eBPF).
- Fully portable standard C++20.
- Trivially debuggable — bytecode can be disassembled and traced.

**Cons:**
- Central `switch` creates a single indirect branch that pollutes the CPU branch
  predictor. Each opcode shares the same predictor entry, reducing prediction
  accuracy.
- Bounds check per iteration (C standard requires it for `switch`).
- ~5–15 ns per opcode (vs. ~0.3 ns for native instructions).

### 3.2 Approach B: Threaded / Computed-Goto Bytecode VM with Type Specialisation

**How it works:** Same front-end and bytecode representation, but two
optimisations are applied:

1. **Computed goto** (`goto *dispatch_table[opcode]` via GCC's `&&label`
   extension) replaces the central `switch`. Each opcode gets its own indirect
   jump, allowing the CPU branch predictor to learn per-opcode patterns. The
   bounds check is eliminated.

2. **Type specialisation at compile time:** When the expression is compiled
   (at `setConditional` / `setSelection` time), the source `Layout` is known.
   The compiler resolves each cell reference `X[r][c]` to a concrete
   `ColumnType`. Instead of emitting a generic `LOAD_CELL` opcode that must
   dispatch on type at runtime, it emits a type-specific opcode
   (`LOAD_CELL_DOUBLE`, `LOAD_CELL_INT32`, etc.). This eliminates the inner
   type-dispatch `switch` from the hot loop entirely.

| Criterion | Assessment |
|-----------|------------|
| Eval speed | ~60–200 M evals/sec (3-op conditional) |
| Compile latency | < 1 μs |
| External deps | None |
| Portability | GCC/Clang native; MSVC fallback to switch |
| Complexity | Medium (~800 LOC for VM core) |
| Maintenance | Low–Medium |
| BCSV fit | Fully consistent |

**Pros:**
- 2–3× faster than switch dispatch (measured in CPython, LuaJIT interpreter,
  Go runtime, and the [Eli Bendersky benchmark](https://eli.thegreenplace.net/2012/07/12/computed-goto-for-efficient-dispatch-tables)
  showing 25% speedup on random opcodes, 15–20% in Python's VM).
- The type-specialisation win is independent of dispatch method — it eliminates
  the most expensive per-opcode branch (12-way type switch).
- Still pure C++20 with a GCC/Clang extension. MSVC automatically falls back to
  Approach A behaviour via `#ifdef`.
- Same sub-microsecond compilation; same debuggability (bytecode disassembly).
- Front-end is reusable: if a JIT back-end is ever needed, only the code
  generator changes.

**Cons:**
- Computed goto is not ISO C++ — but it is supported by GCC, Clang, and ICC
  (the three compilers BCSV targets for Linux/macOS). MSVC gets the `switch`
  fallback.
- Slightly more complex instruction dispatch than a simple switch (dispatch
  table of label pointers).

### 3.3 Approach C: Lightweight JIT Code Generation

**How it works:** Same front-end, but the back-end generates native x86_64 or
AArch64 machine code in memory using a JIT library. The compiled expression
becomes a native function pointer `bool(*)(const RowWindow&)`.

**Library candidates:**

| Library | Footprint | Platforms | Integration complexity |
|---------|-----------|-----------|------------------------|
| asmjit | ~300 KB source | x86_64 | Medium — emit individual instructions |
| xbyak | ~100 KB header-only | x86_64 | Medium — similar to asmjit |
| libgccjit | ~50 MB runtime | x86_64 + ARM | Low-level C API; requires libgccjit.so |
| LLVM ORC JIT | ~100 MB runtime | All major | High — most powerful, enormous dependency |
| MIR | ~30 KB | x86_64 + ARM | Low — compiles C-like IR |
| copy-and-patch | ~50 KB | x86_64 | Low — pre-compiled stencils, patched at runtime |

| Criterion | Assessment |
|-----------|------------|
| Eval speed | ~300 M – 1 B evals/sec |
| Compile latency | 10 μs (asmjit) – 500 ms (LLVM) |
| External deps | asmjit/xbyak/LLVM/etc. |
| Portability | x86_64 only (asmjit/xbyak); multi-arch with LLVM |
| Complexity | High (~2000+ LOC for code generator) |
| Maintenance | High |
| BCSV fit | Partially — breaks zero-dependency philosophy |

**Pros:**
- Maximum possible evaluation throughput — true native code.
- Compiler can optimise across the entire expression (register allocation,
  constant folding, dead code elimination).
- SIMD auto-vectorisation possible with LLVM.

**Cons:**
- Introduces external dependency — breaks BCSV's zero-dependency, header-only
  philosophy.
- Platform-specific: asmjit/xbyak are x86_64 only.
- Significant implementation and maintenance complexity.
- Security surface: executable memory pages (W^X policy conflicts).
- Build system complexity: CMake find-module, CI matrix explosion.
- Compilation latency (LLVM: 50–500 ms) is noticeable in interactive use.

### 3.4 Bottleneck Reality Check

Before choosing, we must consider where the **actual bottleneck** lies:

| Stage | Throughput | Notes |
|-------|-----------|-------|
| Disk I/O (NVMe) | ~3–5 GB/s | |
| Disk I/O (HDD) | ~100–200 MB/s | |
| LZ4 decompression | ~650 MB/s | |
| Row codec (Flat) | ~1 M rows/sec | 1000 columns, float |
| Row codec (ZoH/Delta) | ~200 K rows/sec | 1000 columns, float |
| **Approach A eval** | **~20–60 M evals/sec** | 3-op conditional |
| **Approach B eval** | **~60–200 M evals/sec** | 3-op conditional |
| **Approach C eval** | **~300 M+ evals/sec** | 3-op conditional |

Even Approach A is **100× faster than codec deserialization**. The expression
evaluator will almost never be the bottleneck — the Reader's I/O and codec work
dominate. JIT gains are real but unmeasurable in practice because the Sampler
spends its time waiting on `reader.readNext()`.

### 3.5 Decision: Approach B

**Recommended: Approach B (Threaded bytecode VM with type specialisation).**

Rationale:
1. The Reader is the bottleneck, not the evaluator — any approach is fast enough.
2. Zero dependencies — consistent with BCSV's header-only philosophy.
3. Type specialisation is the single biggest practical win (eliminates the 12-way
   column-type switch from the hot loop).
4. Computed goto is a well-understood, low-risk optimisation for GCC/Clang.
5. Front-end (Tokenizer → Parser → AST) is reusable — a JIT back-end can be
   added later behind a `BCSV_ENABLE_JIT` build flag without API changes.
6. Compilation in < 1 μs — no perceptible delay.

---

## 4. Recommended Approach: Threaded Bytecode VM

### 4.1 Pipeline Overview

```text
User string ─► Tokenizer ─► Parser ─► AST ─► TypeResolver(Layout)
                                                     │
                                      ┌──────────────┘
                                      ▼
                           BytecodeCompiler
                                      │
                       ┌──────────────┼──────────────┐
                       ▼              ▼              ▼
                  Conditional     Selection      OutputLayout
                  Bytecode        Bytecode       (derived)
                       │              │              │
                       └──────┬───────┘              │
                              ▼                      │
                    RowWindow (circular buffer)       │
                              │                      │
                         VM::eval()                   │
                              │                      │
                    ┌─────────┴─────────┐            │
                    ▼                   ▼            ▼
               bool result         OutputRow    OutputLayout
               (conditional)       (selection)  (for writer)
```

### 4.2 Component Decomposition

| Component | Responsibility | Estimated LOC | Header |
|-----------|---------------|---------------|--------|
| `Tokenizer` | Lexical analysis of expression strings | ~220 | `sampler_tokenizer.h` |
| `Parser` | Pratt parser producing AST | ~450 | `sampler_parser.h` |
| `AST` | Expression tree nodes with type annotations | ~150 | `sampler_ast.h` |
| `TypeResolver` | Resolve cell references (index + name) against Layout; infer types | ~250 | `sampler_types.h` |
| `BytecodeCompiler` | AST → typed bytecode stream (incl. short-circuit jumps) | ~350 | `sampler_compiler.h` |
| `VM` | Threaded bytecode interpreter | ~600 | `sampler_vm.h` / `.hpp` |
| `RowWindow` | Circular buffer of Row snapshots | ~200 | `sampler_window.h` |
| `Sampler` | Public API coordinating all components | ~300 | `sampler.h` / `.hpp` |
| `BoundaryStrategy` | TRUNCATE / EXPAND policy abstraction | ~30 | `sampler_window.h` |
| Error policy logic | `SamplerErrorPolicy` dispatch in VM | ~30 | `sampler_vm.h` |
| **Total** | | **~2580** | |

### 4.3 File Organisation

```text
include/bcsv/
├── sampler.h                  # Public Sampler class declaration
├── sampler.hpp                # Sampler implementation
├── sampler_tokenizer.h        # Tokenizer (declaration + impl, small)
├── sampler_parser.h           # Parser (declaration + impl, small)
├── sampler_ast.h              # AST node types
├── sampler_types.h            # TypeResolver
├── sampler_compiler.h         # BytecodeCompiler
├── sampler_vm.h               # VM declaration
└── sampler_vm.hpp             # VM implementation (threaded dispatch)
```

All files are header-only, following the existing `.h` / `.hpp` split convention.
The umbrella header `bcsv.h` will include `sampler.h`.

---

## 5. Expression Language Specification

### 5.1 Formal Grammar (EBNF)

```ebnf
(* ── Top-level ────────────────────────────────────────────────── *)
conditional   = bool_expr ;
selection     = select_item { "," select_item } ;

(* ── Boolean expressions (conditional) ────────────────────────── *)
bool_expr     = bool_or ;
bool_or       = bool_and  { "||" bool_and  } ;
bool_and      = bit_or    { "&&" bit_or    } ;
bit_or        = bit_xor   { "|"  bit_xor   } ;
bit_xor       = bit_and   { "^"  bit_and   } ;
bit_and       = bool_factor { "&" bool_factor } ;
bool_factor   = [ "!" | "~" ] bool_atom ;
bool_atom     = comparison
              | cell_ref                 (* standalone boolean column reference *)
              | bool_literal
              | "(" bool_expr ")" ;
(* Parser disambiguation: when a `cell_ref` appears in `bool_atom`,
   the parser first attempts to parse a `comparison` (cell_ref comp_op ...).
   Only if no comp_op follows does it fall back to the bare `cell_ref`
   production. In a Pratt parser this is natural: `cell_ref` binds as an
   atom, and the comparison operator is an infix with its own precedence. *)

(* ── Comparisons ─────────────────────────────────────────────── *)
comparison    = arith_expr comp_op arith_expr ;
comp_op       = "==" | "!=" | "<" | "<=" | ">" | ">=" ;

(* ── Arithmetic expressions ──────────────────────────────────── *)
arith_expr    = shift_expr { ("+" | "-") shift_expr } ;
shift_expr    = term { ("<<" | ">>") term } ;
term          = unary { ("*" | "/" | "%") unary } ;
unary         = [ "-" | "~" ] atom ;  (* ~ is bitwise NOT; type-checked to integer-only *)
atom          = cell_ref
              | literal
              | "(" arith_expr ")" ;

(* ── Cell references ─────────────────────────────────────────── *)
cell_ref      = "X" "[" row_offset "]" [ "[" col_spec "]" ] ;
row_offset    = integer ;               (* relative: 0=current, -1=prev, +3=future *)
col_spec      = integer | string_lit | "*" ;  (* index, name, or wildcard — column-name indexing is v1 locked *)

(* ── Selection items ─────────────────────────────────────────── *)
select_item   = arith_expr ;             (* may reference cells, constants, arithmetic *)

(* ── Literals ────────────────────────────────────────────────── *)
bool_literal  = "true" | "false" ;
literal       = bool_literal | integer | float_lit | string_lit ;
integer       = [ "-" ] digit { digit } ;
float_lit     = [ "-" ] digit { digit } "." digit { digit } [ ("e"|"E") ["+"|"-"] digit { digit } ] ;
string_lit    = '"' { any_char_except_quote | '\"' } '"' ;

(* ── Lexical ─────────────────────────────────────────────────── *)
digit         = "0" | "1" | ... | "9" ;
```

### 5.2 Operator Precedence (highest to lowest)

| Precedence | Operators | Associativity | Notes |
|:---:|---|---|---|
| 1 | `()` | — | Grouping |
| 2 | unary `-`, `!`, `~` | Right | Arithmetic / logical / bitwise negation |
| 3 | `*`, `/`, `%` | Left | Multiplication, division, modulo |
| 4 | `+`, `-` | Left | Addition, subtraction |
| 5 | `<<`, `>>` | Left | Bitwise shift |
| 6 | `<`, `<=`, `>`, `>=` | Left | Relational comparison |
| 7 | `==`, `!=` | Left | Equality |
| 8 | `&` | Left | Bitwise AND |
| 9 | `^` | Left | Bitwise XOR |
| 10 | `\|` | Left | Bitwise OR |
| 11 | `&&` | Left | Logical AND (short-circuit) |
| 12 | `\|\|` | Left | Logical OR (short-circuit) |

### 5.3 Type System

The expression language operates on **five runtime types**, directly mapping to
BCSV's type system:

| Expression Type | Maps to | Notes |
|-----------------|---------|-------|
| `Bool` | `bool` | Result of comparisons and boolean logic |
| `Integer` | `int64_t` | All BCSV integer types promoted to `int64_t` for arithmetic |
| `UInteger` | `uint64_t` | Unsigned integers; promoted from `uint8/16/32/64` |
| `Float` | `double` | `float` and `double` both promoted to `double` |
| `String` | `std::string` | Only `==` and `!=` comparisons supported |

**Type promotion rules:**
- `Integer` op `Float` → `Float` (widen to `double`)
- `UInteger` op `Integer` → `Integer` (widen to `int64_t`, preserving sign semantics)
- `UInteger` op `Float` → `Float`
- `Bool` used in arithmetic context → `Integer` (0 or 1)
- `String` in arithmetic context → **compile-time error**
- Mixed `String`/numeric comparison → **compile-time error**

These promotions are resolved during the `TypeResolver` phase, **before
bytecode generation**. The bytecode operates on already-promoted types.

**Numeric-in-boolean-context:** When a numeric value (`Integer`, `UInteger`,
or `Float`) appears as a bare `bool_atom` — i.e., a standalone cell reference
used as a conditional without an explicit comparison — C semantics apply:
`0` / `0.0` → `false`, non-zero → `true`. The compiler inserts an implicit
`CMP_NE_INT` (or `CMP_NE_FLOAT`) against a zero constant. This enables concise
conditionals such as `X[0]["flags"]` (true when non-zero) and aligns with
user expectations from C/C++/Python.

### 5.4 Cell Reference Semantics

`X[row_offset][col_index]`

- `row_offset`: signed integer relative to the "current" row being evaluated.
  - `0` = current row
  - `-1` = previous row
  - `+3` = 3 rows after current
- `col_index`: zero-based column index into the source Layout, or `*` for all columns.
- `X[row_offset]` without a column index is shorthand for the entire row — only
  valid in selection expressions (expands to `X[row_offset][0], X[row_offset][1], ..., X[row_offset][N-1]`).

**Column name references:**
`X[0]["temperature"]` references a column by name. The `TypeResolver` resolves
the string literal against `Layout::columnNames()` to a concrete column index
at compile time. This adds ~50 LOC to the tokenizer/parser and incurs zero
runtime cost — the bytecode contains the resolved integer index.

### 5.5 Example Expressions

**Conditionals:**

```text
X[0][0] != X[-1][0]                    # Edge detection: first column changed
X[0][3] > 100.0 && X[0][4] == "active" # Threshold + status filter
(X[0][0] - X[-1][0]) > 0.5            # Rate of change > 0.5
!(X[0][2] == X[-1][2])                 # Negated equality
X[0][0] > X[-2][0] + X[-1][0]         # Current > sum of previous two
X[0][0] % 100 == 0                     # Downsample: every 100th value
X[0]["flags"] & 0x04 != 0              # Bitwise flag extraction
X[0]["temperature"] > 50.0             # Column by name
```

**Selections:**

```text
X[0][0], X[0][1]                       # First two columns of current row
X[0][*]                                # All columns (identity projection)
X[0][0], X[-1][0]                      # Current and previous value of col 0
X[0][0], X[0][2] - X[-1][2]           # Col 0, and delta of col 2
X[-1][*], X[0][*]                      # Previous row + current row (doubled width)
```

---

## 6. Bytecode VM Design

### 6.1 Value Representation

The VM operates on a tagged value stack. Each stack slot is a **raw
discriminated union** (`SamplerValue`):

```cpp
struct SamplerValue {
    enum class Tag : uint8_t { BOOL, INT, UINT, FLOAT, STRING };
    Tag tag;
    union {
        bool     b;
        int64_t  i;
        uint64_t u;
        double   f;
        // String handled out-of-band (index into string pool)
    };
    uint16_t string_idx;  // valid when tag == STRING
};
```

**`std::variant` is explicitly rejected** for the value stack. In the hot
evaluation loop, every `std::visit` call compiles to a computed index dispatch
with bounds-checking and exception-handling overhead. A raw C union with an
explicit `Tag` enum eliminates this — the VM already knows the exact type of
each stack slot (resolved at bytecode compilation time), so the safety
guarantees of `std::variant` provide no benefit and incur measurable cost.

Stack depth is bounded by expression complexity (typical: 4–8 slots). The stack
is allocated once and reused across all row evaluations.

### 6.2 Instruction Set

Each instruction is a packed struct. Operands are encoded inline after the
opcode byte. The instruction stream is a `std::vector<uint8_t>`.

#### 6.2.1 Load Instructions (type-specialised)

These are emitted per-cell-reference with the concrete type resolved from the Layout:

| Opcode | Operands | Stack effect | Description |
|--------|----------|:---:|---|
| `LOAD_BOOL` | `row_off:i16, col:u16` | +1 | Push `X[row_off][col]` as Bool |
| `LOAD_INT8` | `row_off:i16, col:u16` | +1 | Push `X[row_off][col]` as Int (promoted from int8) |
| `LOAD_INT16` | `row_off:i16, col:u16` | +1 | Push as Int (promoted from int16) |
| `LOAD_INT32` | `row_off:i16, col:u16` | +1 | Push as Int (promoted from int32) |
| `LOAD_INT64` | `row_off:i16, col:u16` | +1 | Push as Int |
| `LOAD_UINT8` | `row_off:i16, col:u16` | +1 | Push as UInt (promoted from uint8) |
| `LOAD_UINT16` | `row_off:i16, col:u16` | +1 | Push as UInt (promoted from uint16) |
| `LOAD_UINT32` | `row_off:i16, col:u16` | +1 | Push as UInt (promoted from uint32) |
| `LOAD_UINT64` | `row_off:i16, col:u16` | +1 | Push as UInt |
| `LOAD_FLOAT` | `row_off:i16, col:u16` | +1 | Push as Float (promoted from float32) |
| `LOAD_DOUBLE` | `row_off:i16, col:u16` | +1 | Push as Float |
| `LOAD_STRING` | `row_off:i16, col:u16` | +1 | Push String reference |

Rationale for 12 type-specific load opcodes: eliminates the 12-way `ColumnType`
switch from the inner loop. The VM jumps directly to the handler that calls
`row.get<int32_t>(col)` — no runtime type decision.

#### 6.2.2 Literal / Constant Instructions

| Opcode | Operands | Stack effect | Description |
|--------|----------|:---:|---|
| `CONST_BOOL` | `val:u8` | +1 | Push boolean constant |
| `CONST_INT` | `val:i64` | +1 | Push integer constant |
| `CONST_UINT` | `val:u64` | +1 | Push unsigned integer constant |
| `CONST_FLOAT` | `val:f64` | +1 | Push float constant |
| `CONST_STRING` | `idx:u16` | +1 | Push string from constant pool |

#### 6.2.3 Arithmetic Instructions

| Opcode | Operands | Stack effect | Description |
|--------|----------|:---:|---|
| `ADD_INT` | — | -1 | `a + b` (int64) |
| `ADD_FLOAT` | — | -1 | `a + b` (double) |
| `SUB_INT` | — | -1 | `a - b` (int64) |
| `SUB_FLOAT` | — | -1 | `a - b` (double) |
| `MUL_INT` | — | -1 | `a * b` (int64) |
| `MUL_FLOAT` | — | -1 | `a * b` (double) |
| `DIV_INT` | — | -1 | `a / b` (int64, division by zero → runtime error per `SamplerErrorPolicy`) |
| `DIV_FLOAT` | — | -1 | `a / b` (double, div-by-zero produces ±Inf/NaN per IEEE 754) |
| `MOD_INT` | — | -1 | `a % b` (int64, division by zero → runtime error per `SamplerErrorPolicy`) |
| `NEG_INT` | — | 0 | `-a` (int64) |
| `NEG_FLOAT` | — | 0 | `-a` (double) |
| `BIT_AND` | — | -1 | `a & b` (int64) |
| `BIT_OR` | — | -1 | `a \| b` (int64) |
| `BIT_XOR` | — | -1 | `a ^ b` (int64) |
| `BIT_NOT` | — | 0 | `~a` (int64) |
| `BIT_SHL` | — | -1 | `a << b` (int64, b clamped to 0–63) |
| `BIT_SHR` | — | -1 | `a >> b` (int64, arithmetic shift, b clamped to 0–63) |

Modulo is integer-only; `float % float` is a compile-time error (use `fmod` as
a future built-in function if needed).

Bitwise operators are integer-only; applying them to Float or String is a
compile-time error. Bool operands are promoted to Integer via
`PROMOTE_BOOL_TO_INT` before bitwise operations.

**Unsigned integer arithmetic:** The VM does not have separate UINT arithmetic
opcodes. When both operands are `UInteger`, the compiler emits a
`PROMOTE_UINT_TO_INT` for each operand and uses `*_INT` opcodes. This mirrors
the type promotion rules (Section 5.3) and keeps the opcode set small. For
values exceeding `INT64_MAX`, this promotion loses the upper range — see
Open Question #10 for the design trade-off discussion. If profiling reveals
that unsigned-preserving arithmetic is critical, `ADD_UINT` / `SUB_UINT` /
`MUL_UINT` / `DIV_UINT` opcodes can be added without breaking the bytecode
format.

Arithmetic is type-specialised: the compiler emits `ADD_INT` or `ADD_FLOAT`
based on the resolved types of the operands. No runtime type checking.

#### 6.2.4 Type Promotion Instructions

| Opcode | Operands | Stack effect | Description |
|--------|----------|:---:|---|
| `PROMOTE_INT_TO_FLOAT` | — | 0 | Convert top-of-stack Int → Float (`static_cast<double>`) |
| `PROMOTE_UINT_TO_INT` | — | 0 | Convert top-of-stack UInt → Int (`static_cast<int64_t>`) |
| `PROMOTE_UINT_TO_FLOAT` | — | 0 | Convert top-of-stack UInt → Float (`static_cast<double>`) |
| `PROMOTE_BOOL_TO_INT` | — | 0 | Convert top-of-stack Bool → Int (0 or 1) |

The compiler inserts promotion instructions **before** arithmetic or comparison
instructions when operand types differ. For example, `X[0][int_col] + X[0][float_col]`
compiles to: `LOAD_INT32 ..., PROMOTE_INT_TO_FLOAT, LOAD_DOUBLE ..., ADD_FLOAT`.

#### 6.2.5 Comparison Instructions

| Opcode | Operands | Stack effect | Description |
|--------|----------|:---:|---|
| `CMP_EQ_INT` | — | -1 | `a == b` → Bool |
| `CMP_EQ_FLOAT` | — | -1 | `a == b` → Bool |
| `CMP_EQ_STRING` | — | -1 | `a == b` → Bool |
| `CMP_NE_INT` | — | -1 | `a != b` → Bool |
| `CMP_NE_FLOAT` | — | -1 | `a != b` → Bool |
| `CMP_NE_STRING` | — | -1 | `a != b` → Bool |
| `CMP_LT_INT` | — | -1 | `a < b` → Bool |
| `CMP_LT_FLOAT` | — | -1 | `a < b` → Bool |
| `CMP_LE_INT` | — | -1 | `a <= b` → Bool |
| `CMP_LE_FLOAT` | — | -1 | `a <= b` → Bool |
| `CMP_GT_INT` | — | -1 | `a > b` → Bool |
| `CMP_GT_FLOAT` | — | -1 | `a > b` → Bool |
| `CMP_GE_INT` | — | -1 | `a >= b` → Bool |
| `CMP_GE_FLOAT` | — | -1 | `a >= b` → Bool |

String comparisons are limited to `==` and `!=`. Attempting `<`, `>` etc. on
strings causes a compile-time error in the `TypeResolver`.

#### 6.2.6 Boolean / Control Instructions

| Opcode | Operands | Stack effect | Description |
|--------|----------|:---:|---|
| `POP` | — | -1 | Discard top of stack (used after peek-based conditional jumps) |
| `NOT` | — | 0 | `!a` (bool) |
| `AND` | — | -1 | `a && b` (bool) — used after both operands are evaluated |
| `OR` | — | -1 | `a \|\| b` (bool) — used after both operands are evaluated |
| `JUMP_IF_FALSE` | `offset:i16` | 0 | Peek top; if false, jump `offset` bytes forward (short-circuit `&&`). Value stays on stack. |
| `JUMP_IF_TRUE` | `offset:i16` | 0 | Peek top; if true, jump `offset` bytes forward (short-circuit `\|\|`). Value stays on stack. |
| `HALT_COND` | — | -1 | Pop top of stack as bool → conditional result |
| `EMIT` | `out_col:u16` | -1 | Pop top → write to output row at `out_col` |
| `HALT_SEL` | — | 0 | Selection program complete |

**Short-circuit evaluation:** The VM implements short-circuit semantics for
`&&` and `||` using conditional jump instructions. This is a **correctness
requirement**, not merely a performance choice: without short-circuit, an
expression like `(X[0][0] != 0) && (X[0][1] / X[0][0] > 5)` would evaluate
the right-hand side unconditionally, triggering a division-by-zero crash.

The compiler emits `JUMP_IF_FALSE` for `&&` and `JUMP_IF_TRUE` for `||`:

```text
# Compilation of: A && B
<compile A>                   ; stack: [A]
JUMP_IF_FALSE skip_B          ; peek A; if false, jump (A stays on stack as result)
POP                           ; discard A (it was true, B determines result)
<compile B>                   ; stack: [B]
skip_B:                       ; stack top is the result (A's false or B's value)
```

`JUMP_IF_FALSE` / `JUMP_IF_TRUE` use **peek semantics** (stack effect 0):
the top-of-stack value is inspected but not removed. On the taken path the
value remains as the short-circuit result. On the fall-through path a `POP`
discards it before evaluating the second operand. This matches the JVM
(`ifeq`) and CPython (`JUMP_IF_FALSE_OR_POP`) approach and avoids needing
a secondary jump or re-push.

The `AND` / `OR` opcodes remain available for cases where both operands are
already on the stack (e.g., optimiser may use them when side-effect-freedom is
proven), but the default compilation strategy uses conditional jumps.

### 6.3 Dispatch Mechanism

The **primary dispatch path** is a standard `switch` statement — portable,
debugger-friendly, and correct on all compilers (GCC, Clang, MSVC). An
optional **computed-goto acceleration** is enabled automatically on GCC/Clang
via `#ifdef`. This is a measured optimisation (typically 15–25% throughput
improvement) but not a correctness requirement. All testing and CI must pass
with `switch` dispatch; computed goto is a bonus.

```cpp
// Computed goto (GCC / Clang) — optional acceleration
#if defined(__GNUC__) || defined(__clang__)
  #define BCSV_USE_COMPUTED_GOTO 1
#else
  #define BCSV_USE_COMPUTED_GOTO 0
#endif

#if BCSV_USE_COMPUTED_GOTO
  // One label per opcode
  static constexpr void* dispatch_table[] = {
      &&op_load_bool, &&op_load_int8, &&op_load_int16, /* ... */
  };
  #define DISPATCH() goto *dispatch_table[*ip++]
  #define CASE(op)   op_##op:
#else
  // Primary path: standard switch (portable)
  #define DISPATCH() continue
  #define CASE(op)   case Opcode::op:
#endif
```

### 6.4 Bytecode Compilation Example

Expression: `X[0][0] != X[-1][0]`

Source Layout column 0: `ColumnType::DOUBLE`

**Compiled bytecode:**

```text
LOAD_DOUBLE   row_off=0,  col=0    ; push X[0][0] as double
LOAD_DOUBLE   row_off=-1, col=0    ; push X[-1][0] as double
CMP_NE_FLOAT                       ; pop two doubles, push bool
HALT_COND                           ; return top of stack as conditional result
```

Total: 4 instructions, 4 + 4 + 1 + 1 = 10 bytes of bytecode.
Evaluation: 4 indirect jumps + 2 `row.get<double>()` calls + 1 `double` comparison.

---

## 7. Sliding Window Architecture

### 7.1 Circular Row Buffer

```text
┌─────────────────────────────────────────────────────┐
│                  RowWindow                           │
│                                                     │
│  Capacity = lookbehind + 1 + lookahead              │
│           = |min_row_offset| + 1 + max_row_offset   │
│                                                     │
│  ┌───────┬───────┬───────┬───────┬───────┐          │
│  │ Row-2 │ Row-1 │ Row 0 │ Row+1 │ Row+2 │  (slots) │
│  └───────┴───────┴───────┴───────┴───────┘          │
│     ↑                ↑                               │
│  oldest          current_idx                         │
│                                                     │
│  Implemented as std::vector<Row>, circular access    │
│  via modular indexing:                               │
│    slot(row_offset) = (cursor_ + row_offset) % cap   │
└─────────────────────────────────────────────────────┘
```

### 7.2 Window sizing

The `TypeResolver` traverses the AST and collects all `X[offset][col]`
references. From these:

```text
min_offset = min(all row offsets)   // e.g., -2
max_offset = max(all row offsets)   // e.g., +3
lookbehind = |min_offset|           // e.g., 2
lookahead  = max_offset             // e.g., 3
capacity   = lookbehind + 1 + lookahead  // e.g., 6
```

The window is allocated once during `setConditional()` / `setSelection()` and
reused for the lifetime of the Sampler.

### 7.2.1 Boundary Strategy Interface

The boundary mode (TRUNCATE / EXPAND) is implemented behind an internal
strategy interface to isolate the policy from the core evaluation loop:

```cpp
class BoundaryStrategy {
public:
    virtual ~BoundaryStrategy() = default;
    // Returns true if the row at `row_offset` is available.
    // For TRUNCATE: returns false for out-of-bounds offsets.
    // For EXPAND: returns the edge row for out-of-bounds offsets.
    virtual const Row& resolve(const RowWindow& window,
                               int16_t row_offset) const = 0;
    virtual bool        skip(const RowWindow& window,
                             int16_t min_offset,
                             int16_t max_offset) const = 0;
};
```

This adds ~30 LOC but ensures that adding future boundary modes (or
user-defined strategies) requires only a new `BoundaryStrategy`
implementation — no changes to `Sampler`, `RowWindow`, or the VM.

### 7.3 Streaming Protocol

```text
                                READER
                                  │
                      ┌───────────┴───────────┐
                      │ reader.readNext()      │
                      │     ↓                  │
                      │ row → window.push()    │
                      │     ↓                  │
                      │ window.ready()?        │──── No ──→ (loop back, read more)
                      │     ↓ Yes              │
                      │ vm.evalConditional()   │
                      │     ↓                  │
                      │ true?                  │──── No ──→ (advance cursor, loop)
                      │     ↓ Yes              │
                      │ vm.evalSelection()     │
                      │     ↓                  │
                      │ output_row_ populated  │
                      │     ↓                  │
                      │ return true            │
                      └────────────────────────┘
```

**Filling phase:** For `lookahead > 0`, the Sampler must buffer `lookahead`
future rows before it can evaluate the first row. `next()` reads ahead silently
until the window is full.

**Draining phase:** When the Reader reaches EOF, there are `lookbehind` rows in
the window that haven't been "current" yet (if `lookahead > 0`, conversely). The
Sampler continues evaluating these, with boundary mode determining what happens
to out-of-bounds references.

### 7.4 Memory Budget

For a file with 1000 `float` columns:
- Row size ≈ 4000 bytes (data) + overhead ≈ ~5 KB
- Window capacity of 6 (typical) = 30 KB
- Plus VM state (stack, bytecode, constants) ≈ ~1 KB
- **Total Sampler memory: ~31 KB** — trivial even on 500 MB systems

For extreme expressions with `X[-100][0]` (100-row lookbehind):
- 101 rows × 5 KB = ~500 KB — still trivial

### 7.5 Row Snapshots

The Reader's `row()` returns a `const Row&` that is **mutated in-place** on
each `readNext()`. The RowWindow must therefore **copy** each row into its
buffer slot. This copy is:

- `bits_`: `Bitset<>` copy (typically < 128 bytes)
- `data_`: `vector<byte>` copy (typically 4–40 KB for 1000 channels)
- `strg_`: `vector<string>` copy (only for string columns)

For files without string columns, the copy is two `memcpy`-equivalent
operations. This cost is amortised against the Reader's decompression work.

**Optimisation:** The RowWindow allocates all Row objects upfront (sharing the
same Layout). On `push()`, it uses `Row::operator=(const Row&)` which reuses
existing allocations. After the first full cycle of the circular buffer, no
heap allocations occur.

---

## 8. Output Layout Derivation

### 8.1 Algorithm

When `setSelection()` is called, the selection string is parsed and the
`TypeResolver` maps each selection term to a concrete output column:

1. Parse selection: `"X[0][0], X[-1][0], X[0][1], X[0][2] - X[-1][2]"`

2. For each term:
   - **Simple cell ref** (`X[0][0]`): output type = source column type.
     Output name = `"col0"` (or source column name, if available).
   - **Arithmetic expression** (`X[0][2] - X[-1][2]`): output type = result type
     after promotion. Output name = auto-generated (e.g., `"expr3"`).
   - **Wildcard** (`X[0][*]`): expanded to N terms, one per source column.
     Output types = source column types. Output names = source column names.

3. Build output `Layout` with derived columns.

### 8.2 Example

Source Layout: `<uint16_t, string, double, float>` (columns 0–3)

Selection: `"X[0][0], X[-1][0], X[0][1], X[0][2] - X[-1][2]"`

| Selection term | Source type(s) | Output type | Output name |
|----------------|---------------|-------------|-------------|
| `X[0][0]` | `uint16_t` (col 0) | `uint16_t` | `col0` |
| `X[-1][0]` | `uint16_t` (col 0) | `uint16_t` | `col0_m1` |
| `X[0][1]` | `string` (col 1) | `string` | `col1` |
| `X[0][2] - X[-1][2]` | `double - double` | `double` | `expr3` |

Output Layout: `<uint16_t, uint16_t, string, double>`

### 8.3 Output Column Naming

Auto-generated names follow a deterministic scheme:

- Direct cell ref `X[r][c]`: uses source column name + offset suffix
  - `X[0][c]` → source name (e.g., `"temperature"`)
  - `X[-1][c]` → source name + `"_m1"` (e.g., `"temperature_m1"`)
  - `X[+2][c]` → source name + `"_p2"` (e.g., `"temperature_p2"`)
- Arithmetic expression: `"expr" + output_column_index` (e.g., `"expr3"`)
- Wildcard `X[r][*]`: expands using the naming rules above for each column

Users who need custom names can modify the output Layout after construction.

---

## 9. C++ API Design

### 9.1 Public Interface

```cpp
namespace bcsv {

    enum class SamplerMode : uint8_t {
        TRUNCATE,   // Skip rows where window references fall outside file
        EXPAND,     // Replicate edge rows for out-of-bounds references
    };

    enum class SamplerErrorPolicy : uint8_t {
        FAIL_ON_ERROR,  // Abort iteration on first runtime error (default)
        SKIP_ROW,       // Skip the offending row and continue to the next
    };

    struct SamplerCompileResult {
        bool        success;
        std::string error_msg;      // Empty on success; human-readable on failure
        size_t      error_position; // Character offset in the expression string
    };

    template<LayoutConcept LayoutType = Layout>
    class Sampler {
    public:
        using RowType = typename LayoutType::RowType;

        // ── Construction ────────────────────────────────────────────
        explicit Sampler(Reader<LayoutType>& reader);
        ~Sampler();

        // Non-copyable, movable
        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;
        Sampler(Sampler&&) noexcept;
        Sampler& operator=(Sampler&&) noexcept;

        // ── Configuration ───────────────────────────────────────────
        SamplerCompileResult    setConditional(std::string_view conditional);
        const std::string&      getConditional() const;

        SamplerCompileResult    setSelection(std::string_view selection);
        const std::string&      getSelection() const;

        void                    setMode(SamplerMode mode);
        SamplerMode             getMode() const;

        void                    setErrorPolicy(SamplerErrorPolicy policy);
        SamplerErrorPolicy      getErrorPolicy() const;

        // ── Output Schema ───────────────────────────────────────────
        const Layout&           outputLayout() const;

        // ── Iteration ───────────────────────────────────────────────
        bool                    next();         // Advance to next matching row
        const Row&              row() const;    // Access current output row
        size_t                  sourceRowPos() const;  // Position in source file

        // ── Bulk Extraction ─────────────────────────────────────────
        // WARNING: Materialises ALL remaining matches into memory.
        // For large result sets, prefer chunked iteration via next().
        std::vector<Row>        bulk();

        // ── Diagnostics ─────────────────────────────────────────────
        std::string             disassemble() const;  // Human-readable bytecode dump
        size_t                  windowCapacity() const;
    };

} // namespace bcsv
```

### 9.2 Usage Example

```cpp
bcsv::Reader<bcsv::Layout> reader;
reader.open("sensors.bcsv");

bcsv::Sampler sampler(reader);
sampler.setMode(bcsv::SamplerMode::TRUNCATE);

auto cond = sampler.setConditional("X[0][0] != X[-1][0]");
if (!cond.success) {
    std::cerr << "Conditional error at pos " << cond.error_position
              << ": " << cond.error_msg << "\n";
    return 1;
}

auto sel = sampler.setSelection("X[0][0], X[0][1]");
if (!sel.success) { /* handle error */ }

// Stream rows
while (sampler.next()) {
    const auto& out = sampler.row();
    // out has 2 columns matching the selection
    std::cout << out << "\n";
}
```

### 9.3 Template Considerations

The Sampler is templated on `LayoutType` for compatibility with the rest of
BCSV's generic architecture. However, the expression language operates on
**dynamic types** (resolved at setConditional/setSelection time), so `LayoutStatic`
gains no compile-time advantage inside the VM. The primary use case is
`Sampler<Layout>` (dynamic layout).

---

## 10. C API Design

```c
// ── Opaque handle ──────────────────────────────────────────────
typedef void* bcsv_sampler_t;

// ── Lifecycle ──────────────────────────────────────────────────
bcsv_sampler_t  bcsv_sampler_create     (bcsv_reader_t reader);
void            bcsv_sampler_destroy    (bcsv_sampler_t sampler);

// ── Configuration ──────────────────────────────────────────────
typedef enum {
    BCSV_SAMPLER_TRUNCATE = 0,
    BCSV_SAMPLER_EXPAND   = 1,
} bcsv_sampler_mode_t;

bool            bcsv_sampler_set_conditional(bcsv_sampler_t s, const char* expr);
const char*     bcsv_sampler_get_conditional(bcsv_sampler_t s);
bool            bcsv_sampler_set_selection  (bcsv_sampler_t s, const char* expr);
const char*     bcsv_sampler_get_selection  (bcsv_sampler_t s);
void            bcsv_sampler_set_mode       (bcsv_sampler_t s, bcsv_sampler_mode_t mode);
bcsv_sampler_mode_t bcsv_sampler_get_mode   (bcsv_sampler_t s);

typedef enum {
    BCSV_SAMPLER_FAIL_ON_ERROR = 0,
    BCSV_SAMPLER_SKIP_ROW      = 1,
} bcsv_sampler_error_policy_t;

void            bcsv_sampler_set_error_policy(bcsv_sampler_t s, bcsv_sampler_error_policy_t p);
bcsv_sampler_error_policy_t bcsv_sampler_get_error_policy(bcsv_sampler_t s);

// ── Output Schema ──────────────────────────────────────────────
const_bcsv_layout_t bcsv_sampler_output_layout(bcsv_sampler_t s);

// ── Iteration ──────────────────────────────────────────────────
bool            bcsv_sampler_next       (bcsv_sampler_t s);
const_bcsv_row_t bcsv_sampler_row       (bcsv_sampler_t s);
size_t          bcsv_sampler_source_pos (bcsv_sampler_t s);

// Error reporting via bcsv_last_error() (existing pattern)
```

The C API follows existing conventions: opaque `void*` handles, `bcsv_snake_case`
naming, error reporting via `bcsv_last_error()` thread-local string.

**Lifetime contract:** The Sampler holds a non-owning reference to the Reader
passed to `bcsv_sampler_create()`. The Reader must outlive the Sampler. Calling
any Sampler function after the underlying Reader has been destroyed is undefined
behaviour. `bcsv_sampler_destroy()` does not destroy the Reader.

**Error boundary:** Compile-time errors from `bcsv_sampler_set_conditional()` /
`bcsv_sampler_set_selection()` are reported via `bcsv_last_error()` (thread-local
string, existing BCSV pattern). The `bool` return value indicates success/failure.
The C++ `SamplerCompileResult` struct (with `error_position`) exists because the
C++ API can return richer structured errors directly. In the C API, the error
position is encoded into the `bcsv_last_error()` message string (e.g.,
`"column 14: unknown identifier 'foo'"`) since the C ABI cannot return structs
without additional allocation. Runtime errors (div-by-zero under `FAIL_ON_ERROR`)
also surface through `bcsv_last_error()`, and `bcsv_sampler_next()` returns
`false` to signal termination.

**Diagnostics:** The C++ `disassemble()` and `windowCapacity()` methods are
not exposed in the C API. These are developer-facing diagnostics intended for
C++ unit tests and debugging; the C API targets production embedding where
bytecode introspection is not required. They can be added later if demand
arises.

---

## 11. Python API Design

### 11.1 pybind11 Bindings

```python
import pybcsv

reader = pybcsv.Reader()
reader.open("sensors.bcsv")

sampler = pybcsv.Sampler(reader)
sampler.set_conditional('X[0][0] != X[-1][0]')
sampler.set_selection('X[0][0], X[0][1]')
sampler.mode = pybcsv.SamplerMode.TRUNCATE
sampler.error_policy = pybcsv.SamplerErrorPolicy.FAIL_ON_ERROR  # default

# Iteration (Pythonic)
for row in sampler:
    print(row)

# Bulk to list
all_rows = sampler.bulk()
```

### 11.2 pandas / numpy Integration

```python
import pybcsv
from pybcsv.pandas_utils import sampler_to_dataframe, sampler_to_numpy

reader = pybcsv.Reader()
reader.open("sensors.bcsv")

sampler = pybcsv.Sampler(reader)
sampler.set_conditional('X[0][0] != X[-1][0]')
sampler.set_selection('X[0][0], X[0][1]')

# Convert to pandas DataFrame
df = sampler_to_dataframe(sampler)

# Convert to numpy structured array
arr = sampler_to_numpy(sampler)
```

**Implementation strategy for `sampler_to_dataframe()`:**

1. If output layout has no string columns: pre-allocate numpy column arrays,
   fill in a tight C++ loop (GIL released), construct DataFrame from dict of
   arrays. This avoids Python-level per-row overhead.

2. If output layout has string columns: fall back to row-by-row Python list
   construction (strings require GIL for Python object creation).

3. Optional `chunk_size` parameter for chunked reading — returns an iterator
   of DataFrames for very large result sets.

**String boundary safety:** BCSV string values in C++ are views or small
vectors that reference data inside the circular `RowWindow`. When the window
slides forward, previous row slots are overwritten. The pybind11 binding
must **eagerly deep-copy** all string column values into Python `str` objects
at the point of extraction (inside `row()` or the bulk loop). Returning a
C++ `string_view` or pointer across the Python boundary would cause
use-after-free when the window advances. Numeric columns are safe (copied by
value into numpy scalars or arrays).

```python
# Chunked reading for large outputs
for chunk_df in sampler_to_dataframe(sampler, chunk_size=100_000):
    process(chunk_df)
```

---

## 12. C# API Design

The C# API mirrors the C API via P/Invoke, following the existing pattern in
`csharp/`:

```csharp
public class Sampler : IDisposable
{
    private IntPtr handle_;

    public Sampler(Reader reader)
    {
        handle_ = Native.bcsv_sampler_create(reader.Handle);
    }

    public bool SetConditional(string expr)
        => Native.bcsv_sampler_set_conditional(handle_, expr);

    public bool SetSelection(string expr)
        => Native.bcsv_sampler_set_selection(handle_, expr);

    public SamplerMode Mode { get; set; }

    public SamplerErrorPolicy ErrorPolicy { get; set; }

    public bool Next() => Native.bcsv_sampler_next(handle_);

    public Row CurrentRow => new Row(Native.bcsv_sampler_row(handle_));

    public void Dispose() => Native.bcsv_sampler_destroy(handle_);
}
```

---

## 13. CLI Tool: bcsvSampler

### 13.1 Interface

```text
bcsvSampler [OPTIONS] <input.bcsv> [output.bcsv]

OPTIONS:
  -c, --conditional <expr>   Conditional expression (required)
  -s, --selection <expr>     Selection expression (default: "X[0][*]")
  -m, --mode <mode>          Boundary mode: truncate (default), expand
  -e, --error-policy <pol>   Runtime error policy: fail (default), skip-row
  -C, --compress <0-9>       Output compression level (default: 1)
  -d, --delimiter <char>     CSV output delimiter (for --csv mode)
      --csv                  Output as CSV instead of BCSV
      --header               Print output layout header and exit
      --dry-run              Validate expressions without processing
  -v, --verbose              Print progress to stderr
  -h, --help                 Print usage

PIPE SUPPORT:
  cat input.bcsv | bcsvSampler -c "X[0][0] > 100" > output.bcsv
  bcsvSampler -c "..." input.bcsv | bcsv2csv > output.csv
```

### 13.2 Implementation

Follows existing CLI tool patterns (`bcsv2csv.cpp`):
- `Config` struct for parsed arguments
- `parseArgs(argc, argv)` function
- `printUsage()` function
- Uses `bcsv::Reader<bcsv::Layout>` + `bcsv::Sampler<bcsv::Layout>`
- Output via `bcsv::Writer<bcsv::Layout>` (BCSV) or CSV formatter (CSV mode)
- Stdin/stdout support for piping

---

## 14. Implementation Phases

### Phase 0: Specification Freeze

**Goal:** Finalize and lock the specification before any implementation begins.

**Inputs accepted (all must be confirmed before gate passes):**

| # | Item | Section | Status |
|---|------|---------|--------|
| I1 | EBNF grammar — no ambiguous productions | 5.1 | ☐ |
| I2 | Operator precedence table — 12 levels confirmed | 5.2 | ☐ |
| I3 | Type promotion matrix — all pairs decided | 5.3 | ☐ |
| I4 | Opcode table — complete, no placeholders | 6.2 | ☐ |
| I5 | `SamplerErrorPolicy` (FAIL / SKIP) — default confirmed | 9.1 | ☐ |
| I6 | Boundary modes (TRUNCATE + EXPAND) — semantics confirmed | 2.3 | ☐ |
| I7 | C++ API — `Sampler` class signature final | 9.1 | ☐ |
| I8 | C API — function signatures final | 10 | ☐ |
| I9 | Python API — pybind11 surface confirmed | 11 | ☐ |
| I10 | C# API — P/Invoke surface confirmed | 12 | ☐ |
| I11 | CLI flags — `bcsvSampler` interface final | 13 | ☐ |
| I12 | v1 feature lock: column-name indexing, bitwise, modulo | 2.1, 18 | ☐ |
| I13 | Specification test vectors — all expected outputs validated by hand | App. D | ☐ |

**Outputs:**
- This document updated with any review-driven changes and re-committed (tagged)
- Specification test vectors (Appendix D) confirmed correct
- Signoff from project owner
- All checkboxes above ticked

**Estimated effort:** 1–2 days (review + iteration)

### Phase 1: Core Expression Engine (C++ only)

**Goal:** Tokenizer + Parser + AST + TypeResolver + BytecodeCompiler + VM

**Deliverables:**
- `sampler_tokenizer.h` — lexical analysis
- `sampler_parser.h` — Pratt parser (recommended over grammar-layered recursive
  descent for its compactness with many precedence levels; ~20% less code)
- `sampler_ast.h` — AST node types
- `sampler_types.h` — type resolution and validation
- `sampler_compiler.h` — AST → bytecode
- `sampler_vm.h` / `.hpp` — threaded bytecode interpreter
- Unit tests for each component (tokenizer, parser, type resolver, compiler, VM)

**Estimated effort:** 3–5 days

### Phase 2: RowWindow + Sampler Class

**Goal:** Sliding window, Sampler class, `next()` / `row()` / `bulk()`

**Deliverables:**
- `sampler_window.h` — circular row buffer
- `sampler.h` / `.hpp` — public Sampler class
- Output layout derivation
- Boundary modes (TRUNCATE, EXPAND)
- Integration tests (Sampler + Reader on test BCSV files)
- `disassemble()` diagnostic output

**Estimated effort:** 2–3 days

### Phase 3: C API + CLI Tool

**Goal:** C API wrapper, `bcsvSampler` CLI tool

**Deliverables:**
- C API functions in `bcsv_c_api.h` / `bcsv_c_api.cpp`
- `bcsvSampler.cpp` CLI tool
- Build system integration (CMakeLists.txt)
- CLI tests (shell-based, comparing outputs)

**Estimated effort:** 2 days

### Phase 4: Python Bindings

**Goal:** pybind11 bindings, pandas/numpy integration

**Deliverables:**
- `Sampler` class in `bindings.cpp`
- `sampler_to_dataframe()` / `sampler_to_numpy()` in `pandas_utils.py`
- Chunked reading support
- Python unit tests

**Estimated effort:** 2–3 days

### Phase 5: C# Bindings

**Goal:** P/Invoke wrapper

**Deliverables:**
- C# `Sampler` class in `csharp/`
- Example usage

**Estimated effort:** 1 day

### Phase 6: Documentation + Benchmarks

**Goal:** User documentation, performance validation

**Deliverables:**
- `examples/bcsvSampler.md` — CLI documentation
- `docs/SAMPLER.md` — API documentation with examples
- Benchmark: Sampler throughput vs. raw Reader throughput
- Integration into benchmark suite

**Estimated effort:** 1–2 days

**Total estimated effort: 12–18 days** (including Phase 0 spec freeze)

---

## 15. Testing Strategy

### 15.1 Unit Tests (Google Test)

| Test file | Coverage |
|-----------|----------|
| `sampler_tokenizer_test.cpp` | All token types, edge cases (empty, malformed), operator precedence tokens |
| `sampler_parser_test.cpp` | Valid/invalid expressions, all grammar productions, error messages |
| `sampler_types_test.cpp` | Type resolution, promotion rules, invalid type combinations |
| `sampler_compiler_test.cpp` | Bytecode output for known expressions, instruction correctness |
| `sampler_vm_test.cpp` | Evaluation of compiled expressions against known row data |
| `sampler_window_test.cpp` | Circular buffer, boundary modes, push/access patterns |
| `sampler_test.cpp` | End-to-end: Reader → Sampler → output rows, various expression combinations |

### 15.2 Property-Based Tests

- **Round-trip:** For identity selection (`X[0][*]`) with always-true conditional
  (`1 == 1`), output must equal input.
- **Monotonicity:** For conditional `X[0][0] > X[-1][0]`, output col 0 must be
  strictly increasing.
- **Count preservation:** `bulk().size()` must equal the number of times `next()`
  returns true.

### 15.3 Fuzz Testing

Randomised / mutation-based fuzzing of `setConditional()` and `setSelection()`
with garbage strings. Goal: no crashes, no undefined behaviour, graceful error
reporting for all inputs. Use libFuzzer or AFL++ if available, otherwise a
custom random-string generator.

### 15.4 CLI Tests

Shell scripts comparing `bcsvSampler` output against known-good reference files.
Test piping: `bcsvSampler ... | bcsv2csv > output.csv` and compare.

### 15.5 Cross-Language Tests

Python and C# tests that exercise the same expressions as C++ tests and compare
results.

---

## 16. Performance Budget

### 16.1 Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| Expression compilation | < 100 μs | Includes tokenize + parse + resolve + compile |
| Per-row evaluation overhead | < 50 ns | Must not exceed 10% of codec deserialization time |
| `next()` throughput (no match) | ≥ 90% of raw Reader throughput | Overhead is window push + eval |
| `next()` throughput (all match) | ≥ 80% of raw Reader throughput | Additional output row construction |
| Memory overhead | < 100 KB | For typical expressions (window ≤ 10 rows, 1000 columns) |

### 16.2 Benchmark Design

Two benchmarks added to the benchmark suite:

1. **Sampler overhead:** Read same file with raw Reader vs. Sampler with trivial
   conditional (`1 == 1`) and identity selection (`X[0][*]`). Measures pure
   infrastructure overhead.

2. **Sampler realistic:** Edge-detection conditional (`X[0][0] != X[-1][0]`)
   with 2-column selection on a 1000-column file. Measures practical throughput.

3. **Memory validation:** Run the realistic benchmark under Valgrind/Massif on
   a 100+ GB file to confirm O(1) memory usage (no growth beyond the window
   allocation).

---

## 17. Risk Analysis

### 17.1 Technical Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Row copy overhead dominates for wide rows | Medium | Medium | Profile early. Consider copy-on-write or column-subset copying (only copy referenced columns). |
| String expression support adds significant complexity | Medium | Low | Limit string ops to `==` / `!=` in v1. Defer regex/contains/substr to later. |
| Wildcard expansion creates very large output layouts | Low | Medium | Cap at source column count. Warn if output > 10K columns. |
| `WRAP` mode requires knowing row count upfront | — | — | Rejected: architecturally incompatible with streaming. Not in scope. |
| Computed goto not available on MSVC | Certain | Low | Automatic fallback to `switch`. Document MSVC performance note. |
| Complex expressions with deep nesting overflow stack | Very Low | Low | Limit nesting depth to 64 in parser. |
| Division by zero in arithmetic expressions | Medium | Medium | `DIV_FLOAT` follows IEEE 754 (±Inf/NaN). `DIV_INT` / `MOD_INT` behaviour controlled by `SamplerErrorPolicy`: `FAIL_ON_ERROR` (default) aborts; `SKIP_ROW` discards the row and continues. |
| Hidden memory growth in wrapper layers | Medium | Medium | Python `bulk()` / C# `ToList()` can silently materialise millions of rows. Mitigation: make chunked APIs first-class (default `chunk_size` parameter in `sampler_to_dataframe()`), document memory implications, add `windowCapacity()` for introspection. |

### 17.2 Scope Risks

| Risk | Mitigation |
|------|------------|
| Feature creep (regex, aggregation, GROUP BY) | Strict scope: only filter + project. Aggregation is a separate feature request. |
| Expression language becomes Turing-complete | No loops, no variables, no function definitions. Pure expression evaluation. |
| Cross-language API surface grows uncontrollably | C API is the authoritative surface. Python/C# wrap it 1:1. |

---

## 18. Open Questions — Resolved

The following questions were posed in the initial draft and have been resolved
through external review (Gemini 3.1 Pro) and project-owner decisions:

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| 1 | Column name references | **Include in v1** | Layout already stores names; resolution at compile time in TypeResolver; zero runtime cost; ~50 LOC. Grammar and semantics updated. |
| 2 | Selection output type | **Preserve source type for simple refs; promoted type for expressions** | Avoids doubling storage for float-heavy files. `X[0][float_col]` → `float`; `X[0][float_col] - X[-1][float_col]` → `double`. |
| 3 | Short-circuit evaluation | **Yes — implement with `JUMP_IF_FALSE` / `JUMP_IF_TRUE`** | Correctness requirement: `(X[0][0] != 0) && (X[0][1] / X[0][0] > 5)` crashes without it. Opcodes added to Section 6.2.6. |
| 4 | Error recovery (compile-time) | **Fail-fast** | Compilation (parsing, type resolution) is fail-fast: first error with precise position and message; no recovery. Runtime evaluation errors (e.g., div-by-zero in `DIV_INT`) are governed by `SamplerErrorPolicy` (FAIL_ON_ERROR / SKIP_ROW). These are separate concerns: compile-time errors are always fatal; runtime errors are policy-controlled. |
| 5 | WRAP mode | **Rejected entirely** | Architecturally incompatible with forward-only streaming. Removed from requirements and enum. |
| 6 | Modulo operator | **Include in v1** | Useful for downsampling (`X[0][0] % 100 == 0`). `MOD_INT` opcode added. Integer-only. |
| 7 | Bitwise operators | **Include in v1** | Useful for flag columns; BCSV's `Bitset<>` storage makes bit extraction relevant. 6 opcodes added (`BIT_AND/OR/XOR/NOT/SHL/SHR`). Integer-only. |
| 8 | Output writer in Sampler | **CLI only** | Sampler is a pure generator/iterator (SRP). User or CLI creates Writer from `outputLayout()`. |
| 9 | Sparse window copies | **Not in v1** | Premature optimisation. Standard Row deep copies are fast enough. Profile first; add sparse mode in a later release if needed. |
| 10 | Unsigned integer semantics | **Promote to `int64_t`** | Consistent with C++ implicit conversion. Values exceeding `INT64_MAX` lose upper range — acceptable trade-off for predictable behaviour. |

---

## Appendix A: Lean Checklist Summary

| Check | Status |
|-------|--------|
| Scope fit | PASS — Sampler is a well-defined filter+project operator, bounded scope |
| Ownership clarity | PASS — New component, no overlap with existing code |
| Layering | PASS — Sampler depends on Reader + Row (one-way dependency) |
| Duplication budget | PASS — Expression engine is new; CLI follows existing patterns |
| Complexity budget | PASS — ~2580 LOC, comparable to existing codec implementations |
| Safety guardrails | PASS — Expression validation at compile time; boundary modes |
| Compatibility impact | PASS — Additive feature, no changes to existing APIs. C API additions are additive (no symbol removals, no ABI breaks). Python/C# wrappers gate sampler features by native library capability (`NotImplementedError` if native lib too old). |

**High risks:**
1. Row copy overhead for very wide rows (1000+ columns) in the sliding window
2. Expression language scope creep if reviewers request advanced features

**Mitigations:**
1. Profile early; sparse window optimisation identified as fallback
2. Strict v1 scope with clear extension points documented

---

## Appendix B: Related ToDo Items

- **Item 20:** This document (Sampler API)
- **Item 21:** CLI piping — `bcsvSampler` contributes to this goal
- **Item 22:** `bcsvSampler` CLI tool listed here
- **Item 24:** Sparse column reads — may share infrastructure with sparse window optimisation

---

## Appendix C: References

- [Eli Bendersky: Computed goto for efficient dispatch tables](https://eli.thegreenplace.net/2012/07/12/computed-goto-for-efficient-dispatch-tables) — 25% speedup measured, adopted by CPython, Ruby YARV, Dalvik
- [CPython ceval.c](https://github.com/python/cpython/blob/main/Python/ceval.c) — Production use of computed goto in bytecode VM
- [SQLite VDBE](https://www.sqlite.org/opcode.html) — Switch-dispatch bytecode VM processing SQL, ~170 opcodes
- [LuaJIT interpreter](https://luajit.org/luajit.html) — Threaded dispatch for maximum interpreter throughput
- BCSV `ARCHITECTURE.md`, `SKILLS.md`, `docs/ERROR_HANDLING.md` — Project conventions

---

## Appendix D: Specification Test Vectors

These test vectors define **expected behaviour** for the Sampler before any
implementation begins. They serve three purposes:

1. **Spec validation:** If the expected output cannot be determined from the
   specification, the spec has a gap that must be resolved in Phase 0.
2. **Acceptance tests:** Each vector becomes a unit/integration test during
   implementation. A phase is not complete until all relevant vectors pass.
3. **Cross-language parity:** The same vectors are used for C++, Python, and
   C# to ensure consistent behaviour across all bindings.

### D.1 Canonical Test Dataset

A 7-row BCSV file with the following layout:

| Column | Index | Name | Type | Description |
|--------|:-----:|------|------|-------------|
| 0 | 0 | `timestamp` | `double` | Monotonically increasing time |
| 1 | 1 | `temperature` | `float` | Sensor reading (varies) |
| 2 | 2 | `status` | `string` | Status label |
| 3 | 3 | `flags` | `uint16_t` | Bitmask (0x01=alarm, 0x02=valid, 0x04=calibrated) |
| 4 | 4 | `counter` | `int32_t` | Incrementing counter |

**Row data (0-indexed):**

| Row | timestamp | temperature | status | flags | counter |
|:---:|----------:|------------:|--------|------:|--------:|
| 0 | 1.0 | 20.5 | `"ok"` | 0x06 | 0 |
| 1 | 2.0 | 21.0 | `"ok"` | 0x07 | 1 |
| 2 | 3.0 | 21.0 | `"warn"` | 0x03 | 2 |
| 3 | 4.0 | 55.0 | `"alarm"` | 0x05 | 3 |
| 4 | 5.0 | 55.0 | `"alarm"` | 0x05 | 100 |
| 5 | 6.0 | 22.0 | `"ok"` | 0x07 | 101 |
| 6 | 7.0 | 22.5 | `"ok"` | 0x06 | 102 |

This dataset is chosen to exercise: edge detection (rows 1→2, 2→3, 3→4, 5→6),
threshold crossings (row 3 temperature), string comparisons, bitwise flag
manipulation, modulo arithmetic, and boundary mode differences.

### D.2 Test Vectors

All vectors use the canonical dataset above unless stated otherwise.
**Mode** is TRUNCATE unless stated otherwise.

---

#### TV-01: Identity pass-through

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][*]` |
| Mode | TRUNCATE |
| Expected rows | All 7 rows, unchanged |

**Rationale:** Baseline sanity — Sampler with trivial conditional must reproduce
the input exactly. Validates wildcard expansion and `true` literal.

---

#### TV-02: Always-false conditional

| | |
|---|---|
| Conditional | `false` |
| Selection | `X[0][*]` |
| Expected rows | 0 rows (empty output) |

---

#### TV-03: Simple threshold filter

| | |
|---|---|
| Conditional | `X[0][1] > 50.0` |
| Selection | `X[0][0], X[0][1]` |
| Expected rows | 2 |

| timestamp | temperature |
|----------:|------------:|
| 4.0 | 55.0 |
| 5.0 | 55.0 |

---

#### TV-04: Edge detection (lookbehind)

| | |
|---|---|
| Conditional | `X[0][1] != X[-1][1]` |
| Selection | `X[0][0], X[-1][1], X[0][1]` |
| Mode | TRUNCATE |
| Expected rows | 3 (row 0 skipped — no X[-1]) |

Row pairs evaluated: 0→1 (20.5→21.0 ✓), 1→2 (21.0→21.0 ✗),
2→3 (21.0→55.0 ✓), 3→4 (55.0→55.0 ✗), 4→5 (22.0→22.5 ✓).

| timestamp | prev_temp | curr_temp |
|----------:|----------:|----------:|
| 2.0 | 20.5 | 21.0 |
| 4.0 | 21.0 | 55.0 |
| 6.0 | 22.0 | 22.5 |

---

#### TV-05: Edge detection with EXPAND mode

| | |
|---|---|
| Conditional | `X[0][1] != X[-1][1]` |
| Selection | `X[0][0], X[0][1]` |
| Mode | EXPAND |
| Expected rows | 3 |

With EXPAND, row 0 has `X[-1]` = copy of row 0 (edge replication), so
`X[0][1] != X[-1][1]` → `20.5 != 20.5` → false. Same 3 output rows as TV-04
but now all 7 rows are candidates (none skipped at boundary).

| timestamp | temperature |
|----------:|------------:|
| 2.0 | 21.0 |
| 4.0 | 55.0 |
| 6.0 | 22.5 |

---

#### TV-06: String equality

| | |
|---|---|
| Conditional | `X[0][2] == "alarm"` |
| Selection | `X[0][0], X[0][2]` |
| Expected rows | 2 |

| timestamp | status |
|----------:|--------|
| 4.0 | `"alarm"` |
| 5.0 | `"alarm"` |

---

#### TV-07: String inequality

| | |
|---|---|
| Conditional | `X[0][2] != "ok"` |
| Selection | `X[0][0], X[0][2]` |
| Expected rows | 3 |

| timestamp | status |
|----------:|--------|
| 3.0 | `"warn"` |
| 4.0 | `"alarm"` |
| 5.0 | `"alarm"` |

---

#### TV-08: Compound boolean (AND with short-circuit)

| | |
|---|---|
| Conditional | `X[0][4] != 0 && X[0][1] / X[0][4] > 10.0` |
| Selection | `X[0][0], X[0][1], X[0][4]` |
| Expected rows | 3 |

Row 0 has `counter=0` → short-circuit prevents division by zero.
Results: `21.0/1=21.0>10 ✓`, `21.0/2=10.5>10 ✓`, `55.0/3≈18.3>10 ✓`,
`55.0/100=0.55 ✗`, `22.0/101≈0.22 ✗`, `22.5/102≈0.22 ✗`.

| timestamp | temperature | counter |
|----------:|------------:|--------:|
| 2.0 | 21.0 | 1 |
| 3.0 | 21.0 | 2 |
| 4.0 | 55.0 | 3 |

---

#### TV-09: Boolean OR with short-circuit

| | |
|---|---|
| Conditional | `X[0][1] > 50.0 \|\| X[0][2] == "warn"` |
| Selection | `X[0][0]` |
| Expected rows | 3 |

| timestamp |
|----------:|
| 3.0 |
| 4.0 |
| 5.0 |

---

#### TV-10: Arithmetic in selection (delta computation)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], X[0][1] - X[-1][1]` |
| Mode | TRUNCATE |
| Expected rows | 6 (row 0 skipped — X[-1] out of bounds) |

| timestamp | delta_temp |
|----------:|-----------:|
| 2.0 | 0.5 |
| 3.0 | 0.0 |
| 4.0 | 34.0 |
| 5.0 | 0.0 |
| 6.0 | -33.0 |
| 7.0 | 0.5 |

---

#### TV-11: Modulo downsampling

| | |
|---|---|
| Conditional | `X[0][4] % 2 == 0` |
| Selection | `X[0][0], X[0][4]` |
| Expected rows | 4 |

| timestamp | counter |
|----------:|--------:|
| 1.0 | 0 |
| 3.0 | 2 |
| 5.0 | 100 |
| 7.0 | 102 |

---

#### TV-12: Bitwise AND (flag extraction)

| | |
|---|---|
| Conditional | `(X[0][3] & 0x04) != 0` |
| Selection | `X[0][0], X[0][3]` |
| Expected rows | 3 (rows with calibrated flag set) |

Flags: row0=0x06(✓), row1=0x07(✓), row2=0x03(✗), row3=0x05(✓),
row4=0x05(✓), row5=0x07(✓), row6=0x06(✓).

| timestamp | flags |
|----------:|------:|
| 1.0 | 0x06 |
| 2.0 | 0x07 |
| 4.0 | 0x05 |
| 5.0 | 0x05 |
| 6.0 | 0x07 |
| 7.0 | 0x06 |

---

#### TV-13: Bitwise NOT + AND (clear-and-test)

| | |
|---|---|
| Conditional | `(X[0][3] & ~0x02) == 0x04` |
| Selection | `X[0][0], X[0][3]` |
| Expected rows | 2 |

`~0x02` = `0xFFFD` (16-bit), so `flags & 0xFFFD`:
row0: `0x06 & 0xFFFD = 0x04` ✓, row1: `0x07 & 0xFFFD = 0x05` ✗,
row2: `0x03 & 0xFFFD = 0x01` ✗, row3: `0x05 & 0xFFFD = 0x05` ✗,
row4: `0x05 & 0xFFFD = 0x05` ✗, row5: `0x07 & 0xFFFD = 0x05` ✗,
row6: `0x06 & 0xFFFD = 0x04` ✓.

Note: `~0x02` operates on `int64_t` (per type promotion rules), so it is
actually `0xFFFF_FFFF_FFFF_FFFD`. The `uint16_t` flags value is promoted to
`int64_t` before the AND. `0x06 & 0xFFFFFFFFFFFFFFFD = 0x04` ✓.

| timestamp | flags |
|----------:|------:|
| 1.0 | 0x06 |
| 7.0 | 0x06 |

---

#### TV-14: Column-name indexing

| | |
|---|---|
| Conditional | `X[0]["temperature"] > 50.0` |
| Selection | `X[0]["timestamp"], X[0]["temperature"]` |
| Expected rows | 2 (same as TV-03) |

| timestamp | temperature |
|----------:|------------:|
| 4.0 | 55.0 |
| 5.0 | 55.0 |

---

#### TV-15: Negation operator

| | |
|---|---|
| Conditional | `!(X[0][2] == "ok")` |
| Selection | `X[0][0], X[0][2]` |
| Expected rows | 3 (same as TV-07) |

---

#### TV-16: Numeric in boolean context

| | |
|---|---|
| Conditional | `X[0][4]` |
| Selection | `X[0][0], X[0][4]` |
| Expected rows | 6 (row 0 excluded — counter=0 → false) |

| timestamp | counter |
|----------:|--------:|
| 2.0 | 1 |
| 3.0 | 2 |
| 4.0 | 3 |
| 5.0 | 100 |
| 6.0 | 101 |
| 7.0 | 102 |

---

#### TV-17: Lookahead (TRUNCATE)

| | |
|---|---|
| Conditional | `X[+1][1] > X[0][1]` |
| Selection | `X[0][0], X[0][1], X[+1][1]` |
| Mode | TRUNCATE |
| Expected rows | 2 (row 6 skipped — no X[+1]) |

Comparisons: 20.5→21.0 ✓, 21.0→21.0 ✗, 21.0→55.0 ✓, 55.0→55.0 ✗,
55.0→22.0 ✗, 22.0→22.5 ✓.

| timestamp | curr_temp | next_temp |
|----------:|----------:|----------:|
| 1.0 | 20.5 | 21.0 |
| 3.0 | 21.0 | 55.0 |
| 6.0 | 22.0 | 22.5 |

---

#### TV-18: Lookahead (EXPAND)

| | |
|---|---|
| Conditional | `X[+1][1] > X[0][1]` |
| Selection | `X[0][0], X[0][1], X[+1][1]` |
| Mode | EXPAND |
| Expected rows | 3 |

With EXPAND, row 6 gets `X[+1]` = copy of row 6, so `22.5 > 22.5` → false.
Same 3 output rows as TV-17.

| timestamp | curr_temp | next_temp |
|----------:|----------:|----------:|
| 1.0 | 20.5 | 21.0 |
| 3.0 | 21.0 | 55.0 |
| 6.0 | 22.0 | 22.5 |

---

#### TV-19: Mixed lookbehind + lookahead (TRUNCATE)

| | |
|---|---|
| Conditional | `X[0][1] > X[-1][1] && X[0][1] > X[+1][1]` |
| Selection | `X[0][0], X[0][1]` |
| Mode | TRUNCATE |
| Expected rows | 0 (rows 0 and 6 skipped at boundaries) |

Rows 1–5 evaluated (window requires both X[-1] and X[+1]):
Row 1: 21.0>20.5 ✓ && 21.0>21.0 ✗. Row 2: 21.0>21.0 ✗.
Row 3: 55.0>21.0 ✓ && 55.0>55.0 ✗. Row 4: 55.0>55.0 ✗.
Row 5: 22.0>55.0 ✗. No local maxima exist in this dataset.

---

#### TV-20: Type promotion (int + float)

| | |
|---|---|
| Conditional | `X[0][4] + X[0][1] > 60.0` |
| Selection | `X[0][0], X[0][4] + X[0][1]` |
| Expected rows | 3 |

`counter + temperature`: 0+20.5=20.5, 1+21.0=22.0, 2+21.0=23.0,
3+55.0=58.0, 100+55.0=155.0 ✓, 101+22.0=123.0 ✓, 102+22.5=124.5 ✓.

| timestamp | sum |
|----------:|----:|
| 5.0 | 155.0 |
| 6.0 | 123.0 |
| 7.0 | 124.5 |

---

#### TV-21: Wildcard with offset (doubled output)

| | |
|---|---|
| Conditional | `X[0][0] == 3.0` |
| Selection | `X[-1][*], X[0][*]` |
| Mode | TRUNCATE |
| Expected rows | 1 |

Row 2 (timestamp=3.0) matches.
Output is 10 columns: row 1 data + row 2 data.

| prev_ts | prev_temp | prev_status | prev_flags | prev_ctr | ts | temp | status | flags | ctr |
|--------:|----------:|-------------|-----------:|---------:|---:|-----:|--------|------:|----:|
| 2.0 | 21.0 | `"ok"` | 0x07 | 1 | 3.0 | 21.0 | `"warn"` | 0x03 | 2 |

---

#### TV-22: Compile-time error — string in arithmetic

| | |
|---|---|
| Conditional | `X[0][2] + 1 > 0` |
| Expected | `SamplerCompileResult.success == false` |
| Error | Type error: cannot apply `+` to `String` and `Integer` |

---

#### TV-23: Compile-time error — string ordering

| | |
|---|---|
| Conditional | `X[0][2] > "ok"` |
| Expected | `SamplerCompileResult.success == false` |
| Error | Type error: `>` not supported for `String` operands |

---

#### TV-24: Compile-time error — invalid column index

| | |
|---|---|
| Conditional | `X[0][99] > 0` |
| Expected | `SamplerCompileResult.success == false` |
| Error | Column index 99 out of range (layout has 5 columns) |

---

#### TV-25: Compile-time error — unknown column name

| | |
|---|---|
| Conditional | `X[0]["nonexistent"] > 0` |
| Expected | `SamplerCompileResult.success == false` |
| Error | Unknown column name `"nonexistent"` |

---

#### TV-26: Runtime error — division by zero (FAIL_ON_ERROR)

| | |
|---|---|
| Conditional | `X[0][1] / (X[0][4] - X[0][4]) > 0` |
| Error policy | FAIL_ON_ERROR |
| Expected | Compiles successfully. `next()` returns `false` on first row; error reported. |

---

#### TV-27: Runtime error — division by zero (SKIP_ROW)

| | |
|---|---|
| Conditional | `X[0][1] / (X[0][4] - X[0][4]) > 0` |
| Selection | `X[0][0]` |
| Error policy | SKIP_ROW |
| Expected | Compiles successfully. All rows skipped (div-by-zero on every row). 0 output rows. |

---

#### TV-28: Shift operators

| | |
|---|---|
| Conditional | `(X[0][3] >> 1) & 0x01 != 0` |
| Selection | `X[0][0], X[0][3]` |
| Expected rows | 5 |

`flags >> 1`: row0=0x03, row1=0x03, row2=0x01, row3=0x02, row4=0x02,
row5=0x03, row6=0x03. Bit 0 set: rows 0,1,2,5,6.

| timestamp | flags |
|----------:|------:|
| 1.0 | 0x06 |
| 2.0 | 0x07 |
| 3.0 | 0x03 |
| 6.0 | 0x07 |
| 7.0 | 0x06 |

---

#### TV-29: Parenthesised precedence override

| | |
|---|---|
| Conditional | `X[0][4] % (2 + 1) == 0` |
| Selection | `X[0][0], X[0][4]` |
| Expected rows | 3 |

`counter % 3`: 0%3=0 ✓, 1%3=1 ✗, 2%3=2 ✗, 3%3=0 ✓, 100%3=1 ✗,
101%3=2 ✗, 102%3=0 ✓.

| timestamp | counter |
|----------:|--------:|
| 1.0 | 0 |
| 4.0 | 3 |
| 7.0 | 102 |

---

#### TV-30: Linear interpolation (first-order midpoint)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], (X[-1][1] + X[0][1]) / 2.0` |
| Mode | TRUNCATE |
| Expected rows | 6 (row 0 skipped — no X[-1]) |

Computes the midpoint between consecutive temperature samples — a basic
first-order interpolation. Exercises lookbehind, float addition, and division
in the selection expression.

| timestamp | interp |
|----------:|-------:|
| 2.0 | 20.75 |
| 3.0 | 21.0 |
| 4.0 | 38.0 |
| 5.0 | 55.0 |
| 6.0 | 38.5 |
| 7.0 | 22.25 |

---

#### TV-31: Quadratic smoothing (second-order, 3-point)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], (X[-1][1] + 2.0 * X[0][1] + X[+1][1]) / 4.0` |
| Mode | TRUNCATE |
| Expected rows | 5 (rows 0 and 6 skipped — no X[-1] / X[+1]) |

Three-point quadratic (binomial) smoothing kernel `[1,2,1]/4`. Uses both
lookbehind and lookahead, multiplication by a constant, and multi-term
arithmetic in a single selection expression.

| timestamp | smoothed |
|----------:|---------:|
| 2.0 | 20.875 |
| 3.0 | 29.5 |
| 4.0 | 46.5 |
| 5.0 | 46.75 |
| 6.0 | 30.375 |

---

#### TV-32: Sliding window average (3-point)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], (X[-1][1] + X[0][1] + X[+1][1]) / 3.0` |
| Mode | TRUNCATE |
| Expected rows | 5 (rows 0 and 6 skipped) |

Uniform moving average over a 3-row window. Exercises combined lookbehind +
lookahead with division. Output values are repeating decimals — tests that
float comparison in downstream consumers handles precision correctly.

| timestamp | avg_temp |
|----------:|---------:|
| 2.0 | 20.8333… |
| 3.0 | 32.3333… |
| 4.0 | 43.6666… |
| 5.0 | 44.0 |
| 6.0 | 33.1666… |

---

#### TV-33: Sliding window average (5-point, wide window)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], (X[-2][1] + X[-1][1] + X[0][1] + X[+1][1] + X[+2][1]) / 5.0` |
| Mode | TRUNCATE |
| Expected rows | 3 (rows 0,1 and 5,6 skipped — window needs X[-2]…X[+2]) |

Tests a wider window (capacity = 5). The dataset has only 7 rows, so only 3
are evaluable under TRUNCATE. Validates correct RowWindow sizing for
`min_offset=-2, max_offset=+2`.

| timestamp | avg_temp |
|----------:|---------:|
| 3.0 | 34.5 |
| 4.0 | 34.8 |
| 5.0 | 35.1 |

---

#### TV-34: Gradient over time (first derivative)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])` |
| Mode | TRUNCATE |
| Expected rows | 6 (row 0 skipped) |

Computes `dT/dt` — the rate of change of temperature (col 1) with respect
to timestamp (col 0). This is the canonical gradient computation: numerator
is the value delta, denominator is the time delta. Tests division where the
divisor comes from data (not a constant), which could be zero for duplicate
timestamps — here all timestamps are distinct so no runtime error occurs.

| timestamp | gradient |
|----------:|---------:|
| 2.0 | 0.5 |
| 3.0 | 0.0 |
| 4.0 | 34.0 |
| 5.0 | 0.0 |
| 6.0 | -33.0 |
| 7.0 | 0.5 |

---

#### TV-35: Gradient filter (threshold on rate-of-change)

| | |
|---|---|
| Conditional | `(X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0]) > 1.0 \|\| (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0]) < -1.0` |
| Selection | `X[0][0], X[0][1], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])` |
| Mode | TRUNCATE |
| Expected rows | 2 |

Selects rows where the absolute gradient exceeds 1.0 °/s. Since the
expression language has no `abs()` built-in, this uses `g > 1.0 || g < -1.0`.
Tests a complex conditional with repeated sub-expressions (future optimiser
could CSE these), combined with a matching selection that outputs the gradient.

Gradients: 0.5 ✗, 0.0 ✗, 34.0 ✓, 0.0 ✗, −33.0 ✓, 0.5 ✗.

| timestamp | temperature | gradient |
|----------:|------------:|---------:|
| 4.0 | 55.0 | 34.0 |
| 6.0 | 22.0 | -33.0 |

---

#### TV-36: Second-order gradient (acceleration / curvature)

| | |
|---|---|
| Conditional | `true` |
| Selection | `X[0][0], (X[+1][1] - 2.0 * X[0][1] + X[-1][1]) / ((X[0][0] - X[-1][0]) * (X[+1][0] - X[0][0]))` |
| Mode | TRUNCATE |
| Expected rows | 5 (rows 0 and 6 skipped) |

Computes `d²T/dt²` — the second derivative (finite-difference approximation).
Numerator: `f(x+h) - 2f(x) + f(x-h)`. Denominator: `h₁ × h₂` (here all
`= 1.0`). Tests deeply nested arithmetic with 6 cell references, 4 arithmetic
ops, and mixed lookbehind/lookahead.

- Row 1: (21.0 − 2×21.0 + 20.5) / (1.0×1.0) = −0.5
- Row 2: (55.0 − 2×21.0 + 21.0) / (1.0×1.0) = 34.0
- Row 3: (55.0 − 2×55.0 + 21.0) / (1.0×1.0) = −34.0
- Row 4: (22.0 − 2×55.0 + 55.0) / (1.0×1.0) = −33.0
- Row 5: (22.5 − 2×22.0 + 55.0) / (1.0×1.0) = 33.5

| timestamp | accel |
|----------:|------:|
| 2.0 | −0.5 |
| 3.0 | 34.0 |
| 4.0 | −34.0 |
| 5.0 | −33.0 |
| 6.0 | 33.5 |

---

### D.3 Test Vector Summary

| ID | Category | Conditional features | Selection features | Mode | Expected rows |
|:---:|----------|----------------------|-------------------|------|:---:|
| TV-01 | Baseline | `true` literal | Wildcard `[*]` | TRUNC | 7 |
| TV-02 | Baseline | `false` literal | Wildcard | TRUNC | 0 |
| TV-03 | Threshold | Float comparison | Column subset | TRUNC | 2 |
| TV-04 | Edge detect | Lookbehind, `!=` | Multi-row refs | TRUNC | 3 |
| TV-05 | Boundary | Lookbehind, `!=` | Column subset | EXPAND | 3 |
| TV-06 | String | String `==` | String column | TRUNC | 2 |
| TV-07 | String | String `!=` | String column | TRUNC | 3 |
| TV-08 | Short-circuit | `&&`, div-by-zero guard | 3 columns | TRUNC | 3 |
| TV-09 | Short-circuit | `\|\|`, mixed types | Single column | TRUNC | 3 |
| TV-10 | Arithmetic | `true` (selection-only) | Subtraction, delta | TRUNC | 6 |
| TV-11 | Modulo | `%` operator | 2 columns | TRUNC | 4 |
| TV-12 | Bitwise | `&` flag test | 2 columns | TRUNC | 6 |
| TV-13 | Bitwise | `~`, `&`, promotion | 2 columns | TRUNC | 2 |
| TV-14 | Col names | Name-based indexing | Name-based indexing | TRUNC | 2 |
| TV-15 | Negation | `!` operator | 2 columns | TRUNC | 3 |
| TV-16 | Bool context | Numeric-as-bool | 2 columns | TRUNC | 6 |
| TV-17 | Lookahead | `X[+1]`, comparison | 3 columns | TRUNC | 3 |
| TV-18 | Boundary | `X[+1]`, comparison | 3 columns | EXPAND | 3 |
| TV-19 | Window | Lookbehind + lookahead | 2 columns | TRUNC | 0 |
| TV-20 | Promotion | `int + float` | Arithmetic result | TRUNC | 3 |
| TV-21 | Wildcard | Equality | Dual wildcard `X[-1][*], X[0][*]` | TRUNC | 1 |
| TV-22 | Error | String + arithmetic | — | — | compile error |
| TV-23 | Error | String ordering | — | — | compile error |
| TV-24 | Error | Bad column index | — | — | compile error |
| TV-25 | Error | Bad column name | — | — | compile error |
| TV-26 | Error | Div-by-zero / FAIL | — | — | runtime error |
| TV-27 | Error | Div-by-zero / SKIP | — | — | 0 rows |
| TV-28 | Shift | `>>`, `&` | 2 columns | TRUNC | 5 |
| TV-29 | Precedence | `%`, `()` grouping | 2 columns | TRUNC | 3 |
| TV-30 | Interpolation | `true` (selection-only) | Linear midpoint (1st order) | TRUNC | 6 |
| TV-31 | Interpolation | `true` (selection-only) | Quadratic smoothing (2nd order) | TRUNC | 5 |
| TV-32 | Moving avg | `true` (selection-only) | 3-point uniform average | TRUNC | 5 |
| TV-33 | Moving avg | `true` (selection-only) | 5-point average (wide window) | TRUNC | 3 |
| TV-34 | Gradient | `true` (selection-only) | `dT/dt` first derivative | TRUNC | 6 |
| TV-35 | Gradient | `\|\|`, repeated sub-expr | Gradient + threshold filter | TRUNC | 2 |
| TV-36 | Gradient | `true` (selection-only) | `d²T/dt²` second derivative | TRUNC | 5 |

**Coverage check:** Every EBNF production, every opcode category, both boundary
modes, both error policies, all five data types, short-circuit correctness,
type promotion, column-name resolution, and all error paths have at least one
test vector. TV-30–36 additionally cover practical engineering use cases
(interpolation, smoothing, differentiation) that validate the expression
language is powerful enough for real-world signal processing on time-series
data. Missing coverage can be added during implementation (e.g.,
nested parentheses, deeply chained `&&`/`||`, maximum window sizes).
