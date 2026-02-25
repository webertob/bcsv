# BCSV Architecture & Requirements

**Binary CSV for Time-Series Data**  
Technical design, requirements, and implementation roadmap

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Performance Requirements](#performance-requirements)
3. [User Requirements](#user-requirements)
4. [File Format Specification](#file-format-specification)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Design Decisions](#design-decisions)
7. [Optimization Techniques](#optimization-techniques)

---

## Design Philosophy

### Core Principles

**1. Simplicity First**
- No schema definition files (unlike Protocol Buffers, FlatBuffers)
- Define data structures directly in code (C++, Python, C#)
- Self-documenting files (header contains all type information)
- Feel natural to programmers familiar with CSV

**2. Streaming Architecture**
- Process files larger than available RAM
- Read/write row-by-row without full file buffering
- Constant-time operations for real-time recording
- Support embedded platforms with limited memory

**3. Time-Series Optimized**
- Efficient compression for constant values (Zero-Order Hold)
- Binary waveform compression
- Sparse recording/event-based data
- Timestamp and counter column optimizations

**4. Crash Resilience**
- Retrieve data from incomplete/interrupted writes
- Read up to last fully written packet
- Packet-based architecture for fault isolation
- Checksum validation for data integrity

**5. Performance Balance**
- Compression vs computation trade-off
- Storage efficiency vs access speed
- Sequential optimization with acceptable random access

---

## Performance Requirements

### Target Platforms & Workloads

| Platform | CPU | RAM | Target Workload |
|----------|-----|-----|-----------------|
| **STM32F4** | 168 MHz Cortex-M4 | 192 KB | 1000 channels @ 1 KHz |
| **STM32F7** | 216 MHz Cortex-M7 | 512 KB | 1000 channels @ 10 KHz |
| **Zynq-7000** | Dual ARM A9 @ 866 MHz | 512 MB | 1000 channels @ 10 KHz |
| **Raspberry Pi** | ARM A53/A72 | 1-8 GB | 1000 channels @ 10 KHz |
| **Desktop (Zen3)** | 3.5+ GHz | 16+ GB | ≥1M rows/sec processing |

### Performance Targets

#### Sequential Recording

| Metric | Target | Status | Notes |
|--------|--------|--------|-------|
| **STM32F4 throughput** | 1000 ch @ 1 KHz | ✅ Achievable | 32-bit float per channel |
| **STM32F7 throughput** | 1000 ch @ 10 KHz | ✅ Achievable | With streaming compression |
| **Zynq/RPi throughput** | 1000 ch @ 10 KHz | ✅ Achievable | Dual-core ARM |
| **Write latency (P99)** | <1 ms | 🔄 v1.3.0 | Streaming LZ4 required |
| **Write latency (mean)** | <100 μs | ✅ Current | Batch compression |

#### File Size Efficiency

| Metric | Target | Status | Implementation |
|--------|--------|--------|----------------|
| **Idle file growth** | <1 KB/s (1000ch@10KHz) | ✅ With ZoH | Counter-only recording |
| **Compression ratio** | <30% of CSV | ✅ 15-25% typical | LZ4 + type optimization |
| **ZoH compression** | <5% of CSV | ✅ 3-4% typical | Sparse/constant data |
| **Packet overhead** | <2% | ✅ 0.3-1% | 20-byte header per 8MB |

#### Read Performance

| Metric | Target | Status | Platform |
|--------|--------|--------|----------|
| **Desktop sequential** | ≥1M rows/sec | ✅ 127K-220K | Zen3 CPU |
| **Random access latency** | <10 ms | 🔄 v1.4.0 | Requires file index |
| **Decompression speed** | ≥500 MB/s | ✅ ~650 MB/s | LZ4 decompression |
| **Checksum validation** | ≥10 GB/s | ✅ ~13 GB/s | xxHash64 |

### Computational Complexity

| Operation | Complexity | Time (typical) | Memory |
|-----------|-----------|----------------|--------|
| **Write row** | O(columns) | 0.5-1.5 μs | Row buffer only |
| **Read row** | O(columns) | 0.4-1.0 μs | Row buffer only |
| **Compress packet** | O(packet_size) | 10-30 ms | 16 MB peak |
| **Decompress packet** | O(packet_size) | 5-15 ms | 16 MB peak |
| **Count rows** | O(packets) | <1 ms | Header only |
| **Random seek** | O(log packets) | <10 ms | Index + packet |

---

## User Requirements

### Functional Requirements

#### FR1: Schema Definition
- ✅ Define layout in code (no external schema files)
- ✅ Support 12 data types (bool, int8-64, uint8-64, float, double, string)
- ✅ Column names embedded in file header
- ✅ Type enforcement for all rows (no mixed types per column)
- ✅ Maximum 65,535 columns per file

#### FR2: Data I/O
- ✅ Sequential row-by-row read/write
- ✅ Files larger than available RAM
- 🔄 Random access by row index (v1.4.0)
- ✅ Crash recovery (read last complete packet)
- ✅ Append to existing files

#### FR3: Data Integrity
- ✅ Checksum validation (xxHash64)
- ✅ Packet-based fault isolation
- ✅ Detect corrupted packets
- ✅ Resilient mode (skip bad packets, continue reading)

#### FR4: Compression
- ✅ Automatic LZ4 compression
- ✅ Zero-Order Hold (ZoH) for constant values
- 🔄 Streaming compression (v1.3.0)
- 🔄 Variable-Length Encoding (v1.5.0)
- 🔄 Dictionary encoding for strings (v1.5.0)

#### FR5: Multi-Language Support
- ✅ C++ (header-only library)
- ✅ C API (shared library .dll/.so)
- ✅ Python (pandas integration)
- ✅ C# (Unity integration)
- ✅ CLI tools (csv2bcsv, bcsv2csv)

### Non-Functional Requirements

#### NFR1: Usability
- **No external tools required** for schema generation
- **Single header include** for C++ usage
- **Self-documenting files** (schema in header)
- **Intuitive API** similar to CSV workflows
- **Comprehensive examples** and documentation

#### NFR2: Portability
- **C++20 standard** (no compiler-specific extensions)
- **Cross-platform** (Windows, Linux, macOS, embedded)
- **No external dependencies** (LZ4 and xxHash embedded)
- **Little-endian** file format (dominant platform)

#### NFR3: Maintainability
- **Header-only library** (easy integration)
- **Modern C++** (concepts, templates, smart pointers)
- **Comprehensive tests** (continuously validated in CI)
- **Automated versioning** (git tag-based)
- **CI/CD pipeline** (GitHub Actions)

#### NFR4: Performance
- **Zero-copy design** where possible
- **Template metaprogramming** for compile-time optimization
- **Memory pooling** for packet buffers
- **SIMD-friendly** data layouts (future)
- **Profiling hooks** for optimization

---

## File Format Specification

### Version 1.2.0 (Current Development)

```
┌─────────────────────────────────────────────────────────┐
│                     File Header                         │
│ ┌─────────────────────────────────────────────────────┐ │
│ │ Magic: "BCSV" (0x56534342)                          │ │
│ │ Version: 1.2.0                                      │ │
│ │ Compression: LZ4 level (1-9)                        │ │
│ │ Flags: (ZoH, wide rows, etc.)                       │ │
│ │ Column count: N                                     │ │
│ │ Column types: [UINT16] × N                          │ │
│ │ Column name lengths: [UINT16] × N                   │ │
│ │ Column names: concatenated UTF-8 strings            │ │
│ └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│                    Packet Header                        │
│ ┌─────────────────────────────────────────────────────┐ │
│ │ Magic: "PCKT" (0x54)                                │ │
│ │ First row index: UINT64                             │ │
│ │ Row count: UINT32                                   │ │
│ │ Payload size: UINT32                                │ │
│ │ Checksum: UINT64 (xxHash64)                         │ │
│ └─────────────────────────────────────────────────────┘ │
│                                                         │
│ ┌─────────────────────────────────────────────────────┐ │
│ │           Row Lengths (UINT16 × N-1)                │ │
│ │  (last row length implicit from payload size)       │ │
│ └─────────────────────────────────────────────────────┘ │
│                                                         │
│ ┌─────────────────────────────────────────────────────┐ │
│ │            Compressed Payload (LZ4)                 │ │
│ │  ┌───────────────────────────────────────────────┐  │ │
│ │  │ Row 1: [bits_][data_][strg_lengths][strg_data]│  │ │
│ │  │ Row 2: [bits_][data_][strg_lengths][strg_data]│  │ │
│ │  │  ...                                          │  │ │
│ │  │ Row N: [bits_][data_][strg_lengths][strg_data]│  │ │
│ │  └───────────────────────────────────────────────┘  │ │
│ └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
[Packet Header]
[Row Lengths]
[Compressed Payload]
...
[Repeat for all data]
```

### Data Alignment

**Design Decision**: **8-bit (1-byte) alignment** for file format

**Rationale**:
- Minimize file size (primary goal)
- Simple offset calculation
- Portable across all platforms
- Higher-order alignment used only for temporary/in-memory data

### Row Wire Format (Flat Encoding)

Each row is serialized as four consecutive sections:

```
[bits_][data_][strg_lengths][strg_data]
```

| Section | Size | Content |
|---------|------|---------|
| `bits_` | ⌈bool_count / 8⌉ bytes | Bit-packed boolean values in layout order |
| `data_` | Σ sizeOf(scalar_type) | Tightly packed scalars (no alignment padding) |
| `strg_lengths` | string_count × 2 bytes | uint16_t length per string column |
| `strg_data` | Σ string lengths | Concatenated string payloads |

Sections with zero elements contribute zero bytes.
Scalar values are packed with no alignment — access uses memcpy.
Boolean values use 1 bit each (not 1 byte). String offsets are
derived from cumulative sum of lengths (no explicit offsets stored).

**In-memory processing**:
- Row buffers use natural alignment (8/16-byte)
- Compiler optimizations enabled
- SIMD-friendly layouts (future)

### Row Codec Layer

Serialization and deserialization of rows is handled by codec classes that are
separate from `Row` and `Layout`. This decouples wire-format knowledge from
in-memory data storage.

#### Architecture

The file codec determines how rows are encoded on disk:

```
Writer ──── RowCodecType ──▶ RowCodecFlat001  or  RowCodecZoH001
            (compile-time)   (Writer knows what it writes)

Reader ──── CodecDispatch ──▶ RowCodecFlat001  or  RowCodecZoH001
            (runtime)         (file flags determine codec)
```

#### Codec Classes

| Class | File | Encoding | State |
|-------|------|----------|-------|
| `RowCodecFlat001<Layout>` | `row_codec_flat001.h/hpp` | Dense flat encoding | Wire metadata + per-column offsets |
| `RowCodecZoH001<Layout>` | `row_codec_zoh001.h/hpp` | Zero-Order-Hold delta | Composes `RowCodecFlat001` for first-row; internal `wire_bits_` for change header |
| `CodecDispatch<Layout>` | `row_codec_dispatch.h` | Runtime dispatch | Union storage + function pointers |

Each codec provides:
- `setup(layout)` — compute wire-format metadata from the layout
- `serialize(row, buffer) → span<byte>` — encode a row to wire format
- `deserialize(span, row)` — decode wire format into a row
- `reset()` — clear per-packet state (ZoH change tracking)

`setup()` also acquires a `LayoutGuard` (RAII, defined in `layout_guard.h`)
that increments a reference counter on `Layout::Data`.  While any guard is
held, the six structural mutation methods (`addColumn`, `removeColumn`,
`setColumnType`, `setColumns`, `clear`) throw `std::logic_error`.
`setColumnName` is excluded — it does not affect wire metadata.  The guard is
released automatically when the codec is destroyed or move-assigned.

Wire-format metadata (`wireBitsSize`, `wireDataSize`, `wireStrgCount`,
`wireFixedSize`, per-column `offsets_[]`) is owned exclusively by the codec.
`Layout` and `Row` classes contain no wire-format knowledge.

#### Codec Selection

**Writer** holds `RowCodecType<Layout>` — a compile-time selected codec.
The Writer knows what format it writes. All serialize calls are direct member
function calls, fully inlined.

**Reader** holds `CodecDispatch<Layout>` — runtime codec selection via
function pointers. At `open()` time, `CodecDispatch::selectCodec(flags, layout)`
reads the file's `ZERO_ORDER_HOLD` flag, constructs the correct codec in union
storage via placement new, and wires function pointers. Subsequent `deserialize()`
calls go through a single indirect call — branch predictor learns the target
after the first row.

#### Naming Convention

`RowCodec` + `Format` + `Version`:
- `Flat001` — dense flat encoding, version 001
- `ZoH001` — zero-order-hold, version 001
- Future formats (e.g., `Delta001`, `CSV001`) follow the same pattern.

#### Row ↔ Codec Access Pattern

Codecs access Row internals (`bits_`, `data_`, `strg_`) via `friend`
declarations. This narrow internal boundary avoids polluting Row's public API
with wire-format-specific accessors. Each codec class is tightly co-designed
with Row's three-container storage layout.

Static-layout codecs (`RowCodecFlat001<LayoutStatic<Ts...>, P>`) use
`constexpr` wire metadata computed at compile time — zero runtime setup cost.

### Packet Size Strategy

| Size | Sequential | Random | Compression | Decision |
|------|-----------|---------|-------------|----------|
| 16 KB | ✅ Fast | ❌ Poor | ⚠️ OK | Too small |
| 32-64 KB | ⚠️ Neither | ⚠️ Neither | ⚠️ OK | **Avoid** |
| 256 KB | ✅ Good | ⚠️ Fair | ✅ Good | v1.1.x default |
| 4 MB | ✅ Excellent | ✅ Good | ✅ Excellent | v1.2.0+ default |
| 8 MB | ✅ Excellent | ✅ Excellent | ✅ Excellent | **v1.3.0+ default** |

**Current implementation** (v1.2.0): 256 KB default, configurable  
**Planned** (v1.3.0+): 8 MB default for optimal compression and random access

---

## Implementation Roadmap

### Phase 1: Foundation (v1.2.0) ✅ **Current**

**Status**: Complete (Nov 2025)

**Changes**:
- ✅ Replaced CRC32 with xxHash64 (3-5x faster)
- ✅ Removed Boost dependency (zero external deps)
- ✅ Upgraded to C++20 (concepts, requires)
- ✅ Fixed all tests (59/59 passing)
- ✅ Updated documentation

**Breaking changes**: File format incompatible with v1.1.x (checksum algorithm)

---

### Phase 2: Streaming Compression (v1.3.0) 🔄 **Dec 2025**

**Goal**: Constant write latency for real-time recording

**Current problem**:
- Batch compression causes write spikes (10-30 ms)
- P99 latency unacceptable for real-time systems
- Buffering delays data persistence

**Solution**: Stream-based LZ4 compression

```cpp
// New PacketHeader structure (20 bytes)
struct PacketHeaderV2 {
    char magic[4];           // "PCKT"
    uint64_t firstRowIndex;  // Absolute row index
    uint32_t prevChecksum;   // Chain validation
    uint32_t headerChecksum; // xxHash64 (lower 32 bits)
};

// Row encoding (per-row overhead: 2-4 bytes)
struct EncodedRow {
    uint16_t length;         // Compressed row size
                            // 0 = ZoH repeat
                            // 0xFFFF = packet end
    char data[length];       // LZ4 stream compressed data
};
```

**Benefits**:
- ✅ Constant-time writeRow() (no spikes)
- ✅ Better compression (LZ4 stream preserves context)
- ✅ Robust (read partial packets)
- ⚠️ Trade-off: 2-4 bytes overhead per row

**Implementation tasks**:
1. Design stream-based packet format
2. Implement LZ4 streaming encoder/decoder
3. Add length-prefix encoding for rows
4. Performance testing vs batch compression
5. Update Writer/Reader classes
6. Comprehensive testing (edge cases, corruption)

---

### Phase 3: File Indexing (v1.4.0) 🔄 **Jan 2026**

**Goal**: Fast random access (<10 ms for any row)

**Index structure** (appended at EOF):

```cpp
struct FileFooter {
    char startMagic[4];              // "BIDX"
    
    struct PacketIndexEntry {
        uint64_t headerOffset;        // File offset to PacketHeader
        uint64_t firstRowIndex;       // First row in packet
    };
    PacketIndexEntry packets[N];     // One entry per packet
    
    char endMagic[4];                // "EIDX"
    uint32_t indexStartOffset;       // Bytes from EOF to startMagic
    uint64_t lastRowIndex;           // Total rows in file
    uint32_t indexChecksum;          // xxHash64 of index
};
```

**Benefits**:
- ✅ O(log N) random access via binary search
- ✅ Instant row count (no file scan)
- ✅ Optional (backward compatible)
- ✅ Small overhead (~24 bytes per packet = ~24KB for 1000 packets)

**Implementation tasks**:
1. Design index structure
2. Writer: maintain packet offset list, append on close()
3. Reader: detect and load index on open()
4. Implement seek(rowIndex) and readAt(rowIndex)
5. Binary search for row lookup
6. ZoH handling (scan backward for actual data)

---

### Phase 4: Variable-Length Encoding (v1.5.0) 🔄 **Feb 2026**

**Goal**: 20%+ compression improvement on time-series data

**⚠️ Complexity Warning**: Major undertaking, consider deferring

**Encoding scheme** (bit-packed, non-byte-aligned):

```
Row Header (per column):
  Bit 0: Repetition flag
    0 = New encoding info follows
    1 = Same encoding as previous row
  
  Bit 1-2: Encoding mode (if bit 0 == 0)
    00 = CONST (value unchanged)
    01 = PLAIN (raw value)
    10 = EXTRAPOLATE (2nd order hold)
    11 = DELTA (1st order hold)
  
  Bit 3-5: Length field (variable width)
    1-byte types: 0 bits (implicit)
    2-byte types: 1 bit  (1-2 bytes)
    4-byte types: 2 bits (1-4 bytes)
    8-byte types: 3 bits (1-8 bytes)
  
  Bit 6+: Data payload (variable length)
```

**Column hints** (metadata for optimization):

```cpp
enum class ColumnHint : uint8_t {
    NONE        = 0x00,
    VOLATILE    = 0x01,  // Arbitrary changes, minimal compression
    INDEX       = 0x02,  // Relationship with row number (e.g., timestamp)
    MONOTONIC   = 0x04,  // Nearly constant rate of change
    UNIQUE      = 0x08,  // No duplicates
    ASCENDING   = 0x10,  // Non-decreasing
    DESCENDING  = 0x20,  // Non-increasing
};
```

**Implementation tasks**:
1. Design bit-packing specification
2. Implement BitWriter/BitReader utilities
3. Encoding decision logic (heuristics)
4. Column hints system
5. Extensive testing (type boundaries, alignment)
6. Performance benchmarks (encoding time vs compression)
7. Document when to use RAW_MODE instead

---

### Phase 5: Advanced Compression (v1.6.0) 🔄 **Mar 2026**

**Goal**: State-of-the-art compression for specific data patterns

**String dictionary compression**:
- Per-packet string dictionary
- 16-bit indices replace strings
- Automatic overflow handling
- Variable-length integer encoding for indices

**Integer optimizations**:
- ZigZag encoding for signed integers
- Protobuf-style varint encoding
- Apply to string addresses, row lengths

**Float compression**:
- CHIMP algorithm (minimal bit-flip encoding)
- GORILLA algorithm (XOR-based compression)
- Evaluate trade-offs (compression vs decode speed)

---

### Phase 6: Stable Release (v2.0.0) 🎯 **Q2 2026**

**Goal**: Production-ready with compatibility guarantees

**Changes**:
- Change magic to "BCS2" (indicate v2.0 format)
- Establish semantic versioning guarantee
- Create v1.x → v2.0 migration tools
- Full documentation overhaul
- Performance validation against all targets
- Community feedback integration

**Compatibility policy**:
- v2.x.y: Patch versions fully compatible
- v2.x: Minor versions backward compatible (read older files)
- v3.0: Major version may break compatibility (provide migration)

---

## Design Decisions

### 1. Why xxHash64 over CRC32?

| Metric | CRC32 | xxHash64 | Winner |
|--------|-------|----------|--------|
| **Speed (desktop)** | ~3-4 GB/s | ~13 GB/s | xxHash64 (3-5x) |
| **Speed (STM32F4)** | ~3-8 MB/s | ~15-25 MB/s | xxHash64 (3-5x) |
| **Collision resistance** | Good | Excellent | xxHash64 |
| **Output size** | 32-bit | 64-bit | xxHash64 |
| **Dependencies** | Boost | None | xxHash64 |

**Decision**: xxHash64 for all versions ≥1.2.0

---

### 2. Why LZ4 over other compressors?

| Compressor | Speed | Ratio | Embedded | Decision |
|------------|-------|-------|----------|----------|
| **LZ4** | ✅ Very fast | ✅ Good | ✅ Yes | **Selected** |
| zstd | ⚠️ Medium | ✅ Excellent | ⚠️ Maybe | Future option |
| gzip | ❌ Slow | ✅ Good | ✅ Yes | Too slow |
| bzip2 | ❌ Very slow | ✅ Excellent | ❌ No | Too slow |
| Snappy | ✅ Fast | ⚠️ Fair | ✅ Yes | Similar to LZ4 |

**Decision**: LZ4 for v1.x, consider zstd as option in v2.0+

---

### 3. Why header-only library?

**Advantages**:
- ✅ Easy integration (just copy include/)
- ✅ No ABI compatibility issues
- ✅ Compiler can optimize across boundaries
- ✅ No separate compilation step
- ✅ Template metaprogramming (compile-time optimization)

**Disadvantages**:
- ⚠️ Longer compile times
- ⚠️ Code bloat if used in many translation units
- ⚠️ Binary size increase

**Mitigation**:
- Provide C API shared library (.dll/.so) for:
  - Other languages (Python, C#)
  - Reducing binary size
  - Stable ABI for plugins

---

### 4. Why packet-based architecture?

**Advantages**:
- ✅ Fault isolation (corrupted packet doesn't affect others)
- ✅ Random access (seek to packet boundary)
- ✅ Streaming (process one packet at a time)
- ✅ Parallel processing (decompress packets concurrently)
- ✅ Crash recovery (last complete packet readable)

**Disadvantages**:
- ⚠️ Packet header overhead (~20 bytes per packet)
- ⚠️ Compression boundary (reset LZ4 context)

**Trade-off**: 8 MB packets = 20 bytes / 8 MB = 0.00024% overhead

---

## Optimization Techniques

### 1. Zero-Order Hold (ZoH)

**Use case**: Constant or sparse data

**Implementation**:
- Flag row as "repeat previous value"
- Store flag in row length array (length = 0)
- Skip compression for repeated rows

**Results**:
- ✅ 96% compression for constant data
- ✅ Minimal CPU overhead
- ✅ Works with all data types

---

### 2. Type Optimization

**Use case**: CSV conversion with unknown types

**Strategy**:
- Analyze all values in column
- Select smallest type that fits all values
- Example: "255" → UINT8 instead of INT64

**Results**:
- ✅ 87.5% space reduction for small integers
- ✅ Automatic in csv2bcsv tool
- ✅ Manual override available

---

### 3. Template Metaprogramming

**Use case**: Known schema at compile time

**Strategy**:
- `LayoutStatic<int32_t, std::string, float>`
- Compiler generates optimal code
- No runtime type checks

**Results**:
- ✅ 4-5x faster than flexible interface
- ✅ Zero runtime overhead
- ✅ Type safety at compile time

---

### 4. Memory Pooling (Future)

**Use case**: Reduce allocation overhead

**Strategy**:
- Pre-allocate packet buffers
- Reuse between packets
- Thread-local pools for parallelism

**Expected results**:
- ✅ 10-20% faster writes
- ✅ Reduced memory fragmentation
- ✅ Better cache locality

---

## Profiling & Benchmarking

### Benchmark Suite

**Included benchmarks**:
1. `performance_benchmark.cpp` - Write/read speed
2. `large_scale_benchmark.cpp` - Scaling behavior
3. `csv2bcsv` tool - Real-world conversion
4. Google Test suite - Correctness validation

**Key metrics**:
- Rows per second (read/write)
- Compression ratio
- Memory usage
- Latency distribution (P50, P99, P99.9)

### Platform-Specific Testing

**Required platforms**:
- ✅ Desktop (Zen3) - performance baseline
- 🔄 STM32F4 - embedded minimum target
- 🔄 STM32F7 - embedded typical target
- 🔄 Raspberry Pi - embedded maximum target
- ✅ Python - pandas integration
- ✅ Unity - C# integration

---

## Future Considerations

### Potential Features (Post v2.0)

1. **SIMD Optimization**
   - Vectorized compression/decompression
   - Parallel checksum computation
   - Requires 16-byte alignment

2. **Async I/O**
   - Background compression thread
   - Overlapped disk I/O
   - Double-buffering

3. **Network Streaming**
   - TCP/UDP transport
   - Packet-based protocol
   - Real-time telemetry

4. **Advanced Indexing**
   - Secondary indices (column values)
   - Time-range queries
   - Metadata queries

5. **Platform Integrations**
   - ROS/ROS2 topics
   - MQTT publish/subscribe
   - Kafka producer/consumer

---

## References

- [LZ4 Specification](https://github.com/lz4/lz4)
- [xxHash](https://github.com/Cyan4973/xxHash)
- [CHIMP Compression](https://github.com/panagiotisl/chimp)
- [GORILLA Compression](https://github.com/keisku/gorilla)
- [Protocol Buffers](https://protobuf.dev/)
- [Apache Parquet](https://parquet.apache.org/)

---

**Last Updated**: 2025-11-08  
**Version**: 1.2.0-dev  
**Status**: Active Development
