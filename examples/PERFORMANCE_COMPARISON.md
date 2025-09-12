# BCSV Performance Comparison Summary

## Test Results Overview

All tests performed with **8 columns** of mixed data types using identical schemas for fair comparison.
Both tests optimized with `createRow()` outside the loop and run in Release mode for maximum performance.

### Latest Performance Results (100,000 rows, 8 columns, Release Mode)

#### Static Interface Performance
- **Write Speed**: 4,757,849 rows/second  
- **Read Speed**: 7,581,956 rows/second
- **Write Time**: 21.02 ms
- **Read Time**: 13.19 ms
- **Total Throughput**: 2,923,669 rows/second
- **File Size**: 3,662,428 bytes (36.62 bytes/row)

#### Flexible Interface Performance  
- **Write Speed**: 3,611,420 rows/second
- **Read Speed**: 2,350,176 rows/second  
- **Write Time**: 27.69 ms
- **Read Time**: 42.55 ms
- **Total Throughput**: 1,423,526 rows/second
- **File Size**: 3,658,176 bytes (36.58 bytes/row)

## Performance Comparison

### Write Performance
- **Static Interface**: 4,757,849 rows/sec
- **Flexible Interface**: 3,611,420 rows/sec
- **Static Advantage**: **1.32x faster** writes

### Read Performance
- **Static Interface**: 7,581,956 rows/sec
- **Flexible Interface**: 2,350,176 rows/sec
- **Static Advantage**: **3.23x faster** reads

### Overall Performance
- **Static Interface Total**: 34.20 ms (2,923,669 rows/sec)
- **Flexible Interface Total**: 70.25 ms (1,423,526 rows/sec)
- **Static Advantage**: **2.05x faster** overall

### File Size Efficiency
- **Static Interface**: 36.62 bytes/row
- **Flexible Interface**: 36.58 bytes/row
- **Difference**: Virtually identical (<0.1% difference)

## Key Findings

1. **Exceptional Performance**: Both interfaces achieve millions of rows per second throughput in Release mode

2. **File Format Compatibility**: Both interfaces produce nearly identical file sizes, confirming they use the same efficient binary format

3. **Write Performance**: Static interface provides moderate write performance advantage (32% faster)

4. **Read Performance**: Static interface provides significant read performance advantage (223% faster)

5. **Overall Efficiency**: Static interface is over 2x faster for complete read/write cycles

6. **Optimization Impact**: Release mode compilation provides dramatic performance improvements over Debug builds

## Historical Comparison (32 columns)

### Previous Static Interface Performance (32 columns)
- **Write Speed**: 85,034 rows/second
- **Read Speed**: 917,431 rows/second  
- **File Size**: 10.07 MB (105.52 bytes/row)

### Previous Flexible Interface Performance (32 columns)
- **Write Speed**: 49,554 rows/second
- **Read Speed**: 833,333 rows/second
- **File Size**: 9.96 MB (104.46 bytes/row)

### Performance Evolution
- **Current 8-column tests show dramatically higher throughput** due to:
  - Fewer columns per row (8 vs 32)
  - Release mode optimization
  - Improved compiler optimizations

## Compatibility Verification

✅ **Cross-Interface Compatibility Confirmed**
- Static → Static: ✅ Compatible
- Flexible → Flexible: ✅ Compatible  
- Static → Flexible: ✅ Compatible
- Flexible → Static: ✅ Compatible

All combinations produce identical file formats with 0 validation errors.

## Benchmark Environment

- **Compiler**: MSVC 2022 (Release mode with `/O2` optimization)
- **CPU**: Modern x64 processor
- **Test Dataset**: 100,000 rows × 8 columns (mixed data types)
- **Test Method**: Multiple iterations with consistent results
- **Memory**: Adequate RAM to avoid I/O bottlenecks
