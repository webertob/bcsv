# BCSV Sampler Implementation Plan
**Date:** March 1, 2026
**Author:** GitHub Copilot (Gemini 3.1 Pro + Claude Opus 4.6 Critiques)
**Status:** DRAFT - Pending Expert Review

## 1. Executive Summary
The `bcsv::Sampler` is a high-throughput, memory-bounded row extraction engine designed for the `bcsv` ecosystem. It enables on-the-fly streaming extraction of data subsets based on user-defined conditionals and column selections. By evaluating these conditions dynamically without holding the entire dataset in memory, the Sampler can process massive datasets (100+ GB) on deeply constrained memory systems (e.g., 500 MB limits).

This document acts as an exhaustive blueprint for the implementation team, defining the architectural constraints, grammar, virtual machine execution pipeline, memory management, and multi-language API bindings.

---

## 2. Architectural Decisions

### 2.1 Compilation Strategy: Single-Threaded Bytecode VM
- **Decision: Custom Bytecode VM (Interpreter) executing sequentially on a single thread.**
- **Rationale:** The `Reader` decoding throughput is the fundamental I/O constraint. A fast, single-threaded VM eliminates interpreter overhead. Concurrency and multi-threading are explicitly avoided to adhere strictly to project non-functional requirements (N7 Thread Safety: "Sampler instances are not shared; no internal locking needed") and because avoiding buffer copying preserves the streaming O(1) memory guarantees.
  
### 2.2 Execution Value Binding: Discriminated Union
- **Decision: Raw structured C-unions with enum Tags wrappers rather than `std::variant`.**
- **Rationale:** Standard `std::variant::visit` overheads are measurable in tight loop dispatch. A bespoke discriminated union directly mapping types (Bool, Int, Float, String-ref) dramatically reduces instruction cache footprint and dispatch costs for billions of row evaluations.

### 2.3 Boundary Modes: Truncate and Expand
- **Decision: Support `TRUNCATE` and `EXPAND`. Explicitly reject `WRAP`.**
- **Rationale:** `WRAP` implies loading the tail of a file while reading the head, which forces infinite memory buffering or erratic seeking, breaking the continuous constrained stream model.

---

## 3. Expression Language Grammar & Type System

A custom expression language is required for users to filter (`setConditional`) and map (`setSelection`) columns. A Pratt parser will be deployed to elegantly handle mathematical precedence rules during the AST construction.

### 3.1 EBNF Grammar Overview

```ebnf
<expression> ::= <logic_or>
<logic_or>   ::= <logic_and> ( "||" <logic_and> )*
<logic_and>  ::= <equality> ( "&&" <equality> )*
<equality>   ::= <comparison> ( ( "==" | "!=" ) <comparison> )*
<comparison> ::= <term> ( ( "<" | "<=" | ">" | ">=" ) <term> )*
<term>       ::= <factor> ( ( "+" | "-" ) <factor> )*
<factor>     ::= <unary> ( ( "*" | "/" ) <unary> )*
<unary>      ::= ( "!" | "-" ) <unary> | <primary>
<primary>    ::= <literal> | <reference> | "(" <expression> ")"

<literal>    ::= <string_literal> | <float_literal> | <int_literal> | "true" | "false"
<reference>  ::= "X" "[" <int_literal> "]" "[" <int_literal> "]" | "X" "[" <int_literal> "]" "[" "*" "]"
```

### 3.2 Parsing and Inference
- **Layout Derivation:** The AST logically infers column types dynamically. E.g. `X[0][1] + X[-1][1]` promotes an Integer and a Double to a Double layout dynamically.
- **Wildcard Expansion:** Evaluators encountering `X[0][*]` iterate the schema bounds automatically, creating 1:N mapped selections reflecting the source schema.
- **Output Names:** Are retained directly from the internal Codec schema headers or implicitly bound via string coercions.

---

## 4. Virtual Machine ISA and Dispatch Pipeline

### 4.1 Instruction Set Architecture (ISA) Layout
The VM instructions are encoded with a **variable-width** pattern (Opcode byte + inline operands) to maximize the amount of logic capable of fitting inside the L1 Instruction Cache (I-Cache residency).

**Sample Opcodes:**
- `LOAD_CONST <idx>`
- `LOAD_CELL <row_offset, col_offset>`
- `ADD_INT`, `ADD_FLOAT`, `MUL_FLOAT`, `ADD_STR` (Type-Specialized)
- `CMP_EQ_INT`, `CMP_GT_FLOAT`
- `JMP_FALSE <offset>`, `JMP_TRUE <offset>` (Short-circuit conditional branching)

### 4.2 Dispatch Loop Implementation
To attain sub-50 nanosecond row evaluations, the central VM dispatcher loop will execute via Computed Gotos (where supported by GCC/Clang) and fallback to a dense unified `switch` loop for MSVC targets.

---

## 5. Ring Buffer and Sliding Window Mechanics

To facilitate `N` rows of lookahead and `M` rows of look-behind memory safely, `bcsv` requires a sliding boundary engine.

### 5.1 The Ring Buffer
A pre-allocated bounded buffer of exactly `|M| + N + 1` size (e.g. 3 array elements if referencing `X[-1]` to `X[1]`). References fetch modulo the head tracker to bypass array shifts.

### 5.2 Filling Phase
Prior to successful evaluation of the *first* iteration of `next()`, the reader sequentially reads `N + 1` rows to ensure Lookahead conditions are primed correctly.

### 5.3 Boundary Evaluation & Draining Phase
Boundary behaviors apply when looking out-of-bounds at the absolute start or absolute EOF limits.
- **Truncate:** Immediately discards the element evaluation logic and skips yielding any row where external accesses fail. Lookahead at EOF immediately short-circuits.
- **Expand:** Clamps the out-of-bounds index dynamically to the closest existing edge object continuously (e.g. `X[-1]` on Row 0 points symmetrically mapped at Row 0).

---

## 6. Multi-Language API Contracts

### 6.1 Native C++ Target
```cpp
struct SamplerCompileResult { bool success; std::string error_msg; size_t error_position; };

template <typename LayoutType>
class Sampler {
public:
    SamplerCompileResult setConditional(const std::string& conditional);
    SamplerCompileResult setSelection(const std::string& selection);
    void setBoundaryMode(BoundaryMode mode);
    const bcsv::Layout& outputLayout() const;
    const bcsv::Row& row() const;
    bool next();
    size_t sourceRowPos() const; // Traceability index mapping output 
};
```

### 6.2 C Handle FFI
The authoritative target for ABI-stable foreign bindings must utilize opaque handles and proper error buffers.
```c
typedef struct bcsv_sampler_t bcsv_sampler_t;
bcsv_sampler_t* bcsv_sampler_create(bcsv_reader_t* reader);
void bcsv_sampler_destroy(bcsv_sampler_t* sampler);
int bcsv_sampler_set_conditional(bcsv_sampler_t* sampler, const char* expr, bcsv_error_t* err);
int bcsv_sampler_set_selection(bcsv_sampler_t* sampler, const char* expr, bcsv_error_t* err);
const bcsv_row_t* bcsv_sampler_row(const bcsv_sampler_t* sampler);
int bcsv_sampler_next(bcsv_sampler_t* sampler);
```

### 6.3 Python Wrapper (pybind11)
Crucially designed to respect maximum RAM deployments by avoiding unbounded file dumps.
```python
class Sampler:
    def __iter__(self): ...
    def bulk(self) -> List[Tuple]: ...
    # Exposes memory-safe iterator streams directly to Pandas/Numpy representations
    def to_dataframe(self, chunk_size: int = 100_000): ...
```

### 6.4 C# Interoperability (P/Invoke)
Implementation requires extending the `IDisposable` pattern securely around the `IntPtr` C handles.
```csharp
public class Sampler : IDisposable {
    private IntPtr handle_;
    public void SetConditional(string expr);
    public void SetSelection(string expr);
    public bool Next();
    public Row Current { get; }
    public void Dispose();
}
```

---

## 7. Fast CLI Tooling
Command line integration maps standard Linux pipes over standard arguments securely.
- Usage: `cat massive.bcsv | bcsvSampler --cond "X[0][1] > 50" --sel "X[0][*]" --mode TRUNCATE | bcsv2csv > sample.csv`
- Error outputs target strictly `stderr`, preserving valid schema rows exclusively to `stdout`.

---

## 8. Performance Budget & Testing Matrix

### 8.1 Evaluated Target Performance Budget
Against the official N3 architectural requirements:
- **Lexing / Compile Time Overhead:** strictly `< 100 μs`.
- **Per-Row Evaluation Overhead:** strictly `< 50 ns` dynamically mapped payload checking.
- **IO Scaling:** Throughput `next()` strictly `>= 90%` of raw file reading speed.
- **System Footprint:** Total O(1) Sampler structure padding & Buffer strictly smaller than `100 KB` peak. 

### 8.2 Security, Fuzzing & Memory Testing
- **Fuzzing Operations:** Implementing randomized garbage input testing suites natively fed to the Lexer / AST generation stack to isolate regression crashes.
- **Strict Memory Analysis:** Valgrind and Massif will evaluate all pipelines automatically running simulated memory constraints to guarantee the fixed allocation sizes are natively bounded and explicitly immune to out-of-memory cascading faults.
