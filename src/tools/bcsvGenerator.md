# bcsvGenerator – Generate Synthetic BCSV Test Datasets

Generate BCSV files from 14 built-in dataset profiles with configurable row count, data mode, and encoding. Useful for testing, benchmarking, and validating tools in the BCSV ecosystem.

## Basic Usage

```bash
# Generate 10K rows of mixed_generic (default)
bcsvGenerator -o test.bcsv

# Generate 100K rows of sensor data
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv

# List available profiles
bcsvGenerator --list

# Help
bcsvGenerator --help
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output FILE` | Output BCSV file (required) | |
| `-p, --profile NAME` | Dataset profile | `mixed_generic` |
| `-n, --rows N` | Number of rows | `10000` |
| `-d, --data-mode MODE` | `timeseries` or `random` | `timeseries` |
| `--file-codec CODEC` | File codec (see below) | `packet_lz4_batch` |
| `--row-codec CODEC` | Row codec: `delta`, `zoh`, `flat` | `delta` |
| `--compression-level N` | LZ4 compression level | `1` |
| `--block-size N` | Block size in KB | `64` |
| `-f, --overwrite` | Overwrite output file if it exists | |
| `-v, --verbose` | Verbose progress output | |
| `--list` | List all available profiles and exit | |
| `-h, --help` | Show help message | |

### File Codecs

| Value | Description |
|-------|-------------|
| `packet_lz4_batch` | Async double-buffered LZ4 (default, best throughput) |
| `packet_lz4` | Packet LZ4 |
| `packet` | Packet, no compression |
| `stream_lz4` | Streaming LZ4 |
| `stream` | Streaming, no compression |

## Dataset Profiles

14 profiles covering diverse real-world schemas:

| Profile | Cols | Description |
|---------|------|-------------|
| `mixed_generic` | 72 | All 12 types (6 each) — baseline |
| `sparse_events` | 100 | ~1% activity — ZoH best-case |
| `sensor_noisy` | 50 | Float/double Gaussian noise |
| `string_heavy` | 30 | Varied string cardinality |
| `bool_heavy` | 132 | 128 bools + 4 scalars |
| `arithmetic_wide` | 200 | 200 numeric columns, no strings |
| `simulation_smooth` | 100 | Slow linear drift — ideal for ZoH |
| `weather_timeseries` | 40 | Realistic weather pattern |
| `high_cardinality_string` | 50 | Near-unique UUIDs |
| `event_log` | 27 | Backend event stream |
| `iot_fleet` | 25 | Fleet telemetry |
| `financial_orders` | 22 | Order/trade feed |
| `realistic_measurement` | 38 | DAQ session with phases |
| `rtl_waveform` | 290 | RTL digital waveform capture |

Use `bcsvGenerator --list` for full details.

## Data Modes

- **timeseries** (default): Values change at controlled intervals — produces compressible, ZoH-friendly patterns that resemble real time-series data
- **random**: Every cell changes every row — worst-case for compression, useful for stress-testing

```bash
# Time-series data (better compression)
bcsvGenerator -p simulation_smooth -d timeseries -o smooth.bcsv

# Random data (stress test)
bcsvGenerator -p simulation_smooth -d random -o stress.bcsv
```

## Encoding

Default output encoding is **packet_lz4_batch + delta**.

```bash
# No compression at all (flat, uncompressed packets)
bcsvGenerator --file-codec packet --row-codec flat -o flat.bcsv

# ZoH row codec with streaming LZ4
bcsvGenerator --file-codec stream_lz4 --row-codec zoh -o zoh_stream.bcsv

# Maximum LZ4 compression
bcsvGenerator --compression-level 12 -o compressed.bcsv
```

## Summary Output

After each run, a summary is printed to stderr:

```
=== bcsvGenerator Summary ===
Profile:    weather_timeseries
Data mode:  timeseries
  Columns: 40  [ 1×bool, 6×float, 4×double, 1×uint8, 1×uint64, ... ]
  timestamp:uint64, location_id:string, station_name:string, …, uv_index:float, is_daytime:bool

  Rows written: 10000
  Columns:      40
  Encoding:     delta + lz4 + batch (level 1)
  File size:    123456 bytes (120.56 KB)

  Wall time:    15 ms
  Throughput:   666.7 krows/s, 7.85 MB/s

  Output: weather.bcsv
```

## Typical Workflows

```bash
# Generate → inspect header
bcsvGenerator -p sensor_noisy -n 50000 -o sensor.bcsv
bcsvHeader sensor.bcsv

# Generate → peek at data
bcsvGenerator -p weather_timeseries -o weather.bcsv
bcsvHead weather.bcsv

# Generate → sample → convert
bcsvGenerator -p event_log -n 100000 -o events.bcsv
bcsvSampler -c 'X[0][1] > 50.0' events.bcsv filtered.bcsv
bcsv2csv filtered.bcsv filtered.csv
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (unknown profile, I/O error, invalid arguments) |
