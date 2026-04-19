# Architectural Decision Records (ADRs)

This directory records significant design decisions for the BCSV project using
lightweight [ADR format](https://adr.github.io/). Each decision is numbered
sequentially and captures the context, options considered, and rationale.

**Status values:** `Accepted` | `Superseded` | `Deprecated`

## Index

| ADR | Title | Status | Date |
|-----|-------|--------|------|
| [001](0001-error-model-bool-plus-exceptions.md) | Dual error model: bool returns + exceptions | Accepted | 2026-04-19 |
| [002](0002-no-version-compatibility-check-on-read.md) | No explicit version compatibility check on read | Accepted | 2026-04-19 |
| [003](0003-little-endian-only-wire-format.md) | Little-endian-only wire format | Accepted | 2026-04-19 |
| [004](0004-uint32-for-within-row-offsets.md) | uint32_t for within-row offsets | Accepted | 2026-04-19 |
| [005](0005-vle-pessimistic-buffer-allocation.md) | VLE pessimistic buffer allocation strategy | Accepted | 2026-04-19 |
