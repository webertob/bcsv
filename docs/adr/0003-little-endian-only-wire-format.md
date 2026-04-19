# ADR-003: Little-Endian-Only Wire Format

**Status:** Accepted  
**Date:** 2026-04-19  
**Review:** v1.5.6 critical review (finding #26)

## Context

BCSV's binary wire format stores all multi-byte integers in little-endian order
and performs no byte-swapping on read or write. The `#pragma pack(push, 1)`
structs (e.g. `ConstSection`) are read directly via `reinterpret_cast`. This
means BCSV files are **not portable to big-endian architectures** (e.g.
SPARC, older PowerPC, some network processors).

A reviewer flagged this as a portability risk.

## Decision

**Keep little-endian-only.** Do not add byte-swapping logic.

### Rationale

1. **All target platforms are little-endian.** x86/x64, ARM (AArch64), Apple
   Silicon, STM32, Zynq/Versal — every platform in the project's scope is LE.
   Big-endian desktop/server hardware is effectively extinct in the target market
   (metrology, data acquisition, telemetry).

2. **Zero-overhead on the hot path.** No byte-swap branches or `hton`/`ntoh`
   calls in the serialize/deserialize loops. For a library targeting 3.6–7.5M
   rows/sec, even a single extra branch per column matters.

3. **Simplicity.** The wire format maps directly to C++ memory layout on LE
   platforms, enabling `memcpy`-based I/O and `reinterpret_cast` for header
   reads. Byte-swapping would complicate every codec implementation.

4. **`ARCHITECTURE.md` NFR2** explicitly states "Little-endian file format
   (dominant platform)" as a design choice.

## Consequences

- BCSV files cannot be read on big-endian hardware without a conversion tool.
- If a BE platform becomes a target in the future, a `bswap` layer can be added
  in the codec (not in the format) as a new file flag or codec variant.
- Cross-compilation for LE embedded targets (ARM, Zynq) remains zero-overhead.
