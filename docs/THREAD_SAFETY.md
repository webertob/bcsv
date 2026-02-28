# BCSV Thread Safety Model

## Overview

BCSV is a **single-threaded** library. All public types (`Layout`, `Writer`,
`Reader`, `Row`, `FileHeader`, `FileFooter`, etc.) must be accessed from a
single thread at a time. No internal locking or atomic synchronisation is
provided on the data path.

## Rationale

The library is designed for maximum throughput on time-series telemetry data.
Adding mutex or atomic overhead on every row write/read would measurably hurt
the hot path for the primary use case (high-frequency sensor logging).

## Structural Lock

`Layout::Data` contains a plain `uint32_t` counter
(`structural_lock_count_`) that prevents accidental schema mutations while a
`Writer` or `Reader` holds the layout open. This is a *logical* lock (an
assertion guard), **not** a thread-safety mechanism.

- `LayoutGuard` increments the counter on construction, decrements on
  destruction.
- `Layout::addColumn()` / `Layout::clear()` throw if the counter is > 0.
- The counter is **not** atomic; concurrent `LayoutGuard` creation from
  multiple threads is undefined behaviour.

## Safe Usage Patterns

| Pattern | Safe? | Notes |
|---------|-------|-------|
| Single-threaded write → close → single-threaded read | ✅ | Normal usage |
| Separate `Writer` instances, each on its own thread, each owning its own `Layout` | ✅ | No shared state |
| Multiple threads sharing the same `Layout` and/or `Writer` | ❌ | Data race |
| Reading a file from one thread while another writes to it | ❌ | File-level conflict |

## Embedding in Multi-Threaded Applications

If your application requires concurrent access:

1. **Serialise access** with an external mutex around `Writer`/`Reader` calls.
2. **Use per-thread instances** — each thread creates its own `Layout`,
   `Writer`, and `Reader` objects writing to separate files.
3. **Post-merge** — write per-thread files and merge them after all threads
   complete.

## Future Considerations

If thread-safe operation becomes necessary in the future, the recommended
approach is a thin wrapper class (`ThreadSafeWriter`) that wraps a
`std::mutex` around the public API, rather than adding locking to the core
library.
