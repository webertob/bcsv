# ADR-001: Dual Error Model — bool Returns + Exceptions

**Status:** Accepted  
**Date:** 2026-04-19  
**Review:** v1.5.6 critical review (finding #13)

## Context

BCSV uses two error-reporting mechanisms:

- **I/O operations** (`open`, `readNext`, `writeRow`) return `bool` and populate
  an error string accessible via `getErrorMsg()`. This allows callers to handle
  expected failures (file not found, corrupt data) without exception overhead.
- **Logic errors** (out-of-range column index, type mismatch, schema violations)
  throw C++ exceptions. These represent programming mistakes, not runtime
  conditions, and should surface immediately.

A reviewer noted that mixing `bool` and exceptions creates an inconsistent error
model where callers cannot rely on a single pattern.

## Decision

**Keep the dual model.** The distinction is intentional and matches the
project's design goals:

1. **Embedded / real-time targets** benefit from exception-free I/O paths.
   Streaming writes in a data-acquisition loop must never unwind.
2. **Logic errors** (wrong column type, schema mismatch) indicate bugs that
   should fail loudly. Silent `false` returns for these would mask programming
   mistakes.
3. The C API cannot use exceptions at all — `bool` returns are mandatory there,
   and the C++ API should be consistent with it for I/O operations.

## Consequences

- Callers must check `bool` returns for I/O and catch exceptions for logic
  errors. This is documented in `docs/ERROR_HANDLING.md`.
- The Python and C# bindings unify both paths into exceptions (idiomatic for
  those languages), so cross-language users see a uniform model.
