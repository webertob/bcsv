# ADR-004: uint32_t for Within-Row Offsets

**Status:** Accepted  
**Date:** 2026-04-19  
**Review:** v1.5.6 critical review (finding #11)

## Context

BCSV uses `uint32_t` for column byte offsets within a row (`Layout::offsets_`,
`ColMeta::dataOffset`) and for the on-wire `packet_size` field. A reviewer
flagged potential `uint32_t` truncation if these values exceed 4 GB.

## Decision

**Keep `uint32_t`.** The types are correctly sized for their domain constraints.

### Analysis

| Field | Type | Max value | Domain constraint | Headroom |
|-------|------|-----------|-------------------|----------|
| Column offset within row | `uint32_t` | 4 GB | `MAX_ROW_LENGTH` = ~16 MB | 250× |
| Packed column offset | `uint32_t` | 4 GB | Same | 250× |
| `packet_size` (wire) | `uint32_t` | 4 GB | `MAX_PACKET_SIZE` = 1 GB | 4× |
| `row_cnt_` (Writer) | `uint64_t` | — | Unlimited | ∞ |
| `dataSize` (Row) | `uint32_t` | 4 GB | `MAX_ROW_LENGTH` = ~16 MB | 250× |

All `uint32_t` usages represent sizes that are structurally bounded well below
2³² by format constants. Changing them to `size_t` would increase memory usage
(8 bytes per offset on 64-bit) for zero practical benefit, and would change the
wire format for `packet_size`.

## Consequences

- If `MAX_ROW_LENGTH` is ever raised above 4 GB, these types must be widened.
  This is extremely unlikely for a time-series format (16 MB rows already
  support 65535 columns × 256 bytes each).
- The `packet_size` wire field is fixed in the binary format and cannot be
  widened without a format version bump.
