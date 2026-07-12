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

## Internal Threading: the Batch File Codec

One component is internally multi-threaded: `FileCodecPacketLZ4Batch001`
(selected by `FileFlags::BATCH_COMPRESS`, the default codec) owns a single
background `std::jthread` that performs packet compression/decompression and
all stream I/O. This does **not** change the public contract — `Writer` and
`Reader` remain single-threaded objects — but it imposes internal rules:

- **Stream ownership**: while a background task is in flight, the background
  thread exclusively owns the file stream. The main thread must not touch it,
  not even to poll `rdstate()`. `Reader::readNext()` therefore queries
  liveness via `FileCodecDispatch::readGood()`, which the batch codec answers
  from main-thread-owned state.
- **Error propagation**: exceptions on the background thread are captured in
  an `std::exception_ptr` guarded by the codec mutex and rethrown on the main
  thread at the next synchronization point — packet boundary, `flush()`, or
  `close()`. They are never checked on the per-row fast path (no locking per
  row). `finalize()` rethrows unconditionally before writing the footer, so a
  file with a failed packet never receives a clean footer.
- **EOF signalling**: end-of-data is communicated exclusively through codec
  state (`bg_has_next_packet_`), never through stream flags; the background
  thread restores a defined stream state on every task exit.

The `clang-tsan` CMake preset builds the test suite under ThreadSanitizer to
guard these invariants:

```bash
cmake --preset clang-tsan
cmake --build --preset clang-tsan-build --target bcsv_gtest bcsvRepair -j
./build/clang-tsan/bin/bcsv_gtest --gtest_filter='*Batch*:*LZ4*:FileCodec*'
```
