# bcsvNarrowType implementation plan

**Session ID:** ses_0ecb92cc3ffeqZioppSPOq7a3T
**Created:** 6/29/2026, 2:07:16 PM
**Updated:** 6/29/2026, 4:19:53 PM

---

## User

Please help me to specify and plan the implementation, otimization and testing of a new bcsv cli tool called bcsvNarrowType. The tool should take and scan a bcsv file and identifiy if any of the columns could be converted to a smaller/narrower type to presereve the actuall content at a smaller filesize. In cases columns that could be converted to a smaller type are identified the file should be converted to the new layout (same number of columns, same column names, but optimized narrower types). Consider a file that heavily uses int64. But the column defined as int64 only contains positive values. In that case an unsigned type should be used. If the actual value (all values in this column) fits into uint8 the cokumn shout become an uint8_t. In case it requires 16 or 32bit to encode the value uint16 or uint32 should be used. Same applies to signed values, thus consider int8, int16, int32 or int64. If an integer simply jumpt between 0 and 1 consider to use a bool. Same for double value. If double values can be mapped onto float without loosing information (perhaps somebody accidentially stored floats into doubles) we should revert/convert to float. if float or double columns only contain integers, convert this column to integers. I expect that we first need to run a scan path over the entire file, checking for each column what the smallest type possible is for that column. Than we inform the user, on the finding and convert the file to the new layout. We should support writing it to another file (default), including overwrite function. An in-place option should also be provided (non default, warn user about replacing the file). the new tool should support bcsv's usual cli interface and documentation. !st do you understand the request? Ask if anything is unclear. Than familiarize with the project and develop a plan for the tool. Consider multiple approaches, discuss the pros and cons before making a recommendation. Consider code clarity, correctness, robustness and performance. Avoid unnecessary deep copies. consider using visitor pattern or other constructs that allow the compiler to optimize inner loops. The resulting file should use the same encoding schema as the input. Present your plan, before taking any action. 

---

## Assistant (Plan · Qwen3.6-27B Dense · 6.0s)

_Thinking:_

The user wants me to:
1. Understand the request for a new `bcsvNarrowType` CLI tool
2. Ask clarifying questions if needed
3. Familiarize myself with the project
4. Develop a plan considering multiple approaches
5. Present the plan before taking any action

This is plan mode - I should only read/inspect, not modify. Let me first understand the project structure, then ask questions, then present a plan.

Let me start by exploring the project.




Yes, I understand the request. Let me first familiarize myself with the project before asking questions and presenting a plan.




**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv</path>
<type>directory</type>
<entries>
.cache/
.clang-format
.clangd
.claude.md
.git/
.github/
.gitignore
.markdownlint.json
.opencode/
.pytest_cache/
.ruff_cache/
.venv/
AGENTS.md
ARCHITECTURE.md
bcsv.sln
benchmark/
build/
CHANGELOG.md
cmake/
CMakeLists.txt
CMakePresets.json
CODE_OF_CONDUCT.md
compile_commands.json
CONTRIBUTING.md
csharp/
docs/
examples/
include/
LICENSE
plan.md
python/
README.md
review.md
scripts/
SECURITY.md
SKILLS.md
src/
tests/
tmp/
ToDo.txt
unity/
VERSION.txt
VERSIONING.md

(43 entries)
</entries>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/.github/copilot-instructions.md"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/.github/copilot-instructions.md</path>
<type>file</type>
<content>
1: # BCSV — Copilot Instructions
2: 
3: Binary-CSV (BCSV): C++20 header-only library for fast, compact time-series storage.
4: Streaming row-by-row I/O, constant-time writes, crash resilient. Targets STM32 to desktop.
5: 
6: Minimum toolchains: GCC 13+, Clang 16+/libstdc++13, MSVC 2022 17.4+, Xcode 15.4+,
7: Arm GNU 13.2+ (STM32 Linux), Vitis/PetaLinux 2024.1+ (AMD/Xilinx Linux).
8: 
9: ## Build & Test Commands
10: 
11: ```bash
12: # Debug build + unit tests
13: cmake --preset ninja-debug && cmake --build --preset ninja-debug-build -j$(nproc)
14: ./build/ninja-debug/bin/bcsv_gtest
15: 
16: # Release build
17: cmake --preset ninja-release && cmake --build --preset ninja-release-build -j$(nproc)
18: 
19: # Run ALL tests (unit + integration via CTest)
20: ctest --test-dir build/ninja-debug --output-on-failure
21: 
22: # Filter specific test suite
23: ./build/ninja-debug/bin/bcsv_gtest --gtest_filter="ComprehensiveTest.*"
24: 
25: # C API tests
26: ./build/ninja-debug/bin/test_c_api && ./build/ninja-debug/bin/test_row_api
27: 
28: # Quick micro benchmark
29: python3 benchmark/run.py wip --type=MICRO --no-report
30: 
31: # Macro benchmark (15 profiles)
32: python3 benchmark/run.py wip --type=MACRO-SMALL
33: 
34: # Codec parity investigation (Flexible vs Static)
35: ./build/ninja-release/bin/bench_codec_compare \
36:     --rows=50000 --iterations=5 --profile=measurement_campaign \
37:     --storage=both --json=tmp/parity_investigation.json
38: 
39: # Build single CLI tool
40: cmake --build --preset ninja-release-build --target bcsvSampler -j$(nproc)
41: 
42: # Clean rebuild
43: rm -rf build/ninja-debug && cmake --preset ninja-debug && cmake --build --preset ninja-debug-build -j$(nproc)
44: 
45: # System-wide install (Ubuntu / CachyOS — requires sudo)
46: bash scripts/install.sh                  # installs to /usr/local
47: bash scripts/install.sh --prefix ~/local # user-local install
48: bash scripts/install.sh --uninstall      # remove installed files
49: ```
50: 
51: The install script builds the `ninja-release` preset, then runs `cmake --install` and `ldconfig`.
52: Installed locations: `$PREFIX/bin/` (9 CLI tools), `$PREFIX/lib/libbcsv_c_api.so`, `$PREFIX/include/bcsv/`, `$PREFIX/lib/cmake/bcsv/`.
53: 
54: ## Project Layout
55: 
56: ```
57: include/bcsv/         # Header-only C++ library (.h = declarations, .hpp = implementations)
58:   codec_row/           # Row codecs (flat001, zoh001, delta002)
59:   codec_file/          # File codecs (stream, packet, LZ4 variants)
60:   sampler/             # Bytecode VM for row filtering/projection
61: src/tools/             # 9 CLI tools (csv2bcsv, bcsv2csv, bcsvHead/Tail/Header, bcsvSampler, bcsvGenerator, bcsvValidate, bcsvRepair)
62: tests/                 # GTest suite (25 .cpp) + C API tests (3 .c) + shell integration tests
63: benchmark/             # Python orchestrator + Google Benchmark executables
64: python/                # nanobind bindings + pandas integration
65: unity/                 # C# / Unity bindings (P/Invoke)
66: examples/              # 11 C++ example programs
67: ```
68: 
69: ## Naming Conventions
70: 
71: | Element | Convention | Example |
72: |---------|-----------|---------|
73: | Private members | `snake_case_` (trailing `_`) | `file_path_`, `row_count_` |
74: | Public members | `snake_case` | `version_major`, `name` |
75: | Functions/methods | `lowerCamelCase` | `addColumn()`, `readNext()` |
76: | Classes/structs | `UpperCamelCase` | `Layout`, `FileHeader` |
77: | Constants/enums | `CAPITAL_SNAKE_CASE` | `MAX_COLUMNS`, `ZERO_ORDER_HOLD` |
78: | C API | `bcsv_snake_case` | `bcsv_reader_open()` |
79: 
80: ## File Organization
81: 
82: - `.h` = concise declarations, human-readable
83: - `.hpp` = implementations, detailed logic
84: - Header guard: `#pragma once`
85: - Include order: standard library (`<>`) first, then project headers (`""`)
86: 
87: ## Error Handling
88: 
89: - I/O operations (`open`, `readNext`, `writeRow`): return `bool`, use `getErrorMsg()` for details
90: - Logic errors (out-of-range, type mismatch): throw exceptions
91: - Debug output: controlled by `DEBUG_OUTPUTS` constexpr in `definitions.h`
92: 
93: ## Key Design Patterns
94: 
95: - **Streaming I/O**: never load entire file — row-by-row read/write
96: - **Observer pattern**: `Layout` notifies attached `Row` objects of schema changes
97: - **Codec-based design**: row encoding (Flat/ZoH/Delta) and file codec (stream/packet) are orthogonal template axes
98: - **Three-container Row**: `bits_` (Bitset), `data_` (byte vector), `strg_` (string vector)
99: - **Codec separation**: wire-format lives in codec classes, not in Row
100: - **LayoutGuard**: RAII lock prevents layout mutation while a codec is active
101: 
102: ## Temporary Files Policy
103: 
104: Use `tmp/` under project root for scratch work — it's gitignored.
105: 
106: ## Release Workflow
107: 
108: When tagging a release version, update `CHANGELOG.md` with the changes since the last tag.
109: Follow [Keep a Changelog](https://keepachangelog.com/) format (Added/Changed/Fixed/Removed sections).
110: 
111: ## Architectural Decision Records (ADRs)
112: 
113: Design decisions that were explicitly evaluated and accepted (or deferred) are
114: recorded in `docs/adr/`. Before proposing changes to error handling, wire format,
115: buffer strategies, or type widths, **read the relevant ADR first** to avoid
116: re-litigating settled decisions.
117: 
118: ## Deep Reference
119: 
120: - `SKILLS.md` — full AI skills reference (API classes, source inventory, all conventions)
121: - `ARCHITECTURE.md` — design philosophy, binary format spec, roadmap
122: - `docs/adr/README.md` — architectural decision records (ADRs)
123: - `src/tools/CLI_TOOLS.md` — all 9 CLI tools with options and examples
124: - `tests/README.md` — test infrastructure details
125: - `benchmark/README.md` — benchmark orchestrator and profiles
126: - `ToDo.txt` — active task list with priorities

(End of file - total 126 lines)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 4.1s)

_Thinking:_

Good basic structure. Now let me dig deeply into the project to understand:
1. Existing CLI tools - their structure and patterns
2. The type system in bcsv
3. The reader/writer APIs
4. The layout/schema representation
5. How rows are read and written
6. The encoding/decoding mechanisms

Let me read the key files.






**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/src/tools"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/src/tools</path>
<type>directory</type>
<entries>
bcsv2csv.cpp
bcsvCompare.cpp
bcsvGenerator.cpp
bcsvHead.cpp
bcsvHeader.cpp
bcsvRepair.cpp
bcsvSampler.cpp
bcsvTail.cpp
bcsvValidate.cpp
cli_common.h
CLI_TOOLS.md
CMakeLists.txt
csv2bcsv.cpp

(13 entries)
</entries>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv</path>
<type>directory</type>
<entries>
bcsv_c_api.h
bcsv.h
bcsv.hpp
bitset.h
bitset.hpp
byte_buffer.h
checksum.hpp
codec_file/
codec_row/
column_name_index.h
column_name_index.hpp
csv_reader.h
csv_reader.hpp
csv_writer.h
csv_writer.hpp
definitions.h
file_footer.h
file_header.h
file_header.hpp
layout_guard.h
layout.h
layout.hpp
lz4_block.hpp
lz4_stream.hpp
packet_header.h
reader_concept.h
reader.h
reader.hpp
row_visitors.h
row.h
row.hpp
sampler/
std_charconv_compat.h
vle.hpp
writer_concept.h
writer.h
writer.hpp

(37 entries)
</entries>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/src/tools/CLI_TOOLS.md"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/src/tools/CLI_TOOLS.md</path>
<type>file</type>
<content>
1: # BCSV CLI Tools
2: 
3: Command-line utilities for CSV ↔ BCSV conversion, data inspection, filtering, validation, comparison, and synthetic dataset generation.
4: All tools are built from `src/tools/` and output to `build/bin/`.
5: 
6: ## Overview
7: 
8: | Tool | Purpose | Input | Output |
9: |------|---------|-------|--------|
10: | **csv2bcsv** | Convert CSV to BCSV | CSV | BCSV |
11: | **bcsv2csv** | Convert BCSV to CSV | BCSV | CSV |
12: | **bcsvHeader** | Display file schema | BCSV | Text |
13: | **bcsvHead** | Display first N rows | BCSV | CSV |
14: | **bcsvTail** | Display last N rows | BCSV | CSV |
15: | **bcsvSampler** | Filter & project rows | BCSV | BCSV |
16: | **bcsvGenerator** | Generate synthetic datasets | — | BCSV |
17: | **bcsvCompare** | Deterministic file comparison | BCSV × 2 | Exit code + report |
18: | **bcsvValidate** | Validate structure & content | BCSV (+CSV) | Report |
19: | **bcsvRepair** | Repair damaged/interrupted files | BCSV | BCSV |
20: 
21: ## Quick Start
22: 
23: ```bash
24: csv2bcsv data.csv data.bcsv            # Convert CSV → BCSV
25: bcsvHeader data.bcsv                    # Inspect schema
26: bcsvHead data.bcsv -n 5                 # Preview first rows
27: bcsvTail data.bcsv -n 5                 # Preview last rows
28: bcsv2csv data.bcsv output.csv           # Convert back to CSV
29: bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv  # Filter rows
30: bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv   # Generate test data
31: bcsvValidate -i data.bcsv                              # Validate structure
32: bcsvValidate -i data.bcsv --compare source.csv         # Compare files
33: bcsvCompare a.bcsv b.bcsv                              # Deterministic comparison
34: bcsvCompare --mode compatible a.bcsv b.bcsv             # Ignore column names
35: bcsvCompare --mode value --tolerance 1e-6 a.bcsv b.bcsv  # Cross-type, float tolerance
36: bcsvRepair -i broken.bcsv --dry-run                    # Analyze damage
37: bcsvRepair -i broken.bcsv -o repaired.bcsv             # Repair to new file
38: ```
39: 
40: ## Pipeline Examples
41: 
42: ```bash
43: # Generate → inspect → filter → export
44: bcsvGenerator -p weather_timeseries -n 50000 -o weather.bcsv
45: bcsvHeader weather.bcsv
46: bcsvSampler -c 'X[0][3] > 25.0' -s 'X[0][0], X[0][3]' weather.bcsv hot_days.bcsv
47: bcsv2csv hot_days.bcsv hot_days.csv
48: 
49: # Slice and export
50: bcsv2csv --slice 100:200 data.bcsv rows_100_to_199.csv
51: 
52: # Batch convert all CSV files
53: for file in *.csv; do csv2bcsv "$file" "${file%.csv}.bcsv"; done
54: ```
55: 
56: ## Build
57: 
58: ```bash
59: # Build all tools (BUILD_TOOLS=ON by default)
60: cmake --preset ninja-release && cmake --build --preset ninja-release-build -j$(nproc)
61: 
62: # Build a single tool
63: cmake --build --preset ninja-release-build --target bcsvSampler -j$(nproc)
64: ```
65: 
66: ## Source Structure
67: 
68: | File | Description |
69: |------|-------------|
70: | `csv2bcsv.cpp` | CSV → BCSV converter |
71: | `bcsv2csv.cpp` | BCSV → CSV converter |
72: | `bcsvHead.cpp` | First-N-rows display |
73: | `bcsvTail.cpp` | Last-N-rows display |
74: | `bcsvHeader.cpp` | Schema display |
75: | `bcsvSampler.cpp` | Expression-based filter & project |
76: | `bcsvGenerator.cpp` | Synthetic dataset generator |
77: | `bcsvValidate.cpp` | Structure & content validation |
78: | `bcsvRepair.cpp` | Repair damaged/interrupted BCSV files |
79: | `bcsvCompare.cpp` | Deterministic file comparison (strict/compatible/value modes) |
80: | `cli_common.h` | Shared CLI utilities (codec dispatch, validation, formatting) |
81: | `CMakeLists.txt` | Build definitions for all 10 tools |
82: 
83: ---
84: 
85: ## csv2bcsv — CSV to BCSV Converter
86: 
87: Convert CSV files to BCSV format with automatic delimiter detection, aggressive type optimization, and compression.
88: 
89: ### Usage
90: 
91: ```bash
92: csv2bcsv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
93: csv2bcsv data.csv                                    # Auto-detect, default output
94: csv2bcsv data.csv output.bcsv                         # Custom output
95: csv2bcsv -d ';' --decimal-separator ',' german.csv    # European format
96: csv2bcsv --row-codec delta data.csv output.bcsv       # Explicit delta codec
97: csv2bcsv --benchmark data.csv output.bcsv             # Measure throughput
98: ```
99: 
100: ### Options
101: 
102: | Option | Description | Default |
103: |--------|-------------|---------|
104: | `-d, --delimiter CHAR` | Field delimiter (auto-detected if omitted) | auto |
105: | `--decimal-separator CHAR` | Decimal separator: `.` or `,` | `.` |
106: | `--no-header` | CSV file has no header row | |
107: | `-v, --verbose` | Verbose output | |
108: | `--benchmark` | Print timing stats to stderr | |
109: | `--json` | With `--benchmark`: emit JSON timing blob to stdout | |
110: | `-h, --help` | Show help message | |
111: 
112: ### Type Optimization
113: 
114: All columns are scanned in a first pass to select the most compact type:
115: 
116: | Input Data | Detected Type | Savings vs INT64 |
117: |------------|---------------|-------------------|
118: | `0, 1, 255` | `UINT8` | 87.5% |
119: | `-100, 0, 100` | `INT8` | 87.5% |
120: | `1000, 50000` | `UINT16` | 75% |
121: | `3.14, 2.71` | `FLOAT` | 50% |
122: | `3.141592653589793` | `DOUBLE` | — |
123: 
124: ### Delimiter Auto-Detection
125: 
126: When `-d` is omitted, scans the first line. Priority: comma → semicolon → tab → pipe.
127: 
128: ---
129: 
130: ## bcsv2csv — BCSV to CSV Converter
131: 
132: Convert BCSV files to CSV format with RFC 4180 compliance. Supports row slicing, custom delimiters, and all BCSV codecs.
133: 
134: ### Usage
135: 
136: ```bash
137: bcsv2csv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
138: bcsv2csv data.bcsv output.csv                  # Basic conversion
139: bcsv2csv -d ';' data.bcsv euro.csv             # European format
140: bcsv2csv --slice 10:20 data.bcsv               # Rows 10–19
141: bcsv2csv -d $'\t' --no-header data.bcsv        # Tab-separated, no header
142: ```
143: 
144: ### Options
145: 
146: | Option | Description | Default |
147: |--------|-------------|---------|
148: | `-d, --delimiter CHAR` | Output field delimiter | `,` |
149: | `--no-header` | Omit header row in output | |
150: | `--firstRow N` | Start from row N (0-based) | `0` |
151: | `--lastRow N` | End at row N (0-based, inclusive) | last |
152: | `--slice SLICE` | Python-style slice (overrides firstRow/lastRow) | |
153: | `-v, --verbose` | Verbose output | |
154: | `--benchmark` | Print timing stats to stderr | |
155: | `-h, --help` | Show help message | |
156: 
157: ### Row Slicing
158: 
159: ```bash
160: bcsv2csv --slice 10:20 data.bcsv     # Rows 10–19
161: bcsv2csv --slice :100 data.bcsv      # First 100 rows
162: bcsv2csv --slice 50: data.bcsv       # From row 50 to end
163: bcsv2csv --slice ::2 data.bcsv       # Every 2nd row
164: bcsv2csv --slice -10: data.bcsv      # Last 10 rows
165: ```
166: 
167: ---
168: 
169: ## bcsvHeader — Display File Schema
170: 
171: Display column index, name, and type. Header-only read — works instantly on any file size.
172: 
173: ### Usage
174: 
175: ```bash
176: bcsvHeader data.bcsv          # Display schema
177: bcsvHeader -v data.bcsv       # Verbose (includes codec info)
178: ```
179: 
180: ### Options
181: 
182: | Option | Description |
183: |--------|-------------|
184: | `-v, --verbose` | Additional details (codec names, file version) |
185: | `-h, --help` | Show help message |
186: 
187: ### Output
188: 
189: ```text
190: BCSV Header Structure: dataset.bcsv
191: Columns: 4
192: 
193: Index  Name      Type
194: -----  --------  ------
195: 0      id        int32
196: 1      name      string
197: 2      score     float
198: 3      active    bool
199: ```
200: 
201: ---
202: 
203: ## bcsvHead — Display First N Rows
204: 
205: Display the first rows of a BCSV file in CSV format. Memory efficient — only loads requested rows.
206: 
207: ### Usage
208: 
209: ```bash
210: bcsvHead data.bcsv               # First 10 rows (default)
211: bcsvHead -n 5 data.bcsv          # First 5 rows
212: bcsvHead --no-header data.bcsv   # Data only (no column header)
213: bcsvHead data.bcsv | grep "Alice"          # Pipe to grep
214: bcsvHead -n 100 data.bcsv | awk -F, '{sum+=$3} END {print sum}'  # Pipe to awk
215: ```
216: 
217: ### Options
218: 
219: | Option | Description | Default |
220: |--------|-------------|---------|
221: | `-n, --lines N` | Number of rows to display | `10` |
222: | `-d, --delimiter CHAR` | Field delimiter | `,` |
223: | `--no-header` | Omit header row | |
224: | `-v, --verbose` | Verbose output | |
225: | `-h, --help` | Show help message | |
226: 
227: ---
228: 
229: ## bcsvTail — Display Last N Rows
230: 
231: Display the last rows of a BCSV file in CSV format. Uses a circular buffer — constant memory regardless of file size.
232: 
233: ### Usage
234: 
235: ```bash
236: bcsvTail data.bcsv               # Last 10 rows (default)
237: bcsvTail -n 5 data.bcsv          # Last 5 rows
238: bcsvTail --no-header data.bcsv   # Data only
239: bcsvTail -n 500 --no-header data.bcsv | awk -F, '$4 > 1000 {print $2, $4}'
240: ```
241: 
242: ### Options
243: 
244: | Option | Description | Default |
245: |--------|-------------|---------|
246: | `-n, --lines N` | Number of rows to display | `10` |
247: | `-d, --delimiter CHAR` | Field delimiter | `,` |
248: | `--no-header` | Omit header row | |
249: | `-v, --verbose` | Verbose output | |
250: | `-h, --help` | Show help message | |
251: 
252: ---
253: 
254: ## bcsvSampler — Filter & Project BCSV Files
255: 
256: Apply conditional filters and column projections using the Sampler bytecode VM. Writes matching rows to a new BCSV file.
257: 
258: ### Usage
259: 
260: ```bash
261: bcsvSampler -c 'X[0][0] > 100' data.bcsv                           # Filter rows
262: bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv projected.bcsv         # Project columns
263: bcsvSampler -c 'X[0][1] > 0' -s 'X[0][0], X[0][1]' data.bcsv out.bcsv  # Both
264: ```
265: 
266: ### Options
267: 
268: | Option | Description | Default |
269: |--------|-------------|---------|
270: | `-c, --conditional EXPR` | Row filter (boolean expression) | none (pass all) |
271: | `-s, --selection EXPR` | Column projection (comma-separated) | none (all) |
272: | `-m, --mode MODE` | Boundary mode: `truncate` or `expand` | `truncate` |
273: | `--compression-level N` | LZ4 compression level | `1` |
274: | `--block-size N` | Block size in KB | `64` |
275: | `-f, --overwrite` | Overwrite output file | |
276: | `--disassemble` | Print compiled bytecode and exit | |
277: | `-v, --verbose` | Verbose progress output | |
278: | `-h, --help` | Show help message | |
279: 
280: ### Expression Syntax
281: 
282: Cells are referenced as `X[row_offset][column_index]`:
283: 
284: - `X[0][0]` — current row, column 0
285: - `X[-1][0]` — previous row, column 0
286: - `X[1][0]` — next row, column 0
287: 
288: **Conditional examples:**
289: 
290: ```bash
291: bcsvSampler -c 'X[0][0] > 100' data.bcsv                    # Threshold
292: bcsvSampler -c 'X[0][1] != X[-1][1]' data.bcsv              # Edge detection
293: bcsvSampler -c 'X[0][0] > 10 && X[0][1] < 50' data.bcsv     # Boolean logic
294: ```
295: 
296: **Selection examples:**
297: 
298: ```bash
299: bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv                 # Pick columns
300: bcsvSampler -s 'X[0][0], X[0][1] - X[-1][1]' data.bcsv      # Gradient
301: bcsvSampler -s 'X[0][0], (X[-1][1] + X[0][1] + X[1][1]) / 3.0' data.bcsv  # Moving avg
302: ```
303: 
304: **Boundary mode:** `truncate` (default) skips rows with incomplete windows; `expand` clamps to edge row.
305: 
306: ---
307: 
308: ## bcsvGenerator — Generate Synthetic Datasets
309: 
310: Generate BCSV files from 14 built-in dataset profiles with configurable row count, data mode, and encoding.
311: 
312: ### Usage
313: 
314: ```bash
315: bcsvGenerator -o test.bcsv                                  # Default: 10K mixed_generic
316: bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv      # 100K sensor data
317: bcsvGenerator --list                                        # List profiles
318: bcsvGenerator -p weather_timeseries -d random -o stress.bcsv  # Random (stress test)
319: ```
320: 
321: ### Options
322: 
323: | Option | Description | Default |
324: |--------|-------------|---------|
325: | `-o, --output FILE` | Output BCSV file (required) | |
326: | `-p, --profile NAME` | Dataset profile | `mixed_generic` |
327: | `-n, --rows N` | Number of rows | `10000` |
328: | `-d, --data-mode MODE` | `timeseries` or `random` | `timeseries` |
329: | `--file-codec CODEC` | File codec (see below) | `packet_lz4_batch` |
330: | `--row-codec CODEC` | Row codec: `delta`, `zoh`, `flat` | `delta` |
331: | `--compression-level N` | LZ4 compression level | `1` |
332: | `--block-size N` | Block size in KB | `64` |
333: | `-f, --overwrite` | Overwrite output file | |
334: | `-v, --verbose` | Verbose output | |
335: | `--list` | List all profiles and exit | |
336: | `-h, --help` | Show help message | |
337: 
338: ### File Codecs
339: 
340: | Value | Description |
341: |-------|-------------|
342: | `packet_lz4_batch` | Async double-buffered LZ4 (default, best throughput) |
343: | `packet_lz4` | Packet LZ4 |
344: | `packet` | Packet, no compression |
345: | `stream_lz4` | Streaming LZ4 |
346: | `stream` | Streaming, no compression |
347: 
348: ### Dataset Profiles (14)
349: 
350: | Profile | Cols | Description |
351: |---------|------|-------------|
352: | `mixed_generic` | 72 | All 12 types (6 each) — baseline |
353: | `sparse_events` | 100 | ~1% activity — ZoH best-case |
354: | `sensor_noisy` | 50 | Float/double Gaussian noise |
355: | `string_heavy` | 30 | Varied string cardinality |
356: | `bool_heavy` | 132 | 128 bools + 4 scalars |
357: | `arithmetic_wide` | 200 | 200 numeric columns, no strings |
358: | `simulation_smooth` | 100 | Slow linear drift — ideal for ZoH |
359: | `weather_timeseries` | 40 | Realistic weather pattern |
360: | `high_cardinality_string` | 50 | Near-unique UUIDs |
361: | `event_log` | 27 | Backend event stream |
362: | `iot_fleet` | 25 | Fleet telemetry |
363: | `financial_orders` | 22 | Order/trade feed |
364: | `realistic_measurement` | 38 | DAQ session with phases |
365: | `rtl_waveform` | 290 | RTL digital waveform capture |
366: 
367: ---
368: 
369: ## bcsvValidate — Validate BCSV Structure & Content
370: 
371: Validate BCSV files in three modes: structure inspection, pattern validation against benchmark profiles, and file-to-file comparison.
372: 
373: ### Usage
374: 
375: ```bash
376: bcsvValidate -i data.bcsv                                   # Structure check
377: bcsvValidate -i data.bcsv --deep                             # Parse every row
378: bcsvValidate -i data.bcsv -p sensor_noisy -n 10000           # Pattern validation
379: bcsvValidate -i a.bcsv --compare b.bcsv                      # File comparison
380: bcsvValidate -i a.bcsv --compare b.csv --tolerance 1e-6      # With float tolerance
381: bcsvValidate -i data.bcsv -p mixed_generic --json            # JSON output for CI
382: bcsvValidate --list                                          # List profiles
383: ```
384: 
385: ### Options
386: 
387: | Option | Description | Default |
388: |--------|-------------|---------|
389: | `-i, --input FILE` | Input BCSV file (required) | |
390: | `-p, --profile NAME` | Benchmark profile name | |
391: | `-n, --rows N` | Expected row count | profile default |
392: | `-d, --data-mode MODE` | `timeseries` or `random` | `timeseries` |
393: | `--compare FILE` | Second file for comparison (BCSV or CSV) | |
394: | `--tolerance TOL` | Float/double comparison tolerance | `0.0` |
395: | `--max-errors N` | Maximum mismatches to report | `10` |
396: | `--deep` | Parse every row during structure check | |
397: | `--json` | Machine-readable JSON output | |
398: | `--list` | List available profiles and exit | |
399: | `-v, --verbose` | Verbose output | |
400: | `-h, --help` | Show help message | |
401: 
402: ### Exit Codes
403: 
404: | Code | Meaning |
405: |------|---------|
406: | 0 | Validation passed |
407: | 1 | Validation failed (mismatches found) |
408: | 2 | Error (file not found, bad arguments) |
409: 
410: ---
411: 
412: ## bcsvRepair — Repair Damaged BCSV Files
413: 
414: Recover data from BCSV files where the writer was interrupted before completing the write (crash, power loss, killed process). Supports all five file codecs and all three row codecs.
415: 
416: **Packet-mode** files (packet, packet_lz4, packet_lz4_batch): walks packet-by-packet, rebuilds the packet index, attempts partial recovery of the last incomplete packet, writes a valid footer.
417: 
418: **Stream-mode** files (stream, stream_lz4): walks row-by-row using per-row XXH32 checksums, truncates at the last valid row.
419: 
420: ### Usage
421: 
422: ```bash
423: bcsvRepair -i broken.bcsv --dry-run                      # Analyze — report damage without modifying
424: bcsvRepair -i broken.bcsv --dry-run --json               # Machine-readable damage report
425: bcsvRepair -i broken.bcsv -o repaired.bcsv               # Copy repaired data to new file
426: bcsvRepair -i broken.bcsv -o repaired.bcsv --deep -v     # Deep checksum validation + progress
427: bcsvRepair -i broken.bcsv --in-place --backup            # Truncate + rewrite footer in place
428: ```
429: 
430: ### Options
431: 
432: | Option | Description | Default |
433: |--------|-------------|---------|
434: | `-i, --input FILE` | Input BCSV file (required) | |
435: | `-o, --output FILE` | Write repaired file to new location (copy mode) | |
436: | `--in-place` | Modify input file directly (truncate + append footer) | |
437: | `--backup` | With `--in-place`: copy original to `FILE.bak` first | |
438: | `--deep` | Validate packet payload checksums (slower, more thorough) | |
439: | `--dry-run` | Analyze only — no files modified (auto-enables `--deep`) | |
440: | `--json` | Machine-readable JSON output to stdout | |
441: | `-v, --verbose` | Print progress (packets scanned, rows counted) | |
442: | `-h, --help` | Show help message | |
443: 
444: ### Exit Codes
445: 
446: | Code | Meaning |
447: |------|---------|
448: | 0 | Repair successful (or file already valid) |
449: | 1 | Repair failed or file not repairable |
450: | 2 | Argument error |
451: 
452: ### Notes
453: 
454: - The `packet_lz4_batch` codec wraps entire packets in a single LZ4 frame; if the frame is truncated, zero rows can be recovered from that packet. Non-batch codecs recover individual rows.
455: - Use `--dry-run --json` in CI pipelines to detect interrupted writes.
456: - `recovery_pct` in JSON output shows the percentage of rows successfully recovered.
457: 
458: ---
459: 
460: ## bcsvCompare — Deterministic File Comparison
461: 
462: Compare two BCSV files and return exit code 0 (identical) or 1 (different) with three comparison strictness modes.  Designed for CI guards, snapshot comparisons, and quick verification that two binary files contain the same data.
463: 
464: ### Comparison Modes
465: 
466: | Mode | Column names | Column types | Cell values |
467: |------|-------------|-------------|-------------|
468: | **strict** (default) | must match | must match | same-type, exact (+ tolerance) |
469: | **compatible** | ignored | must match | same-type, exact (+ tolerance) |
470: | **value** | ignored | ignored | coerced to double (or string) |
471: 
472: In **value** mode, numeric types (INT*, UINT*, FLOAT, DOUBLE, BOOL) are coerced to double and compared with tolerance.  STRING columns are compared as strings only — STRING vs numeric is always a mismatch.  Columns are compared positionally (index N vs index N), not by name.
473: 
474: NaN values in FLOAT/DOUBLE columns are handled consistently: identical files with matching NaN positions compare as identical.  Tolerance is always absolute (not relative) and applies only to FLOAT/DOUBLE columns.
475: 
476: Out-of-bounds `--rows` or `--cols` ranges exit with code 2.
477: 
478: ### Usage
479: 
480: ```bash
481: bcsvCompare a.bcsv b.bcsv                              # strict, all data
482: bcsvCompare --mode compatible a.bcsv b.bcsv            # ignore column names
483: bcsvCompare --mode value --tolerance 1e-6 a.bcsv b.bcsv # value coercion
484: bcsvCompare --rows 0:99 --cols 2:5 a.bcsv b.bcsv      # compare subset
485: bcsvCompare -v a.bcsv b.bcsv                            # verbose mismatch report
486: bcsvCompare --mode value --tolerance 1e-6 a.bcsv b.bcsv  # cross-type value comparison
487: ```
488: 
489: ### Options
490: 
491: | Option | Description | Default |
492: |--------|-------------|---------|
493: | `--mode MODE` | `strict`, `compatible`, or `value` | `strict` |
494: | `--tolerance TOL` | Float/double comparison epsilon | `0.0` |
495: | `--rows RANGES` | Row indices to compare (inclusive, e.g. `0:99,-10:`) | all rows |
496: | `--cols RANGES` | Column indices to compare (inclusive, e.g. `2:5`) | all columns |
497: | `-v, --verbose` | Report mismatch details to stdout | summary only |
498: | `-h, --help` | Show help message | — |
499: 
500: ### Range Syntax
501: 
502: Ranges use inclusive indexing and support comma-separated, disjoint ranges:
503: 
504: | Example | Meaning |
505: |---------|---------|
506: | `5` | Single index 5 |
507: | `0:99` | Indices 0 through 99 |
508: | `:100` | First 101 indices (0:100) |
509: | `-10:` | Last 10 indices |
510: | `0:9,50:59` | Two disjoint ranges |
511: 
512: ### Exit Codes
513: 
514: 0 (identical), 1 (different), 2 (argument / file error).
515: 
516: ### Examples
517: 
518: ```bash
519: # CI guard: fail the pipeline if outputs differ
520: bcsvCompare --mode compatible release.bcsv staging.bcsv
521: 
522: # Compare only the first 100 rows, columns 0-4
523: bcsvCompare --rows :99 --cols 0:4 a.bcsv b.bcsv
524: 
525: # Cross-type value comparison
526: bcsvCompare --mode value --tolerance 0.001 a.bcsv b.bcsv
527: 
528: # Verbose report of which columns are different
529: bcsvCompare --mode strict -v a.bcsv b.bcsv
530: ```
531: 
532: ### Differences from bcsvValidate --compare
533: 
534: `bcsvValidate --compare` is a sub-mode of the validation tool, focused on structural validation and pattern matching.  `bcsvCompare` is a dedicated, faster diff tool with:
535: 
536: - Three comparison modes (strict, compatible, value) for semantic flexibility
537: - Row/column range scoping for targeted comparisons
538: - Cross-type value coercion (value mode allows int(1) to match double(1.0))
539: - Cleaner exit-code semantics (0 = same, 1 = different)
540: - No CSV input support (both files must be BCSV)
541: - NaN values compare equal (NaN == NaN)
542: - Out-of-range `--rows`/`--cols` exits with code 2

(End of file - total 542 lines)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 6.0s)

_Thinking:_

Now let me read the key source files to understand the type system, reader/writer, and CLI patterns.







**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/definitions.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/definitions.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: /* This file holds all constants and definitions used throughout the BCSV library */
13: #include <cstdint>
14: #include <string>
15: #include <cstddef>
16: #include <array>
17: #include <bit>
18: #include <variant>
19: #include <limits>
20: #include <cstring>
21: #include <stdexcept>
22: #include <type_traits>
23: 
24: // Include auto-generated version information.
25: // Use angle-bracket form so the compiler searches -I paths (including
26: // CMAKE_BINARY_DIR/include) with the bcsv/ prefix, which is required
27: // when building from a clean clone where the header lives only in the
28: // build tree at <build>/include/bcsv/version_generated.h.
29: #include <bcsv/version_generated.h>
30: 
31: // Force-inline macro for hot-path accessors (get/set/ref/visit).
32: // Without this, GCC's inlining heuristic may arbitrarily chose which
33: // template instantiations to inline, causing inconsistent benchmark
34: // results and sub-optimal performance in real workloads.
35: #if defined(_MSC_VER)
36:     #define BCSV_ALWAYS_INLINE __forceinline
37: #elif defined(__GNUC__) || defined(__clang__)
38:     #define BCSV_ALWAYS_INLINE __attribute__((always_inline)) inline
39: #else
40:     #define BCSV_ALWAYS_INLINE inline
41: #endif
42: 
43: namespace bcsv {
44:     
45:     // Configuration
46:     constexpr bool RANGE_CHECKING = true;
47:     constexpr bool STRING_OVERFLOW_THROWS = true;  // true = throw std::length_error on string > MAX_STRING_LENGTH, false = silently truncate (legacy behavior)
48: #ifndef NDEBUG
49:     constexpr bool DEBUG_OUTPUTS  = true;       // sends information to std::cerr and std::cout to support development and debugging
50: #else
51:     constexpr bool DEBUG_OUTPUTS  = false;      // disabled in release builds
52: #endif             
53: 
54:     // Version information (from auto-generated header)
55:     constexpr int VERSION_MAJOR = version::MAJOR;
56:     constexpr int VERSION_MINOR = version::MINOR;
57:     constexpr int VERSION_PATCH = version::PATCH;
58:     
59:     inline std::string getVersion() {
60:         return version::STRING;
61:     }
62: 
63:     // Since v1.5.0, library version and wire-format version are unified.
64:     // The library version (from git tags) IS the file format version.
65:     // See VERSIONING.md for compatibility rules (A/B/C).
66: 
67:     // Constants for the binary file format
68:     // uint32 constants store magic as little-endian integers; char arrays hold the on-disk byte order.
69:     constexpr uint32_t BCSV_MAGIC = 0x56534342;         // LE bytes: "BCSV"
70:     // NOTE: PCKT_MAGIC on-disk bytes read "PKCT" (K/C transposed vs the intended "PCKT").
71:     // Kept as-is to avoid invalidating all existing files; correct in the next breaking change.
72:     constexpr uint32_t PCKT_MAGIC = 0x54434B50;         // LE bytes: "PKCT"
73:     constexpr uint32_t FOOTER_BIDX_MAGIC = 0x58444942;  // LE bytes: "BIDX"
74:     constexpr uint32_t FOOTER_EIDX_MAGIC = 0x58444945;  // LE bytes: "EIDX"
75:     constexpr char MAGIC_BYTES_BCSV[4] = { 'B', 'C', 'S', 'V' };
76:     constexpr char MAGIC_BYTES_PCKT[4] = { 'P', 'K', 'C', 'T' };  // matches LE of PCKT_MAGIC
77:     constexpr char MAGIC_BYTES_FOOTER_BIDX[4] = { 'B', 'I', 'D', 'X' };
78:     constexpr char MAGIC_BYTES_FOOTER_EIDX[4] = { 'E', 'I', 'D', 'X' };
79:     constexpr uint32_t PCKT_TERMINATOR = 0x3FFFFFFF ;    // Marker value to indicate end of packet data (no more rows to come)
80:     constexpr size_t MAX_COLUMN_COUNT  = std::numeric_limits<uint16_t>::max();  // Maximum number of columns
81:     constexpr size_t MAX_COLUMN_LENGTH = std::numeric_limits<uint16_t>::max();  // Maximum width of column content
82:     constexpr size_t MAX_STRING_LENGTH = std::numeric_limits<uint16_t>::max(); // Maximum length of string data (wire format uses uint16_t lengths)
83:     constexpr size_t MAX_ROW_LENGTH    = (1ULL << 24) - 2 ;                     // ~16 MB maximum row size, 4b BLE encoding (2 bits for length), reserve 0xFFFF for terminator.
84:     constexpr size_t MIN_PACKET_SIZE         = 64 * 1024;                       // 64KB minimum packet size
85:     constexpr size_t DEFAULT_PACKET_SIZE_KB  = 8192;                            // 8MB default packet size (in KB, for Writer::open blockSizeKB param)
86:     constexpr size_t MAX_PACKET_SIZE         = 1024 * 1024 * 1024;              // 1GB maximum packet size
87:     /**
88:      * @brief Feature flag bit positions (Reserved for future optional features)
89:      * 
90:      * Note: Core features (CHECKSUMS, ROW_INDEX, ALIGNED, COMPRESSED) are now
91:      * mandatory in v1.0+ and no longer require flag bits.
92:      */
93:     enum class FileFlags : uint16_t {
94:         // All bits currently reserved for future optional features
95:         NONE                = 0x0000,                  ///< No special features enabled
96:         ZERO_ORDER_HOLD     = 0x0001,                  ///< Bit 0: Indicates this file uses zero-order hold compression (v1.2.0, will be always-on in v1.3.0)
97:         NO_FILE_INDEX       = 0x0002,                  ///< Bit 1: File has no index (sequential scan only, minimal footer) - for embedded platforms
98:         STREAM_MODE         = 0x0004,                  ///< Bit 2: File uses stream mode (no packets/checksums/footer). Default (0) = packet mode.
99:         BATCH_COMPRESS      = 0x0008,                  ///< Bit 3: Packet payload is batch-compressed as a single LZ4 block (async double-buffered I/O).
100:         DELTA_ENCODING      = 0x0010,                  ///< Bit 4: Delta + VLE row encoding (type-grouped, combined header codes, ZoH/FoC/VLE for numerics).
101:         // Bits 5-15 reserved for future use
102:     };
103: 
104:     // Enable bitwise operations for FileFlags
105:     constexpr FileFlags operator|(FileFlags lhs, FileFlags rhs) {
106:         return static_cast<FileFlags>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
107:     }
108: 
109:     constexpr FileFlags operator&(FileFlags lhs, FileFlags rhs) {
110:         return static_cast<FileFlags>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
111:     }
112: 
113:     constexpr FileFlags operator~(FileFlags flag) {
114:         return static_cast<FileFlags>(~static_cast<uint16_t>(flag));
115:     }
116: 
117:     /// Mask of flags that select the row codec.  Writer strips these from
118:     /// user-provided flags and sets them exclusively via RowCodecFileFlags
119:     /// to guarantee the file header matches the compile-time codec.
120:     inline constexpr FileFlags ROW_CODEC_FLAGS_MASK =
121:         FileFlags::ZERO_ORDER_HOLD | FileFlags::DELTA_ENCODING;
122: 
123:     /**
124:      * @brief Identifies the row-level codec used for serialization/deserialization.
125:      *
126:      * Each ID maps to a concrete RowCodec class.  The ID is derived from
127:      * FileHeader flags and file minor version via resolveRowCodecId().
128:      */
129:     enum class RowCodecId : uint8_t {
130:         FLAT001,    ///< Dense flat binary: raw column data, no delta/compression
131:         ZOH001,     ///< Zero-Order-Hold: detects unchanged columns
132:         DELTA002,   ///< Delta + VLE: type-grouped, combined header codes
133:     };
134: 
135:     // When adding a new RowCodecId value, this static_assert will fire.
136:     // Follow the checklist in the recipe comment above resolveRowCodecId().
137:     constexpr size_t ROW_CODEC_COUNT = 3;
138:     static_assert(static_cast<int>(RowCodecId::DELTA002) + 1 == ROW_CODEC_COUNT,
139:         "New RowCodecId added \u2014 update resolveRowCodecId(), ROW_CODEC_COUNT, "
140:         "and RowCodecDispatch::setup(). See VERSIONING.md \u00a7Codec Registry.");
141: 
142:     // \u2500\u2500 HOW TO ADD A NEW ROW CODEC \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n    // 1. Add enum value to RowCodecId (and bump ROW_CODEC_COUNT)
143:     // 2. Add version threshold in resolveRowCodecId(): if (minor >= N) return new codec
144:     // 3. Add case to RowCodecDispatch::setup() switch
145:     // 4. Bump version::MINOR via git tag
146:     // 5. See VERSIONING.md \u00a7Codec Registry for full details
147:     // \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
148: 
149:     /**
150:      * @brief Derive the RowCodecId from file header fields.
151:      *
152:      * Uses file minor version and FileFlags to select the row codec.
153:      * The fileMinor parameter enables version-gated codec selection
154:      * for backward compatibility.
155:      *
156:      * @param fileMinor  Minor version from the file header
157:      * @param flags  Feature flags from the file header
158:      */
159:     inline constexpr RowCodecId resolveRowCodecId([[maybe_unused]] uint8_t fileMinor,
160:                                                   FileFlags flags) noexcept {
161:         if ((flags & FileFlags::DELTA_ENCODING) != FileFlags::NONE)
162:             return RowCodecId::DELTA002;
163:         else if ((flags & FileFlags::ZERO_ORDER_HOLD) != FileFlags::NONE)
164:             return RowCodecId::ZOH001;
165:         else
166:             return RowCodecId::FLAT001;
167:     }
168: 
169:     /**
170:      * @brief Identifies the file-level codec used for framing, compression and I/O.
171:      *
172:      * Each ID maps to a concrete FileCodec class.  The ID is derived from
173:      * FileHeader fields (compression_level, FileFlags::STREAM_MODE) — it is
174:      * NOT stored explicitly in the file.
175:      *
176:      * Naming: FileCodec + Structure + [Compression] + Version
177:      */
178:     enum class FileCodecId : uint8_t {
179:         STREAM_001,             ///< Stream-Raw: no packets, no compression, per-row XXH32 checksums
180:         STREAM_LZ4_001,         ///< Stream-LZ4: no packets, streaming LZ4 compression, per-row XXH32 checksums
181:         PACKET_001,             ///< Packet-Raw: packet framing + checksums, no compression
182:         PACKET_LZ4_001,         ///< Packet-LZ4-Streaming: packet framing + streaming LZ4 (v1.3.0 default)
183:         PACKET_LZ4_BATCH_001,   ///< Packet-LZ4-Batch: packet framing + batch LZ4, async double-buffered I/O
184:     };
185: 
186:     // When adding a new FileCodecId value, this static_assert will fire.
187:     // Follow the checklist in the recipe comment above resolveFileCodecId().
188:     constexpr size_t FILE_CODEC_COUNT = 5;
189:     static_assert(static_cast<int>(FileCodecId::PACKET_LZ4_BATCH_001) + 1 == FILE_CODEC_COUNT,
190:         "New FileCodecId added — update resolveFileCodecId(), FILE_CODEC_COUNT, "
191:         "and FileCodecDispatch::setup(). See VERSIONING.md §Codec Registry.");
192: 
193:     // ── HOW TO ADD A NEW FILE CODEC ──────────────────────────────────
194:     // 1. Add enum value to FileCodecId (and bump FILE_CODEC_COUNT)
195:     // 2. Add version threshold below: if (fileMinor >= N) return new codec
196:     // 3. Add case to FileCodecDispatch::setup() switch
197:     // 4. Bump version::MINOR via git tag
198:     // 5. See VERSIONING.md §Codec Registry for full details
199:     // ─────────────────────────────────────────────────────────────────────
200: 
201:     /**
202:      * @brief Derive the FileCodecId from file header fields.
203:      *
204:      * Uses file minor version, compression_level and the STREAM_MODE /
205:      * BATCH_COMPRESS flags to select the codec.  The fileMinor parameter
206:      * enables version-gated codec selection for backward compatibility.
207:      *
208:      * @param fileMinor  Minor version from the file header (for version-gated dispatch)
209:      * @param compressionLevel  Compression level from the file header
210:      * @param flags  Feature flags from the file header
211:      */
212:     inline constexpr FileCodecId resolveFileCodecId([[maybe_unused]] uint8_t fileMinor,
213:                                                     uint8_t compressionLevel, FileFlags flags) noexcept {
214:         const bool stream = (flags & FileFlags::STREAM_MODE) != FileFlags::NONE;
215:         const bool compressed = compressionLevel > 0;
216:         const bool batch = (flags & FileFlags::BATCH_COMPRESS) != FileFlags::NONE;
217:         if (stream) {
218:             return compressed ? FileCodecId::STREAM_LZ4_001 : FileCodecId::STREAM_001;
219:         } else if (batch && compressed) {
220:             return FileCodecId::PACKET_LZ4_BATCH_001;
221:         } else {
222:             return compressed ? FileCodecId::PACKET_LZ4_001 : FileCodecId::PACKET_001;
223:         }
224:     }
225: 
226:     /** Column data type enumeration (stored as uint8_t in file) 
227:      * Ensure the order matches ValueType variant! As we rely on index() to match @see isType()
228:      */
229:     enum class ColumnType : uint8_t {
230:         BOOL,
231:         UINT8,
232:         UINT16,
233:         UINT32,
234:         UINT64,
235:         INT8,
236:         INT16,
237:         INT32,
238:         INT64,
239:         FLOAT,
240:         DOUBLE,
241:         STRING,
242:         VOID = 255
243:     };
244:     /** Variant type representing all possible column value types
245:      *  Ensure the order matches ColumnType enum! As we rely on index() to match @see isType()
246:      */
247:     using ValueType = std::variant< bool, 
248:                                     uint8_t, 
249:                                     uint16_t, 
250:                                     uint32_t, 
251:                                     uint64_t, 
252:                                     int8_t, 
253:                                     int16_t, 
254:                                     int32_t, 
255:                                     int64_t, 
256:                                     float, 
257:                                     double, 
258:                                     std::string>;
259: 
260:     inline bool isType(const ValueType& value, const ColumnType& type) {
261:         return value.index() == static_cast<size_t>(type);
262:     }
263: 
264:     // Helper template for static_assert
265:     template<typename T>
266:     constexpr bool ALWAYS_FALSE = false;
267: 
268:     // Convert C++ type to ColumnType enum
269:     template<typename T>
270:     constexpr ColumnType toColumnType() {
271:         if      constexpr (std::is_same_v<T, bool>)     return ColumnType::BOOL;
272:         else if constexpr (std::is_same_v<T, int8_t>)   return ColumnType::INT8;
273:         else if constexpr (std::is_same_v<T, int16_t>)  return ColumnType::INT16;
274:         else if constexpr (std::is_same_v<T, int32_t>)  return ColumnType::INT32;
275:         else if constexpr (std::is_same_v<T, int64_t>)  return ColumnType::INT64;
276:         else if constexpr (std::is_same_v<T, uint8_t>)  return ColumnType::UINT8;
277:         else if constexpr (std::is_same_v<T, uint16_t>) return ColumnType::UINT16;
278:         else if constexpr (std::is_same_v<T, uint32_t>) return ColumnType::UINT32;
279:         else if constexpr (std::is_same_v<T, uint64_t>) return ColumnType::UINT64;
280:         else if constexpr (std::is_same_v<T, float>)    return ColumnType::FLOAT;
281:         else if constexpr (std::is_same_v<T, double>)   return ColumnType::DOUBLE;
282:         else if constexpr (std::is_same_v<T, std::string>)   return ColumnType::STRING;
283:         else if constexpr (std::is_same_v<T, std::string_view>) return ColumnType::STRING;
284:         else static_assert(ALWAYS_FALSE<T>, "Unsupported type");
285:     };
286: 
287:     // Base template
288:     template<ColumnType Type>
289:     struct GetTypeT {
290:         using type = void; // Invalid by default
291:     };
292: 
293:     // Specializations
294:     template<> struct GetTypeT< ColumnType::BOOL   > { using type = bool;     };
295:     template<> struct GetTypeT< ColumnType::UINT8  > { using type = uint8_t;  };
296:     template<> struct GetTypeT< ColumnType::UINT16 > { using type = uint16_t; };
297:     template<> struct GetTypeT< ColumnType::UINT32 > { using type = uint32_t; };
298:     template<> struct GetTypeT< ColumnType::UINT64 > { using type = uint64_t; };
299:     template<> struct GetTypeT< ColumnType::INT8   > { using type = int8_t;   };
300:     template<> struct GetTypeT< ColumnType::INT16  > { using type = int16_t;  };
301:     template<> struct GetTypeT< ColumnType::INT32  > { using type = int32_t;  };
302:     template<> struct GetTypeT< ColumnType::INT64  > { using type = int64_t;  };
303:     template<> struct GetTypeT< ColumnType::FLOAT  > { using type = float;    };
304:     template<> struct GetTypeT< ColumnType::DOUBLE > { using type = double;   };
305:     template<> struct GetTypeT< ColumnType::STRING > { using type = std::string; };
306: 
307:     inline constexpr ColumnType toColumnType(const ValueType& value) {
308:         return std::visit([](auto&& arg) -> ColumnType {
309:             using T = std::decay_t<decltype(arg)>;
310:             return toColumnType<T>();
311:         }, value);
312:     }
313: 
314:     // Helper functions for type conversion
315:     inline constexpr ColumnType toColumnType(const std::string& typeString) {
316:         if (typeString == "bool")   return ColumnType::BOOL;
317:         if (typeString == "uint8")  return ColumnType::UINT8;
318:         if (typeString == "uint16") return ColumnType::UINT16;
319:         if (typeString == "uint32") return ColumnType::UINT32;
320:         if (typeString == "uint64") return ColumnType::UINT64;
321:         if (typeString == "int8")   return ColumnType::INT8;
322:         if (typeString == "int16")  return ColumnType::INT16;
323:         if (typeString == "int32")  return ColumnType::INT32;
324:         if (typeString == "int64")  return ColumnType::INT64;
325:         if (typeString == "float")  return ColumnType::FLOAT;
326:         if (typeString == "double") return ColumnType::DOUBLE;
327:         if (typeString == "string") return ColumnType::STRING;
328:         if (typeString == "void" || typeString.empty())  return ColumnType::VOID;
329:         throw std::invalid_argument("Unknown type: " + typeString);  // Fail loud
330:     }
331: 
332:     inline constexpr size_t alignOf(ColumnType type) {
333:         switch (type) {
334:             case ColumnType::BOOL:   return alignof(bool);
335:             case ColumnType::UINT8:  return alignof(uint8_t);
336:             case ColumnType::UINT16: return alignof(uint16_t);
337:             case ColumnType::UINT32: return alignof(uint32_t);
338:             case ColumnType::UINT64: return alignof(uint64_t);
339:             case ColumnType::INT8:   return alignof(int8_t);
340:             case ColumnType::INT16:  return alignof(int16_t);
341:             case ColumnType::INT32:  return alignof(int32_t);
342:             case ColumnType::INT64:  return alignof(int64_t);
343:             case ColumnType::FLOAT:  return alignof(float);
344:             case ColumnType::DOUBLE: return alignof(double);
345:             case ColumnType::STRING: return alignof(std::string);
346:             default: return 1; // Default to 1 for unknown types
347:         }
348:     }
349: 
350:     inline constexpr size_t sizeOf(ColumnType type) {
351:         switch (type) {
352:             case ColumnType::BOOL:   return sizeof(bool);
353:             case ColumnType::UINT8:  return sizeof(uint8_t);
354:             case ColumnType::UINT16: return sizeof(uint16_t);
355:             case ColumnType::UINT32: return sizeof(uint32_t);
356:             case ColumnType::UINT64: return sizeof(uint64_t);
357:             case ColumnType::INT8:   return sizeof(int8_t);
358:             case ColumnType::INT16:  return sizeof(int16_t);
359:             case ColumnType::INT32:  return sizeof(int32_t);
360:             case ColumnType::INT64:  return sizeof(int64_t);
361:             case ColumnType::FLOAT:  return sizeof(float);
362:             case ColumnType::DOUBLE: return sizeof(double);
363:             case ColumnType::STRING: return sizeof(std::string);
364:             default: return 0; // Default to 0 for unknown types
365:         }
366:     }
367: 
368:     inline constexpr std::string_view toString(ColumnType type) {
369:         switch (type) {
370:             case ColumnType::BOOL:   return "bool";
371:             case ColumnType::UINT8:  return "uint8";
372:             case ColumnType::UINT16: return "uint16";
373:             case ColumnType::UINT32: return "uint32";
374:             case ColumnType::UINT64: return "uint64";
375:             case ColumnType::INT8:   return "int8";
376:             case ColumnType::INT16:  return "int16";
377:             case ColumnType::INT32:  return "int32";
378:             case ColumnType::INT64:  return "int64";
379:             case ColumnType::FLOAT:  return "float";
380:             case ColumnType::DOUBLE: return "double";
381:             case ColumnType::STRING: return "string";
382:             default: return "UNKNOWN";
383:         }
384:     }
385: 
386:     // Stream operator for ColumnType
387:     inline std::ostream& operator<<(std::ostream& os, ColumnType type) {
388:         return os << toString(type);
389:     }
390: 
391:     /**
392:      * @brief Get default value for a given column data type
393:      * @param type The column data type
394:      * @return Default ValueType for the specified type
395:      */
396:     inline ValueType defaultValue(ColumnType type) {
397:         switch (type) {
398:             case ColumnType::BOOL:   return ValueType{bool{false}};
399:             case ColumnType::UINT8:  return ValueType{uint8_t{0} };
400:             case ColumnType::UINT16: return ValueType{uint16_t{0}};
401:             case ColumnType::UINT32: return ValueType{uint32_t{0}};
402:             case ColumnType::UINT64: return ValueType{uint64_t{0}};
403:             case ColumnType::INT8:   return ValueType{int8_t{0}  };
404:             case ColumnType::INT16:  return ValueType{int16_t{0} };
405:             case ColumnType::INT32:  return ValueType{int32_t{0} };
406:             case ColumnType::INT64:  return ValueType{int64_t{0} };
407:             case ColumnType::FLOAT:  return ValueType{float{0.0f}};
408:             case ColumnType::DOUBLE: return ValueType{double{0.0}};
409:             case ColumnType::STRING: return ValueType{std::string{}};
410:             default: throw std::runtime_error("Unknown column type");
411:         }
412:     }
413: 
414:     template<typename Type>
415:     constexpr Type defaultValueT() {
416:         if constexpr (std::is_same_v<Type, bool>) {
417:             return bool{false};
418:         } else if constexpr (std::is_same_v<Type, uint8_t> ) {
419:             return uint8_t{0};
420:         } else if constexpr (std::is_same_v<Type, uint16_t>) {
421:             return uint16_t{0};
422:         } else if constexpr (std::is_same_v<Type, uint32_t>) {
423:             return uint32_t{0};
424:         } else if constexpr (std::is_same_v<Type, uint64_t>) {
425:             return uint64_t{0};
426:         } else if constexpr (std::is_same_v<Type, int8_t>  ) {
427:             return int8_t{0};
428:         } else if constexpr (std::is_same_v<Type, int16_t> ) {
429:             return int16_t{0};
430:         } else if constexpr (std::is_same_v<Type, int32_t> ) {
431:             return int32_t{0};
432:         } else if constexpr (std::is_same_v<Type, int64_t> ) {
433:             return int64_t{0}; 
434:         } else if constexpr (std::is_same_v<Type, float>   ) {
435:             return float{0.0f};
436:         } else if constexpr (std::is_same_v<Type, double>  ) {
437:             return double{0.0};
438:         } else if constexpr (std::is_same_v<Type, std::string>) {
439:             return std::string{};  // Empty string
440:         } else {
441:             static_assert(ALWAYS_FALSE<Type>, "Unsupported type for defaultValueT");
442:         }
443:     }
444:         
445:     template<typename T>
446:     constexpr uint8_t wireSizeOf() {
447:         if      constexpr (std::is_same_v<T, bool>    ) return sizeof(bool);
448:         else if constexpr (std::is_same_v<T, uint8_t> ) return sizeof(uint8_t);
449:         else if constexpr (std::is_same_v<T, uint16_t>) return sizeof(uint16_t);
450:         else if constexpr (std::is_same_v<T, uint32_t>) return sizeof(uint32_t);
451:         else if constexpr (std::is_same_v<T, uint64_t>) return sizeof(uint64_t);
452:         else if constexpr (std::is_same_v<T, int8_t>  ) return sizeof(int8_t);
453:         else if constexpr (std::is_same_v<T, int16_t> ) return sizeof(int16_t);
454:         else if constexpr (std::is_same_v<T, int32_t> ) return sizeof(int32_t);
455:         else if constexpr (std::is_same_v<T, int64_t> ) return sizeof(int64_t);
456:         else if constexpr (std::is_same_v<T, float>   ) return sizeof(float);
457:         else if constexpr (std::is_same_v<T, double>  ) return sizeof(double);
458:         else if constexpr (std::is_same_v<T, std::string>) return sizeof(uint16_t);    // wire format: uint16_t length prefix
459:         else static_assert(ALWAYS_FALSE<T>, "Unsupported type");
460:     }
461: 
462:     // Helper function to get size for each column type
463:     constexpr uint8_t wireSizeOf(ColumnType type) {
464:         switch (type) {
465:             case ColumnType::BOOL:   return sizeof(bool);
466:             case ColumnType::UINT8:  return sizeof(uint8_t);
467:             case ColumnType::UINT16: return sizeof(uint16_t);
468:             case ColumnType::UINT32: return sizeof(uint32_t);
469:             case ColumnType::UINT64: return sizeof(uint64_t);
470:             case ColumnType::INT8:   return sizeof(int8_t);
471:             case ColumnType::INT16:  return sizeof(int16_t);
472:             case ColumnType::INT32:  return sizeof(int32_t);
473:             case ColumnType::INT64:  return sizeof(int64_t);
474:             case ColumnType::FLOAT:  return sizeof(float);
475:             case ColumnType::DOUBLE: return sizeof(double);
476:             case ColumnType::STRING: return sizeof(uint16_t);    // wire format: uint16_t length prefix
477:             default: throw std::runtime_error("Unknown column type");
478:         }
479:     }
480: 
481:     template<size_t candidate, typename ...T>
482:     void setTupleValue(std::tuple<T...>& tuple, size_t index, const ValueType& value) {
483:         if constexpr(candidate >= sizeof...(T)) {
484:             throw std::out_of_range("Index out of range");
485:         } else {
486:             if(candidate == index) {
487:                 using TargetType = std::tuple_element_t<candidate, std::tuple<T...>>;
488:                 if (std::holds_alternative<TargetType>(value)) {
489:                     std::get<candidate>(tuple) = std::get<TargetType>(value);
490:                 } else {
491:                     throw std::runtime_error("Type mismatch in setTupleValue");
492:                 }
493:             } else {
494:                 setTupleValue<candidate + 1, T...>(tuple, index, value);
495:             }
496:         }
497:     }
498: 
499:     template<size_t candidate, typename ...T>
500:     std::variant<T...> getTupleValue(const std::tuple<T...>& tuple, size_t index) {
501:         if constexpr(candidate >= sizeof...(T)) {
502:             throw std::out_of_range("Index out of range");
503:         } else {
504:             if(candidate == index) {
505:                 return std::get<candidate>(tuple);
506:             } else {
507:                 return getTupleValue<candidate + 1, T...>(tuple, index);
508:             }
509:         }
510:     } 
511: 
512:     // convertValueType removed — zero callers in codebase (dead code since 2026-02-25 review).
513:     // Retrievable from git history if ever needed.
514: 
515:     template<typename T>
516:     inline T unalignedRead(const void *src) {
517:         static_assert(std::is_trivially_copyable_v<T>, "unalignedRead requires trivially copyable type");
518:         std::array<std::byte, sizeof(T)> raw{};
519:         std::memcpy(raw.data(), src, sizeof(T));
520:         return std::bit_cast<T>(raw);
521:     }
522: 
523:     template<typename T>
524:     inline void unalignedWrite(void *dst, const T& value) {
525:         static_assert(std::is_trivially_copyable_v<T>, "unalignedWrite requires trivially copyable type");
526:         const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
527:         std::memcpy(dst, raw.data(), sizeof(T));
528:     }
529: 
530:     namespace detail {
531:         // Validation helper: allows templates to check if type is std::variant
532:         template<typename T> struct is_variant : std::false_type {};
533:         template<typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};
534: 
535:         template<typename T>
536:         inline constexpr bool is_variant_v = is_variant<T>::value;
537: 
538:         // Validation helper: Checks if T is an alternative in std::variant<Types...>
539:         template <typename T, typename Variant> struct is_in_variant;
540: 
541:         template <typename T, typename... Ts>
542:         struct is_in_variant<T, std::variant<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};
543: 
544:         template <typename T, typename Variant>
545:         constexpr bool is_in_variant_v = is_in_variant<T, Variant>::value;
546: 
547:         // Helper to check if a type is supported by BCSV (primitive types only, no string)
548:         template<typename T>
549:         struct is_primitive : std::false_type {};
550: 
551:         template<> struct is_primitive<bool>     : std::true_type {};
552:         template<> struct is_primitive<int8_t>   : std::true_type {};
553:         template<> struct is_primitive<int16_t>  : std::true_type {};
554:         template<> struct is_primitive<int32_t>  : std::true_type {};
555:         template<> struct is_primitive<int64_t>  : std::true_type {};
556:         template<> struct is_primitive<uint8_t>  : std::true_type {};
557:         template<> struct is_primitive<uint16_t> : std::true_type {};
558:         template<> struct is_primitive<uint32_t> : std::true_type {};
559:         template<> struct is_primitive<uint64_t> : std::true_type {};
560:         template<> struct is_primitive<float>    : std::true_type {};
561:         template<> struct is_primitive<double>   : std::true_type {};
562: 
563:         template<typename T>
564:         inline constexpr bool is_primitive_v = is_primitive<T>::value;
565: 
566:         // Helper to check if a type is a string-like type
567:         template<typename T>
568:         struct is_string_like : std::false_type {};
569: 
570:         template<> struct is_string_like<std::string> : std::true_type {};
571:         template<> struct is_string_like<std::string_view> : std::true_type {};
572:         //template<> struct is_string_like<const char*> : std::true_type {};
573: 
574:         template<typename T>
575:         inline constexpr bool is_string_like_v = is_string_like<T>::value;
576: 
577:         // C++20 Concept: Types that can be assigned to BCSV columns
578:         // Must be defined AFTER the helper traits it uses
579:         template<typename T>
580:         concept BcsvAssignable = 
581:             is_primitive_v<std::decay_t<T>> ||
582:             is_string_like_v<std::decay_t<T>> ||
583:             (std::is_convertible_v<std::decay_t<T>, bool> && !std::is_arithmetic_v<std::decay_t<T>>);  // For std::_Bit_reference
584:     }    
585: 
586: } // namespace bcsv

(End of file - total 586 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/layout.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/layout.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: #include <array>
13: #include <cassert>
14: #include <cstddef>
15: #include <functional>
16: #include <stdexcept>
17: #include <string>
18: #include <string_view>
19: #include <vector>
20: #include <cstdint>
21: #include <iosfwd>
22: #include <memory>
23: #include "bitset.h"
24: #include "column_name_index.h"
25: #include "definitions.h"
26: 
27: // Forward declarations to avoid circular dependencies
28: namespace bcsv {
29:     class Row;
30:     class LayoutGuard;
31:     template<typename... ColumnTypes> class RowStatic;
32: }
33: 
34: namespace bcsv {
35: 
36:     struct ColumnDefinition {
37:         std::string name;
38:         ColumnType  type;
39: 
40:         ColumnDefinition() 
41:             : name(""), type(ColumnType::BOOL) {}
42:         ColumnDefinition(std::string_view n, ColumnType t)
43:             : name(n), type(t) {}
44:     };
45:     
46:     // ========================================================================
47:     // Layout Concept - Replaces LayoutInterface
48:     // ========================================================================
49:     template<typename T>
50:     concept LayoutConcept = requires(T layout, const T& const_layout, size_t index, const std::string& name, void* owner) {
51:         // Basic layout information
52:         { const_layout.columnCount()                    } -> std::convertible_to<size_t>;
53:         { const_layout.columnIndex(name)                } -> std::convertible_to<size_t>;
54:         { const_layout.columnName(index)                } -> std::convertible_to<std::string>;
55:         { const_layout.columnType(index)                } -> std::convertible_to<ColumnType>;
56:         { const_layout.hasColumn(name)                  } -> std::convertible_to<bool>;    
57:         { layout.setColumnName(index, name)             };  // No return type constraint (may return void or bool, may throw on error)
58:         { const_layout.isCompatible(const_layout)       } -> std::convertible_to<bool>;
59: 
60:         // Type information
61:         typename T::RowType;  // Each layout must define its row type
62:     };
63: 
64:     /**
65:      * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
66:      * This layout is flexible and can be modified at runtime.
67:      * 
68:      * Note: This class is NOT thread-safe. External synchronization required for concurrent access.
69:      */
70:     class Layout {
71:     public:
72:         /**
73:          * @brief Shared layout data with all layout logic and observer management.
74:          * Contains column metadata and maintains consistency between names, indices, and types.
75:          * 
76:          * Note: NOT thread-safe. External synchronization required for concurrent access.
77:          */
78:         class Data {
79:         public:
80:             // Describes a single column transformation during layout update
81:             struct Change {
82:                 uint16_t   index;      // Column index (post-transformation position for adds)
83:                 ColumnType old_type;   // Type before change (nullopt if column is being added)
84:                 ColumnType new_type;   // Type after change (nullopt if column is being removed)
85:             };
86: 
87:             struct Callbacks {
88:                 std::function<void(const std::vector<Change>&)> update;  // Called BEFORE layout updates
89:             };
90: 
91:         private:
92:             std::vector<std::pair<void*, Callbacks>> callbacks_;     // Registered callbacks for layout changes
93:             std::vector<std::string>                 column_names_;  // column --> name
94:             ColumnNameIndex<0>                       column_index_;  // name --> column
95:             std::vector<ColumnType>                  column_types_;  // column --> type
96:             std::vector<uint32_t>                    offsets_;       // Unified per-column offsets into Row's storage containers (bits_, data_, strg_). Meaning depends on columnType: BOOL→bit index in bits_, STRING→index in strg_, scalar→byte offset in data_ (aligned).
97:             std::vector<uint32_t>                    offsets_packed_;// Per-column offsets in packed wire layout (BOOL→bit index, STRING→string index, scalar→byte offset without alignment)
98:             uint32_t                                 column_count_bool_{0};
99:             uint32_t                                 column_count_strings_{0};
100:             uint32_t                                 structural_lock_count_{0};  // Guard count: >0 => structural mutations throw
101:                                                                                 // Plain counter — library is single-threaded.
102: 
103:             // Internal helpers
104:             void rebuildColumnIndex();
105:             void rebuildOffsets();  // Recomputes offsets_ from column_types_
106:             void rebuildPackedMetadata(); // Recomputes packed wire metadata from column_types_
107:             void checkRange(size_t index) const;
108: 
109:         public:
110:             Data();
111:             Data(const Data& other);  // Copy layout data, not observers
112:             Data(Data&& other) noexcept;  // Move layout data, not observers; new lock count is 0
113:             Data& operator=(const Data&) = delete;
114:             Data& operator=(Data&&) = delete;
115:             ~Data() = default;
116: 
117:             // ============================================================
118:             // Read-only accessors
119:             // ============================================================
120:             size_t columnCount(ColumnType type) const noexcept;           
121:             size_t columnCount() const noexcept { return column_types_.size(); }
122:             size_t columnIndex(const std::string& name) const { return column_index_[name]; }
123:             const std::string& columnName(size_t index) const;
124:             ColumnType columnType(size_t index) const;
125:             const std::vector<ColumnType>& columnTypes() const noexcept { return column_types_; }
126:             const std::vector<std::string>& columnNames() const noexcept { return column_names_; }
127:             bool hasColumn(const std::string& name) const { return column_index_.contains(name); }
128:             bool isCompatible(const Data& other) const;
129:             uint32_t columnOffset(size_t index) const;
130:             const std::vector<uint32_t>& columnOffsets() const noexcept { return offsets_; }
131:             uint32_t columnOffsetPacked(size_t index) const;
132:             const std::vector<uint32_t>& columnOffsetsPacked() const noexcept { return offsets_packed_; }
133:             
134:             /// Compute unified offsets from a type vector. Used by Row's onLayoutUpdate to pre-compute new offsets
135:             /// before the layout has been updated. Returns total data_ byte size via out parameter.
136:             static void computeOffsets(const std::vector<ColumnType>& types, std::vector<uint32_t>& offsets, uint32_t& dataSize);
137: 
138:             // ============================================================
139:             // Layout modification operations (with observer notifications)
140:             // ============================================================
141:             void addColumn(ColumnDefinition column, size_t position = SIZE_MAX);
142:             void clear();
143:             void removeColumn(size_t index);
144:             void setColumnName(size_t index, std::string name);
145:             void setColumnType(size_t index, ColumnType type);
146:             void setColumns(const std::vector<ColumnDefinition>& columns);
147:             void setColumns(const std::vector<std::string>& columnNames, const std::vector<ColumnType>& columnTypes);
148: 
149:         private:
150:             /// Throws std::logic_error if any codec guard is held.  Called at
151:             /// the top of every structural mutation method.
152:             void throwIfLocked(const char* method) const;
153: 
154:         public:
155:             // ============================================================
156:             // Structural lock (codec guard)
157:             // Plain counter — the library is single-threaded.
158:             // ============================================================
159:         private:
160:             friend class LayoutGuard;
161:             void acquireStructuralLock() noexcept  { ++structural_lock_count_; }
162:             void releaseStructuralLock() noexcept  {
163:                 assert(structural_lock_count_ > 0 && "releaseStructuralLock: underflow — more releases than acquires");
164:                 --structural_lock_count_;
165:             }
166:         public:
167:             bool isStructurallyLocked() const noexcept { return structural_lock_count_ > 0; }
168: 
169:             // ============================================================
170:             // Observer management
171:             // ============================================================
172:             void registerCallback(void* owner, Callbacks callbacks);
173:             void unregisterCallback(void* owner);
174:             void replaceCallbackOwner(void* old_owner, void* new_owner, Callbacks new_callbacks) noexcept;
175: 
176:         private:
177:             // Notification method (called internally before modifications)
178:             void notifyUpdate(const std::vector<Change>& changes);
179:         };
180: 
181:         using DataPtr = std::shared_ptr<Data>;
182: 
183:     private:
184:         DataPtr data_;
185: 
186:     public:
187:         using RowType     = Row;
188: 
189:         // ============================================================
190:         // Constructors
191:         // ============================================================
192:         Layout() : data_(std::make_shared<Data>()) {}
193:         explicit Layout(DataPtr data) : data_(std::move(data)) {}
194:         Layout(const Layout& other) = default;  // Shallow copy (shares data)
195:         explicit Layout(const std::vector<ColumnDefinition>& columns);
196:         explicit Layout(const std::vector<std::string>& columnNames, const std::vector<ColumnType>& columnTypes);
197:         ~Layout() = default;
198: 
199:         // ============================================================
200:         // Data access and sharing
201:         // ============================================================
202:         Layout clone() const;
203:         const DataPtr& data() const { return data_; }
204: 
205:         // ============================================================
206:         // Observer management (convenience for Row)
207:         // ============================================================
208:         void registerCallback(void* owner, Data::Callbacks callbacks) const {
209:             data_->registerCallback(owner, std::move(callbacks));
210:         }
211: 
212:         void unregisterCallback(void* owner) const {
213:             data_->unregisterCallback(owner);
214:         }
215: 
216:         void replaceCallbackOwner(void* old_owner, void* new_owner, Data::Callbacks new_callbacks) const noexcept {
217:             data_->replaceCallbackOwner(old_owner, new_owner, std::move(new_callbacks));
218:         }
219: 
220:         /// @brief Check whether any codec guard is currently locking this layout.
221:         bool isStructurallyLocked() const noexcept { return data_->isStructurallyLocked(); }
222: 
223:         // ============================================================
224:         // Layout information (facade - delegates to Data)
225:         // ============================================================        
226:         size_t columnCount() const noexcept { 
227:             return data_->columnCount(); 
228:         }
229:         
230:         size_t columnIndex(const std::string& name) const { 
231:             return data_->columnIndex(name); 
232:         }
233:         
234:         const std::string& columnName(size_t index) const { 
235:             return data_->columnName(index); 
236:         }
237:         
238:         ColumnType columnType(size_t index) const { 
239:             return data_->columnType(index); 
240:         }
241:         
242:         uint32_t columnOffset(size_t index) const {
243:             return data_->columnOffset(index);
244:         }
245: 
246:         uint32_t columnOffsetPacked(size_t index) const {
247:             return data_->columnOffsetPacked(index);
248:         }
249:         
250:         const std::vector<uint32_t>& columnOffsets() const noexcept {
251:             return data_->columnOffsets();
252:         }
253: 
254:         const std::vector<uint32_t>& columnOffsetsPacked() const noexcept {
255:             return data_->columnOffsetsPacked();
256:         }
257: 
258:         size_t columnCount(ColumnType type) const noexcept {
259:             return data_->columnCount(type);
260:         }
261:         
262:         const std::vector<ColumnType>& columnTypes() const noexcept { 
263:             return data_->columnTypes(); 
264:         }
265:         
266:         bool hasColumn(const std::string& name) const { 
267:             return data_->hasColumn(name); 
268:         }
269: 
270:         // ============================================================
271:         // Layout modification (facade - delegates to Data)
272:         // ============================================================
273:         void addColumn(ColumnDefinition column, size_t position = SIZE_MAX) {
274:             data_->addColumn(std::move(column), position);
275:         }
276:         
277:         void removeColumn(size_t index) {
278:             data_->removeColumn(index);
279:         }
280:         
281:         void setColumnName(size_t index, std::string name) {
282:             data_->setColumnName(index, std::move(name));
283:         }
284:         
285:         void setColumnType(size_t index, ColumnType type) {
286:             data_->setColumnType(index, type);
287:         }
288:         
289:         void setColumns(const std::vector<ColumnDefinition>& columns) {
290:             data_->setColumns(columns);
291:         }
292:         
293:         void setColumns(const std::vector<std::string>& columnNames, 
294:                        const std::vector<ColumnType>& columnTypes) {
295:             data_->setColumns(columnNames, columnTypes);
296:         }
297:         
298:         void clear() {
299:             data_->clear();
300:         }
301: 
302:         // ============================================================
303:         // Compatibility and assignment
304:         // ============================================================
305:         template<typename OtherLayout>
306:         bool isCompatible(const OtherLayout& other) const;
307:                 
308:         template<typename OtherLayout>
309:         Layout& operator=(const OtherLayout& other);
310: 
311:         template<typename OtherLayout>
312:         bool operator==(const OtherLayout& other) const;
313:     };
314: 
315:     /**
316:      * @brief Static layout definition for BCSV files.
317:      * 
318:      * This layout is defined at compile-time to improve performance and reduce runtime overhead.
319:      */
320:     template<typename... ColumnTypes>
321:     class LayoutStatic {
322:     public:
323:         /**
324:          * @brief Shared layout data containing column names and name-to-index mapping.
325:          * Column types are compile-time constant, so only names need to be shared.
326:          */
327:         class Data {
328:         private:
329:             std::array<std::string, sizeof...(ColumnTypes)> column_names_;    // column --> name
330:             ColumnNameIndex< sizeof...(ColumnTypes) >       column_index_;    // name --> column
331: 
332:         public:
333:             Data();
334:             Data(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames);
335:             Data(const Data&) = default;
336:             Data(Data&&) = default;
337:             Data& operator=(const Data&) = default;
338:             Data& operator=(Data&&) = default;
339:             ~Data() = default;
340: 
341:             void clear();
342:             const std::string& columnName(size_t index) const;
343:             void setColumnName(size_t index, std::string name);
344:             template<typename Container>
345:             void setColumnNames(const Container& names, size_t offset = 0);
346:             size_t columnIndex(const std::string& name) const { return column_index_[name]; }
347:             bool hasColumn(const std::string& name) const { return column_index_.contains(name); }
348:             
349:             const auto& columnNames() const noexcept { return column_names_; }
350:             const auto& columnIndex() const noexcept { return column_index_; }
351:         };
352: 
353:         using DataPtr = std::shared_ptr<Data>;
354: 
355:     private:
356:         DataPtr data_;
357:         
358:         void checkRange(size_t index) const;
359: 
360:     public:
361:         using RowType           = RowStatic<ColumnTypes...>;
362:         using ColTypes          = std::tuple<ColumnTypes...>;
363:                                   template<size_t Index>
364:         using ColType           = std::tuple_element_t<Index, ColTypes>;
365:         using ColTypesArray     = std::array<ColumnType, sizeof...(ColumnTypes)>;
366:         static constexpr ColTypesArray COLUMN_TYPES = { toColumnType<ColumnTypes>()...  }; // column --> type
367:         static constexpr size_t COLUMN_COUNT_BOOL = (0 + ... + (std::is_same_v<ColumnTypes, bool> ? 1 : 0));
368:         static constexpr size_t COLUMN_COUNT_STRINGS = (0 + ... + (std::is_same_v<ColumnTypes, std::string> ? 1 : 0));
369: 
370:         static constexpr std::array<uint32_t, sizeof...(ColumnTypes)> COLUMN_OFFSETS_PACKED = []() {
371:             std::array<uint32_t, sizeof...(ColumnTypes)> r{};
372:             uint32_t bi = 0, di = 0, si = 0;
373:             size_t idx = 0;
374:             auto assign = [&](auto tag) {
375:                 using T = typename decltype(tag)::type;
376:                 if constexpr (std::is_same_v<T, bool>)             r[idx] = bi++;
377:                 else if constexpr (std::is_same_v<T, std::string>) r[idx] = si++;
378:                 else { r[idx] = di; di += static_cast<uint32_t>(sizeof(T)); }
379:                 ++idx;
380:             };
381:             (assign(std::type_identity<ColumnTypes>{}), ...);
382:             return r;
383:         }();
384: 
385: 
386:         LayoutStatic();
387:         explicit LayoutStatic(DataPtr data);
388:         LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames);
389:         LayoutStatic(const LayoutStatic&) = default;  // Shallow copy (shares data)
390:         LayoutStatic(LayoutStatic&&) = default;
391:         ~LayoutStatic() = default;
392:         
393:         // Data access
394:         LayoutStatic clone() const;
395:         const DataPtr& data() const { return data_; }
396: 
397:                                         // Basic Layout information (facade - delegates to Data or static)
398:         void                            clear()                                         { data_->clear(); }
399:         static constexpr size_t         columnCount() noexcept                          { return sizeof...(ColumnTypes); }
400:         size_t                          columnIndex(const std::string& name) const      { return data_->columnIndex(name); }
401:         const std::string&              columnName(size_t index) const                  { checkRange(index); return data_->columnName(index); }
402:                                         template<size_t Index>
403:         static constexpr ColumnType     columnType()                                    { return COLUMN_TYPES[Index]; }          // compile-time version
404: 
405:         static ColumnType               columnType(size_t index)                        { if (index >= sizeof...(ColumnTypes)) { throw std::out_of_range("LayoutStatic::columnType: Index out of range"); } return COLUMN_TYPES[index]; } // runtime version
406:         static constexpr const ColTypesArray& 
407:                                         columnTypes() noexcept                          { return COLUMN_TYPES; }
408:         static constexpr size_t         columnCount(ColumnType type) noexcept {
409:             return (0 + ... + (toColumnType<ColumnTypes>() == type ? 1 : 0));
410:         }
411:         static constexpr uint32_t       columnOffsetPacked(size_t index)                { return COLUMN_OFFSETS_PACKED[index]; }
412:         static constexpr const std::array<uint32_t, sizeof...(ColumnTypes)>&
413:                                         columnOffsetsPacked() noexcept                  { return COLUMN_OFFSETS_PACKED; }
414:         bool                            hasColumn(const std::string& name) const        { return data_->hasColumn(name); }
415:                                         
416:                                         template<typename OtherLayout>
417:         bool                            isCompatible(const OtherLayout& other) const;
418:         void                            setColumnName(size_t index, std::string name)   { data_->setColumnName(index, std::move(name)); }
419:                                         template<typename Container>
420:         void                            setColumnNames(const Container& names, size_t offset = 0) { data_->setColumnNames(names, offset); }
421:                                         
422:         LayoutStatic&                   operator=(const LayoutStatic&) = default;      // Shallow copy (shares data)
423:         LayoutStatic&                   operator=(LayoutStatic&&) = default;           // Move
424:                                         template<typename OtherLayout>
425:         LayoutStatic&                   operator=(const OtherLayout& other);           // Copies column names if layouts are compatible (types must match)
426:         
427:                                         template<typename OtherLayout>
428:         bool                            operator==(const OtherLayout& other) const;
429:     };
430: 
431:     /**
432:      * @brief Stream insertion operator for Layout — prints column metadata.
433:      *
434:      * Outputs a formatted table showing column index, name, and type for each
435:      * column in the layout.  Empty layouts print "Empty layout (no columns)".
436:      *
437:      * Example output for a 3-column layout:
438:      * @code
439:      * Col | Name   | Type
440:      * ----+--------+--------
441:      *   0 | time   | DOUBLE
442:      *   1 | sensor | INT32
443:      *   2 | label  | STRING
444:      * @endcode
445:      *
446:      * Works with both Layout (dynamic) and LayoutStatic<Ts...> via LayoutConcept.
447:      */
448:     template<LayoutConcept LayoutType>
449:     std::ostream& operator<<(std::ostream& os, const LayoutType& layout);
450: 
451: } // namespace bcsv

(End of file - total 451 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/row.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/row.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: #include <cstddef>
13: #include <iosfwd>
14: #include <span>
15: #include <string>
16: #include <string_view>
17: #include <tuple>
18: #include <type_traits>
19: #include <vector>
20: 
21: #include "definitions.h"
22: #include "layout.h"
23: #include "bitset.h"
24: #include "codec_row/row_codec_flat001.h"
25: 
26: namespace bcsv {
27: 
28: /**
29:  * @brief BCSV Binary Row Format Documentation
30:  * 
31:  * The BCSV flat wire format uses a four-section layout that mirrors the Row's
32:  * in-memory storage (bits_, data_, strg_). Sections are written sequentially,
33:  * enabling near-memcpy serialization and efficient codec access.
34:  * 
35:  * =============================================================================
36:  * FLAT ROW WIRE FORMAT LAYOUT
37:  * =============================================================================
38:  * 
39:  * [bits_section][data_section][strg_lengths][strg_data]
40:  * 
41:  * Section 1: bits_         Bit-packed boolean values, layout order
42:  * Section 2: data_         Tightly packed scalars (non-bool, non-string), layout order
43:  * Section 3: strg_lengths  uint16_t per string column, layout order
44:  * Section 4: strg_data     Concatenated string payloads, layout order
45:  * 
46:  * All sections are byte-aligned (no padding between them). A section with
47:  * zero elements contributes zero bytes to the wire format.
48:  * 
49:  * =============================================================================
50:  * SECTION 1: BITS_ (Boolean values)
51:  * =============================================================================
52:  * 
53:  * Size: ⌈bool_count / 8⌉ bytes  (0 bytes if no bool columns)
54:  * 
55:  * Bool columns are stored as individual bits in layout order:
56:  *   - Bit 0 = first bool column in layout, Bit 1 = second, etc.
57:  *   - Within each byte: bit 0 is least significant (rightmost).
58:  *   - Padding bits (above bool_count) are zero-filled to byte boundary.
59:  * 
60:  * ┌─────────────────────────────────────────────────────────────────┐
61:  * │   Byte 0: [pad|pad|pad|bool4|bool3|bool2|bool1|bool0]         │
62:  * │   Byte 1: [bool15|...|bool8]  (if bool_count > 8)             │
63:  * └─────────────────────────────────────────────────────────────────┘
64:  * 
65:  * For Row, bits_ can be memcpy'd directly
66:  * (size == ⌈bool_count / 8⌉) between row and wire format.
67:  * 
68:  * =============================================================================
69:  * SECTION 2: DATA_ (Scalar values)
70:  * =============================================================================
71:  * 
72:  * Size: sum of sizeOf(type) for all non-bool, non-string columns
73:  * 
74:  * Scalars are packed tightly in layout order with NO alignment padding:
75:  * ┌──────────┬──────────┬──────────┬──────────┐
76:  * │ INT8(1B) │DOUBLE(8B)│UINT16(2B)│ FLOAT(4B)│
77:  * └──────────┴──────────┴──────────┴──────────┘
78:  * 
79:  * Access uses memcpy to handle misalignment safely. The in-memory Row data_
80:  * vector uses aligned offsets (for SIMD-friendliness), so serialize/deserialize
81:  * copies each scalar individually — not a single memcpy of the whole section.
82:  * 
83:  * =============================================================================
84:  * SECTION 3: STRG_LENGTHS (String lengths)
85:  * =============================================================================
86:  * 
87:  * Size: string_count × sizeof(uint16_t) bytes  (0 bytes if no string columns)
88:  * 
89:  * One uint16_t per string column in layout order, giving the byte length of
90:  * each string payload in section 4. Max string length: 65,535 bytes.
91:  * 
92:  * ┌──────────────────┬──────────────────┬──────────────────┐
93:  * │  len_0 (uint16)  │  len_1 (uint16)  │  len_2 (uint16)  │
94:  * └──────────────────┴──────────────────┴──────────────────┘
95:  * 
96:  * String offsets into strg_data are derived by cumulative sum of lengths;
97:  * no explicit offsets are stored. Codec pre-computes these during setup.
98:  * 
99:  * =============================================================================
100:  * SECTION 4: STRG_DATA (String payloads)
101:  * =============================================================================
102:  * 
103:  * Size: sum of all string lengths
104:  * 
105:  * Concatenated raw string bytes in layout order. No null terminators,
106:  * no length prefixes, no delimiters. Empty strings contribute 0 bytes.
107:  * 
108:  * ┌────────────────┬────────────────┬────────────────┐
109:  * │  "Hello" (5B)  │  "" (0B)       │  "World" (5B)  │
110:  * └────────────────┴────────────────┴────────────────┘
111:  * 
112:  * =============================================================================
113:  * EXAMPLE: Row with ["John", 25, "Engineer", 3.14, true]
114:  * =============================================================================
115:  * 
116:  * Layout: [STRING, INT32, STRING, DOUBLE, BOOL]
117:  *   bool columns:   col 4             → bool_count = 1
118:  *   scalar columns: col 1 (INT32), col 3 (DOUBLE) → data_size = 12
119:  *   string columns: col 0, col 2      → string_count = 2
120:  * 
121:  * bits_ (1 byte):    [0000000|true]           = 0x01
122:  * data_ (12 bytes):  [INT32: 25][DOUBLE: 3.14]
123:  * strg_lengths (4B): [0x0004][0x0008]
124:  * strg_data (12B):   "JohnEngineer"
125:  * 
126:  * Total: 1 + 12 + 4 + 12 = 29 bytes
127:  * 
128:  * =============================================================================
129:  * SERIALIZATION ALGORITHM
130:  * =============================================================================
131:  * 
132:  * 1. Write bits_ section:
133:  *    - Pack bool column values into ⌈bool_count/8⌉ bytes (bit-packed, layout order)
134:  * 
135:  * 2. Write data_ section:
136:  *    - For each non-bool, non-string column in layout order:
137:  *        memcpy(buffer, &value, sizeOf(type))
138:  * 
139:  * 3. Write strg_lengths section:
140:  *    - For each string column in layout order:
141:  *        write uint16_t(min(string.length(), MAX_STRING_LENGTH))
142:  * 
143:  * 4. Write strg_data section:
144:  *    - For each string column in layout order:
145:  *        write string payload bytes (no terminator)
146:  * 
147:  * Note: Steps 3 and 4 can be combined into a single pass over string columns.
148:  * 
149:  * =============================================================================
150:  * DESERIALIZATION ALGORITHM
151:  * =============================================================================
152:  * 
153:  * 1. Compute section boundaries from layout wire-format metadata:
154:  *      bits_start   = 0
155:  *      data_start   = row_header_size
156:  *      strg_l_start = row_header_size + wire_data_size
157:  *      strg_d_start = row_header_size + wire_data_size + wire_strg_count * 2
158:  * 
159:  * 2. Read bits_ section → set bool column values
160:  * 
161:  * 3. Read data_ section → set scalar column values (memcpy each)
162:  * 
163:  * 4. Read strg_lengths → read strg_data → assign string column values
164:  *    (cumulative sum of lengths gives each string's offset in strg_data)
165:  * 
166:  * 5. Validate buffer size at each section boundary.
167:  * 
168:  * =============================================================================
169:  * PERFORMANCE CHARACTERISTICS
170:  * =============================================================================
171:  * 
172:  * Benefits:
173:  * - Wire format mirrors in-memory Row storage (bits_, data_, strg_)
174:  * - Bool columns use 1 bit instead of 1 byte (87.5% savings per bool)
175:  * - No StringAddr computation — lengths stored directly, offsets derived
176:  * - Scalar section is contiguous — good LZ4 compression, cache-friendly
177:  * - String lengths section enables O(1) skip of all strings if not needed
178:  * - Bit-level bool access (>>3 for byte, &7 for bit)
179:  * 
180:  * Trade-offs:
181:  * - Bool access requires bit-level addressing (trivial shift+mask)
182:  * - String access requires cumulative offset computation
183:  * - In-memory data_ is aligned but wire data_ is packed — per-column copy
184:  * 
185:  * =============================================================================
186:  * MEMORY LAYOUT CONSIDERATIONS
187:  * =============================================================================
188:  * 
189:  * - No alignment padding in wire format (minimizes file size)
190:  * - All access uses memcpy for alignment safety
191:  * - In-memory Row data_ has natural alignment (via Layout::Data offsets_)
192:  * - Codec stores section start offsets + per-column section-relative offsets
193:  * 
194:  * =============================================================================
195:  * ZERO ORDER HOLD (ZoH) COMPRESSION FORMAT
196:  * =============================================================================
197:  * 
198:  * When Zero Order Hold compression is enabled (FileFlags::ZERO_ORDER_HOLD),
199:  * rows use a completely different encoding optimized for time-series data
200:  * where values remain constant for extended periods.
201:  * 
202:  * IMPORTANT: Boolean columns have special encoding in ZoH format:
203:  * - Boolean values are encoded directly in the head_ Bitset
204:  * - Bit value 0 = false, Bit value 1 = true
205:  * - Boolean columns never contribute data to the data section
206:  * - Boolean encoding is independent of whether the value changed
207:  * 
208:  * =============================================================================
209:  * ZoH BINARY FORMAT LAYOUT
210:  * =============================================================================
211:  * 
212:  * [Head Bitset] [Changed Data 1] [Changed Data 2] ... [Changed Data N]
213:  * |            | |              | |              |   |              |
214:  * |Bool Values | | Value 1      | | Value 2      |   | Value N      |
215:  * |+Change Flags| (if changed) | | (if changed) |   | (if changed) |
216:  * 
217:  * =============================================================================
218:  * HEAD BITSET FORMAT (type-grouped layout)
219:  * =============================================================================
220:  * 
221:  * The head Bitset has columnCount bits with a type-grouped layout:
222:  *
223:  *   Bits [0 .. boolCount):           Boolean VALUES (same layout as row.bits_)
224:  *   Bits [boolCount .. columnCount): Change flags, grouped by ColumnType enum:
225:  *       UINT8 flags | UINT16 flags | UINT32 flags | UINT64 flags |
226:  *       INT8 flags  | INT16 flags  | INT32 flags  | INT64 flags  |
227:  *       FLOAT flags | DOUBLE flags | STRING flags
228:  *
229:  * Within each type group, columns appear in their original layout order.
230:  *
231:  * This type-grouped layout enables:
232:  * - Bulk assignRange/equalRange for bool values (word-level operations)
233:  * - Tight per-type inner loops for scalar change detection (no type dispatch)
234:  * - head_[0..boolCount) also serves as previous-bool-value storage
235:  *
236:  * Bit Layout Example (for 6 columns: [BOOL, INT32, BOOL, FLOAT, STRING, UINT8]):
237:  * ┌──────┬──────┬──────┬──────┬──────┬──────┬─────┬─────┐
238:  * │ PAD  │ PAD  │ STR  │FLOAT │INT32 │UINT8 │BOOL1│BOOL0│
239:  * │  7   │  6   │  5   │  4   │  3   │  2   │  1  │  0  │
240:  * └──────┴──────┴──────┴──────┴──────┴──────┴─────┴─────┘
241:  *  ↑ padding     ↑ STRING flag  ↑ FLOAT flag ↑ bool values
242:  *                ↑ change flags (type-grouped: UINT8, INT32, FLOAT, STRING)
243:  * 
244:  * Bit Meanings:
245:  * - Bits [0..boolCount): Boolean VALUES (0=false, 1=true), always present
246:  * - Bits [boolCount..columnCount): Change FLAGS (1=changed, 0=unchanged)
247:  *   grouped by ColumnType enum order (UINT8, UINT16, ..., DOUBLE, STRING)
248:  * 
249:  * Bit Ordering: Bit 0 is the least significant bit (rightmost),
250:  * padding bits are added to the left (most significant bits) to reach byte boundary.
251:  * 
252:  * Bitset Size: Rounded up to nearest byte boundary for efficient storage
253:  * 
254:  * =============================================================================
255:  * ZoH DATA SECTION FORMAT
256:  * =============================================================================
257:  * 
258:  * Only non-BOOL columns with their change flag set have data in the section.
259:  * Data appears in the same type-grouped order as the change flags in the
260:  * head Bitset (ColumnType enum order: UINT8, UINT16, ..., DOUBLE, STRING).
261:  * 
262:  * For BOOLEAN columns:
263:  * - No data in data section (value stored directly in head Bitset)
264:  * - Bit 0 = false, Bit 1 = true
265:  * - Boolean bits represent the actual value, not change status
266:  * - Booleans are always included in serialization regardless of changes
267:  * 
268:  * For STRING columns (if changed):
269:  * ┌─────────────────┬─────────────────────────────────────────┐
270:  * │   Length        │           String Payload                │
271:  * │  (uint16_t)     │         (Length bytes)                  │
272:  * │  (2 bytes)      │        (no null terminator)            │
273:  * └─────────────────┴─────────────────────────────────────────┘
274:  * - Length: String length in bytes (max 65,535)
275:  * - Payload: Raw string bytes (UTF-8 encoded)
276:  * - No offset calculation needed (in-place encoding)
277:  * 
278:  * For PRIMITIVE columns (if changed):
279:  * ┌─────────────────────────────────────────────────────────────────┐
280:  * │                     Raw Value Data                              │
281:  * │                (1, 2, 4, or 8 bytes)                            │
282:  * │              (little-endian encoding)                           │
283:  * └─────────────────────────────────────────────────────────────────┘
284:  * 
285:  * =============================================================================
286:  * ZoH EXAMPLE: Row with ["Alice", 30, "Manager", 75000.0, true]
287:  * =============================================================================
288:  * 
289:  * Layout: [STRING, INT32, STRING, DOUBLE, BOOL]
290:  * Scenario: Only INT32 and BOOL columns changed from previous row
291:  * 
292:  * Head Bitset (1 byte for 5 columns):
293:  * Type-grouped bit assignment:
294:  *   Bit 0: BOOL (col 4) = true  → value bit
295:  *   Bit 1: INT32 (col 1)        → change flag
296:  *   Bit 2: DOUBLE (col 3)       → change flag
297:  *   Bit 3: STRING (col 0)       → change flag
298:  *   Bit 4: STRING (col 2)       → change flag
299:  *
300:  * ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
301:  * │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  │
302:  * ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
303:  * │ PAD │ PAD │ PAD │STR2 │STR1 │DOUB │INT32│BOOL │
304:  * │  0  │  0  │  0  │  0  │  0  │  0  │  1  │  1  │
305:  * └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
306:  * 
307:  * Bitset Value: 0b00000011 = 0x03
308:  * - Bit 0 (BOOL col 4): 1 = boolean value is true (no data in section)
309:  * - Bit 1 (INT32 col 1): 1 = changed (4 bytes in data section)
310:  * - Bit 2 (DOUBLE col 3): 0 = unchanged (no data in section)
311:  * - Bit 3 (STRING col 0): 0 = unchanged (no data in section)
312:  * - Bit 4 (STRING col 2): 0 = unchanged (no data in section)
313:  * - Bits 5-7: Padding bits (always 0)
314:  * 
315:  * Data Section (4 bytes, type-grouped order: INT32 data):
316:  * ┌────────────────────┐
317:  * │       INT32        │
318:  * │     (4 bytes)      │
319:  * │      30            │
320:  * └────────────────────┘
321:  * 
322:  * Total ZoH Row Size: 5 bytes (1 Bitset + 4 data)
323:  * vs. Standard Row Size: 45 bytes (33 fixed + 12 variable)
324:  * Compression Ratio: 89% space savings!
325:  * 
326:  * =============================================================================
327:  * ZoH SERIALIZATION ALGORITHM
328:  * =============================================================================
329:  * 
330:  * 1. Bool bulk compare and assign:
331:  *    any_change = !head_.equalRange(row.bits_, 0, boolCount)
332:  *    head_.assignRange(row.bits_, 0, boolCount)
333:  * 
334:  * 2. Reserve space for head Bitset in buffer
335:  * 
336:  * 3. For each scalar ColumnType (UINT8..DOUBLE), iterate per-type offset vector:
337:  *    for each offset in off_<type>:
338:  *        if data_[offset] != row.data_[offset]:  // memcmp of TypeSize bytes
339:  *            head_[head_idx] = 1     // mark changed
340:  *            copy to data_[offset]   // update prev
341:  *            append value to buffer
342:  *        else:
343:  *            head_[head_idx] = 0     // mark unchanged
344:  *        head_idx++
345:  * 
346:  * 4. For each string column, compare and emit similarly
347:  * 
348:  * 5. If any_change:
349:  *        Write head_ to reserved buffer location
350:  *        Return span of serialized data
351:  *    Else:
352:  *        Undo buffer growth, return empty span (ZoH repeat)
353:  * 
354:  * =============================================================================
355:  * ZoH DESERIALIZATION ALGORITHM
356:  * =============================================================================
357:  * 
358:  * 1. Read head Bitset from buffer:
359:  *    head_.readFrom(buffer, head_.sizeBytes())
360:  * 
361:  * 2. Bool bulk extract:
362:  *    row.bits_.assignRange(head_, 0, boolCount)
363:  * 
364:  * 3. For each scalar ColumnType (UINT8..DOUBLE), iterate per-type offset vector:
365:  *    for each offset in off_<type>:
366:  *        if head_[head_idx]:
367:  *            memcpy from buffer into row.data_[offset]
368:  *        head_idx++
369:  *    // else: keep previous value (Zero Order Hold principle)
370:  * 
371:  * 4. For each string column:
372:  *    if head_[head_idx]:
373:  *        read uint16_t length, then string payload
374:  *    head_idx++
375:  * 
376:  * =============================================================================
377:  * ZoH PERFORMANCE CHARACTERISTICS
378:  * =============================================================================
379:  * 
380:  * Benefits:
381:  * - Dramatic space savings for slowly-changing time-series data (70-95% typical)
382:  * - Boolean values require only 1 bit instead of 1 byte (87.5% savings)
383:  * - Improved compression ratios with LZ4 due to sparse data patterns
384:  * - Reduced I/O bandwidth and memory usage
385:  * - Cache-friendly due to smaller serialized data
386:  * - Perfect for sensor data, financial tickers, configuration changes
387:  * 
388:  * Trade-offs:
389:  * - Not suitable for rapidly-changing data (overhead exceeds benefits)
390:  * - Deserialization requires maintaining previous row state
391:  * - Slightly more complex serialization/deserialization logic
392:  * - First row in each packet must be fully populated (no compression)
393:  * 
394:  * Optimal Use Cases:
395:  * - Time-series data with sparse updates
396:  * - Configuration data that changes infrequently  
397:  * - Sensor readings with stable periods
398:  * - Financial data with hold periods
399:  * - Any scenario where < 30% of columns change per row
400:  * 
401:  * =============================================================================
402:  */
403: 
404: 
405: 
406:     // Mutable visitor concept: accepts (size_t, T&) signature
407:     // Uses || (OR) to check if visitor accepts the pattern with at least one BCSV type
408:     // Generic lambdas with auto& will satisfy this constraint
409:     template<typename V>
410:     concept RowVisitor = 
411:         std::is_invocable_v<V, size_t, bool&> ||
412:         std::is_invocable_v<V, size_t, int8_t&> ||
413:         std::is_invocable_v<V, size_t, int16_t&> ||
414:         std::is_invocable_v<V, size_t, int32_t&> ||
415:         std::is_invocable_v<V, size_t, int64_t&> ||
416:         std::is_invocable_v<V, size_t, uint8_t&> ||
417:         std::is_invocable_v<V, size_t, uint16_t&> ||
418:         std::is_invocable_v<V, size_t, uint32_t&> ||
419:         std::is_invocable_v<V, size_t, uint64_t&> ||
420:         std::is_invocable_v<V, size_t, float&> ||
421:         std::is_invocable_v<V, size_t, double&> ||
422:         std::is_invocable_v<V, size_t, std::string&> ||
423:         std::is_invocable_v<V, size_t, std::string_view&>;
424: 
425:     // Const visitor concept: accepts (size_t, const T&)  
426:     // Uses || (OR) to check if visitor accepts the pattern with at least one BCSV type
427:     // Generic lambdas with const auto& will satisfy this constraint
428:     template<typename V>
429:     concept RowVisitorConst = 
430:         std::is_invocable_v<V, size_t, const bool&> ||
431:         std::is_invocable_v<V, size_t, const int8_t&> ||
432:         std::is_invocable_v<V, size_t, const int16_t&> ||
433:         std::is_invocable_v<V, size_t, const int32_t&> ||
434:         std::is_invocable_v<V, size_t, const int64_t&> ||
435:         std::is_invocable_v<V, size_t, const uint8_t&> ||
436:         std::is_invocable_v<V, size_t, const uint16_t&> ||
437:         std::is_invocable_v<V, size_t, const uint32_t&> ||
438:         std::is_invocable_v<V, size_t, const uint64_t&> ||
439:         std::is_invocable_v<V, size_t, const float&> ||
440:         std::is_invocable_v<V, size_t, const double&> ||
441:         std::is_invocable_v<V, size_t, const std::string&> ||
442:         std::is_invocable_v<V, size_t, const std::string_view&>;
443: 
444:     // Typed mutable visitor concept: accepts (size_t, T&) for a specific type T
445:     // Used by visit<T>() for compile-time type-safe iteration over homogeneous column ranges
446:     template<typename V, typename T>
447:     concept TypedRowVisitor = 
448:         std::is_invocable_v<V, size_t, T&>;
449: 
450:     // Typed const visitor concept: accepts (size_t, const T&) for a specific type T
451:     // Used by visitConst<T>() for compile-time type-safe read-only iteration
452:     template<typename V, typename T>
453:     concept TypedRowVisitorConst = 
454:         std::is_invocable_v<V, size_t, const T&>;
455: 
456:     /* Dynamic row with flexible layout (runtime-defined)
457:      *
458:      * Storage is split into three type-family containers:
459:      *   bits_  — Bitset storing bool column values
460:      *   data_  — contiguous byte buffer for scalar/arithmetic column values (aligned)
461:      *   strg_  — vector of strings for string column values
462:      *
463:      * Per-column offsets live in Layout::Data (shared across all rows sharing the same layout).
464:      * The meaning of layout_.columnOffset(i) depends on columnType(i):
465:      *   BOOL   → bit index into bits_
466:      *   STRING → index into strg_
467:      *   scalar → byte offset into data_
468:      *
469:      * bits_.size() == number of BOOL columns only.
470:      */
471:     // Forward declarations for codec friend access
472:     template<typename LayoutType> class RowCodecFlat001;
473:     template<typename LayoutType> class RowCodecZoH001;
474:     template<typename LayoutType> class RowCodecDelta002;
475: 
476:     class Row {
477:         // Codec friend access — direct access to bits_, data_, strg_ for serialization.
478:         // See docs/ITEM_11_PLAN.md §5 for rationale.
479:         template<typename LT> friend class RowCodecFlat001;
480:         template<typename LT> friend class RowCodecZoH001;
481:         template<typename LT> friend class RowCodecDelta002;
482: 
483:     private:
484:         // Immutable after construction
485:         Layout                      layout_;               // Shared layout data with observer callbacks
486: 
487:         // Mutable data — three type-family storage containers
488:         Bitset<>                    bits_;                  // Bool values (one bit per BOOL column)
489:         std::vector<std::byte>      data_;                 // Aligned scalar/arithmetic column values (no bools, no strings)
490:         std::vector<std::string>    strg_;                 // String column values
491: 
492:         // Observer callback for layout changes
493:         void onLayoutUpdate(const std::vector<Layout::Data::Change>& changes);
494: 
495:         // Legacy untyped access — implementation detail only, not for external use.
496:         // Bool columns use thread_local buffer that aliases on third interleaved call.
497:         [[nodiscard]] const void*   get(size_t index) const;
498: 
499:     public:
500:         Row() = delete; // no default constructor, we always need a layout
501:         Row(const Layout& layout);
502:         Row(const Row& other);
503:         Row(Row&& other) noexcept;
504: 
505:         ~Row();
506: 
507:         void                        clear();
508:         const Layout&               layout() const noexcept         { return layout_; }
509: 
510: 
511:                                     template<typename T>
512:         [[nodiscard]] decltype(auto) get(size_t index) const;
513:                                     template<typename T>
514:         void                        get(size_t index, std::span<T> &dst) const;
515:                                     template<typename T>
516:         [[nodiscard]] bool          get(size_t index, T &dst) const;                        // Flexible: allows type conversions, returns false on failure
517: 
518:                                     template<typename T>
519:         [[nodiscard]] decltype(auto) ref(size_t index);                                      // get a mutable reference to the internal data (returns T& for scalars/strings, Bitset<>::reference for bool)
520: 
521:                                     template<detail::BcsvAssignable T>
522:         void                        set(size_t index, const T& value);
523:         void                        set(size_t index, const char* value)        { set<std::string_view>(index, value); } // Convenience overload for C-style strings, forwarding to std::string_view version
524:                                     template<typename T>
525:         void                        set(size_t index, std::span<const T> values);
526: 
527:         /** @brief Visit columns with read-only access. @see row.hpp */
528:                                     template<RowVisitorConst Visitor>
529:         void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;
530: 
531:         /** @brief Visit all columns with read-only access. @see row.hpp */
532:                                     template<RowVisitorConst Visitor>
533:         void                        visitConst(Visitor&& visitor) const;
534: 
535:         /** @brief Visit columns with mutable access. @see row.hpp */
536:                                     template<RowVisitor Visitor>
537:         void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);
538: 
539:         /** @brief Visit all columns with mutable access. @see row.hpp */
540:                                     template<RowVisitor Visitor>
541:         void                        visit(Visitor&& visitor);
542: 
543:         /** @brief Type-safe visit: iterate columns of known type T with compile-time dispatch (no runtime switch). @see row.hpp */
544:                                     template<typename T, typename Visitor>
545:                                         requires TypedRowVisitor<Visitor, T>
546:         void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);
547: 
548:         /** @brief Type-safe const visit: iterate columns of known type T with compile-time dispatch (no runtime switch). @see row.hpp */
549:                                     template<typename T, typename Visitor>
550:                                         requires TypedRowVisitorConst<Visitor, T>
551:         void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;
552: 
553:         Row&                        operator=(const Row& other);               // Throws std::invalid_argument if layouts incompatible
554:         Row&                        operator=(Row&& other) noexcept;
555:     };
556: 
557: 
558:     /* Static row with compile-time defined layout */
559:     template<typename... ColumnTypes>
560:     class RowStatic {
561:     public:
562:         using LayoutType = LayoutStatic<ColumnTypes...>;
563:         static constexpr size_t COLUMN_COUNT = LayoutType::columnCount();
564:         
565:         template<size_t Index>
566:         using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;
567: 
568:         // Codec friend access — direct access to data_ for serialization.
569:         // See docs/ITEM_11_PLAN.md §5 for rationale.
570:         template<typename LT> friend class RowCodecFlat001;
571:         template<typename LT> friend class RowCodecZoH001;
572:         template<typename LT> friend class RowCodecDelta002;
573: 
574:     private:
575:         // Immutable after construction
576:         LayoutType layout_; 
577: 
578:         // Mutable data
579:         typename LayoutType::ColTypes   data_;
580: 
581:         // Legacy untyped access — implementation detail for get<T>(size_t), not for external use.
582:         [[nodiscard]] const void*   get(size_t index) const noexcept;
583: 
584:     public:
585:         // Constructors
586:         RowStatic() = delete;
587:         RowStatic(const LayoutType& layout);
588:         RowStatic(const RowStatic& other) = default;        // default copy constructor, tuple manages copying its members including strings
589:         RowStatic(RowStatic&& other) noexcept = default;    // default move constructor, tuple manages moving its members including strings
590:         ~RowStatic() = default;
591:        
592:                                     template<size_t Index = 0>
593:         void                        clear();
594:         const LayoutType&           layout() const noexcept         { return layout_; }  // Reconstruct facade
595: 
596:         // =========================================================================
597:         // 1. Static Access (Compile-Time Index) - Zero Overhead
598:         // =========================================================================
599:                                     template<size_t Index>
600:         [[nodiscard]] const auto&   get() const noexcept;                                   // Direct reference to column data. No overhead.
601: 
602:                                     template<size_t StartIndex, typename T, size_t Extent>
603:         void                        get(std::span<T, Extent> &dst) const;                   // Vectorized: exact match fast path, conversions if needed
604:         
605:         // =========================================================================
606:         // 2. Dynamic Access (Runtime Index) - Branching Overhead
607:         // =========================================================================
608: 
609:                                     template<typename T>
610:         [[nodiscard]] const T&      get(size_t index) const;                                // Scalar runtime access. Throws on type/index mismatch.
611:         
612:                                     template<typename T, size_t Extent = std::dynamic_extent> 
613:         void                        get(size_t index, std::span<T, Extent> &dst) const;     // Strict: vectorized runtime access. Throws on type/index mismatch.
614: 
615:                                     template<typename T>
616:         [[nodiscard]] bool          get(size_t index, T &dst) const noexcept;               // Flexible: allows type conversions, returns false on failure.
617:         
618:                                     template<typename T>
619:         [[nodiscard]] T&            ref(size_t index);                                      // get a mutable reference to the internal data (no type conversion, may throw)
620: 
621:                                     template<size_t Index, typename T>
622:         void                        set(const T& value);                                    // scalar compile-time indexed access
623:                                     template<typename T>
624:         void                        set(size_t index, const T& value);                      // scalar runtime indexed access
625:                                     template<size_t StartIndex, typename T, size_t Extent>
626:         void                        set(std::span<const T, Extent> values);                 // vectorized compile-time indexed access
627:                                     template<typename T, size_t Extent = std::dynamic_extent>
628:         void                        set(size_t index, std::span<const T, Extent> values);   // vectorized runtime indexed access
629: 
630:         /** @brief Visit columns with read-only access (compile-time). @see row.hpp */
631:                                     template<RowVisitorConst Visitor>
632:         void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;
633: 
634:         /** @brief Visit all columns with read-only access (compile-time). @see row.hpp */
635:                                     template<RowVisitorConst Visitor>
636:         void                        visitConst(Visitor&& visitor) const;
637:         
638:         /** @brief Visit columns with mutable access (compile-time). @see row.hpp */
639:                                     template<RowVisitor Visitor>
640:         void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);
641: 
642:         /** @brief Visit all columns with mutable access (compile-time). @see row.hpp */
643:                                     template<RowVisitor Visitor>
644:         void                        visit(Visitor&& visitor);
645: 
646:         RowStatic& operator=(const RowStatic& other) = default;          // default copy assignment, tuple manages copying its members including strings (may throw on string alloc)
647:         RowStatic& operator=(RowStatic&& other) noexcept = default;      // default move assignment, tuple manages moving its members including strings
648:     };
649: 
650:     // ────────────────────────────────────────────────────────────────────────
651:     // Stream operator for Row — prints column values in compact CSV-like format
652:     // ────────────────────────────────────────────────────────────────────────
653: 
654:     /**
655:      * @brief Stream insertion operator for Row (dynamic layout).
656:      *
657:      * Prints column values in a compact, comma-separated format suitable for
658:      * debugging and logging.  Bools print as "true"/"false", strings are
659:      * double-quoted with RFC 4180 escaping (embedded quotes doubled), integers
660:      * and floats use std::to_chars for locale-independent formatting.
661:      *
662:      * Example output:  42, 3.14, true, "hello world"
663:      */
664:     std::ostream& operator<<(std::ostream& os, const Row& row);
665: 
666:     /**
667:      * @brief Stream insertion operator for RowStatic (compile-time layout).
668:      *
669:      * Same compact, comma-separated format as the dynamic Row operator<<.
670:      * Uses compile-time type dispatch via fold expression for zero runtime
671:      * type switching.
672:      *
673:      * Example output:  42, 3.14, true, "hello world"
674:      */
675:     template<typename... ColumnTypes>
676:     std::ostream& operator<<(std::ostream& os, const RowStatic<ColumnTypes...>& row);
677: 
678: } // namespace bcsv

(End of file - total 678 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/row_visitors.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/row_visitors.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: /**
11:  * @file row_visitors.h
12:  * @brief Concepts and helpers for BCSV row visitor pattern
13:  * 
14:  * This header provides:
15:  * - C++20 concepts defining row visitor requirements
16:  * - Helper types and examples for common row visitor patterns
17:  * 
18:  * @section visitor_examples Visitor Pattern Examples
19:  * 
20:  * @subsection basic_visitors Basic Visitors
21:  * 
22:  * Read-only visitor (2 parameters):
23:  * @code
24:  * row.visit([](size_t index, const auto& value) {
25:  *     std::cout << "Column " << index << " = " << value << "\n";
26:  * });
27:  * @endcode
28:  * 
29:  * Mutable visitor with fine-grained change tracking (3 parameters):
30:  * @code
31:  * row.visit([](size_t index, auto& value, bool& changed) {
32:  *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
33:  *         value *= 2;
34:  *         changed = true;  // Mark this column as modified
35:  *     } else {
36:  *         changed = false; // Don't mark strings as changed
37:  *     }
38:  * });
39:  * @endcode
40:  * 
41:  * Legacy mutable visitor (2 parameters, all columns marked changed):
42:  * @code
43:  * row.visit([](size_t index, auto& value) {
44:  *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
45:  *         value *= 2;
46:  *     }
47:  * }); // All visited columns automatically marked as changed
48:  * @endcode
49:  * 
50:  * @subsection typed_visitors Type-Specific Visitors
51:  * 
52:  * Handle only specific types:
53:  * @code
54:  * row.visit([](size_t index, const auto& value) {
55:  *     using T = std::decay_t<decltype(value)>;
56:  *     if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
57:  *         double d = static_cast<double>(value);
58:  *         // Process numeric value...
59:  *     } else if constexpr (std::is_same_v<T, std::string>) {
60:  *         // Process string...
61:  *     }
62:  *     // Ignore bool or other types
63:  * });
64:  * @endcode
65:  * 
66:  * @subsection compile_time_typed_visitors Compile-Time Typed Visitors (visit<T>)
67:  * 
68:  * When columns share a known type, visit<T>() eliminates the runtime type switch:
69:  * @code
70:  * // 2x faster than visit() for homogeneous column ranges
71:  * row.visit<double>(0, [](size_t col, double& v, bool& changed) {
72:  *     v *= 2.0;
73:  *     changed = true;
74:  * }, 100);  // Process 100 consecutive double columns
75:  * 
76:  * // Read-only variant
77:  * double sum = 0;
78:  * row.visitConst<int32_t>(0, [&](size_t, const int32_t& v) {
79:  *     sum += v;
80:  * }, 50);
81:  * @endcode
82:  *
83:  * visit<T>() and visitConst<T>() are available on Row and RowStatic.
84:  * 
85:  * @subsection helper_visitors Using Helper Types
86:  * 
87:  * Use overload helper for different types:
88:  * @code
89:  * using bcsv::visitors::overload;
90:  * 
91:  * row.visit(overload{
92:  *     [](size_t, const std::string& s) { std::cout << "String: " << s << "\n"; },
93:  *     [](size_t, int32_t i) { std::cout << "Int: " << i << "\n"; },
94:  *     [](size_t, auto) { } // Catch-all for other types
95:  * });
96:  * @endcode
97:  */
98: 
99: #pragma once
100: 
101: #include <algorithm>
102: #include <cstddef>
103: #include <concepts>
104: #include <limits>
105: #include <ostream>
106: #include <string>
107: #include <type_traits>
108: 
109: namespace bcsv {
110: 
111: // ============================================================================
112: // Row Visitor Concepts (C++20)
113: // ============================================================================
114: 
115: /**
116:  * @brief Concept for read-only row visitors that accept (index, const value&)
117:  * 
118:  * A RowReadOnlyVisitor must be invocable with:
119:  * - size_t index (column index)
120:  * - const T& value (column value of any supported type)
121:  * 
122:  * @tparam Visitor The visitor callable type
123:  * @tparam T The column value type
124:  */
125: template<typename Visitor, typename T>
126: concept RowReadOnlyVisitor = std::invocable<Visitor, size_t, const T&>;
127: 
128: /**
129:  * @brief Concept for mutable row visitors that accept (index, value&)
130:  * 
131:  * A RowMutableVisitor must be invocable with:
132:  * - size_t index (column index)
133:  * - T& value (mutable reference to column value)
134:  * 
135:  * @tparam Visitor The visitor callable type
136:  * @tparam T The column value type
137:  */
138: template<typename Visitor, typename T>
139: concept RowMutableVisitor = std::invocable<Visitor, size_t, T&>;
140: 
141: /**
142:  * @brief Concept for compile-time visitors used with RowStatic
143:  * 
144:  * A RowStaticReadOnlyVisitor must be invocable with:
145:  * - std::integral_constant<size_t, Index> (compile-time index) OR size_t
146:  * - const T& or T& value (depending on const-ness)
147:  * 
148:  * @tparam Visitor The visitor callable type
149:  * @tparam Index The column index as a compile-time constant
150:  * @tparam T The column value type
151:  */
152: template<typename Visitor, size_t Index, typename T>
153: concept RowStaticReadOnlyVisitor = 
154:     std::invocable<Visitor, std::integral_constant<size_t, Index>, const T&> ||
155:     std::invocable<Visitor, size_t, const T&>;
156: 
157: // ============================================================================
158: // Typed Row Visitor Concepts  (C++20)
159: // ============================================================================
160: 
161: /**
162:  * @brief Concept for typed mutable visitors used with visit<T>()
163:  * 
164:  * A TypedRowVisitor must be invocable with a SPECIFIC type T:
165:  * - (size_t index, T& value, bool& changed)   — with change tracking control
166:  * - (size_t index, T& value)                   — all visited columns marked changed
167:  * 
168:  * Unlike the generic visit() which dispatches to the visitor with auto& (each
169:  * column's actual type), visit<T>() calls the visitor with a concrete T& for
170:  * all columns in the range. This enables:
171:  * - Compile-time dispatch (no runtime ColumnType switch) → ~2x faster
172:  * - Concrete lambda signatures (no if-constexpr chains needed)
173:  * - Type-safe bulk operations on homogeneous column ranges
174:  * 
175:  * @note Defined in row.h since it is required by Row's visit<T>() declaration.
176:  *       Documented here for reference.
177:  * 
178:  * @code
179:  * // Scale 100 consecutive double columns by 2x
180:  * row.visit<double>(0, [](size_t, double& v, bool& changed) {
181:  *     v *= 2.0;
182:  *     changed = true;
183:  * }, 100);
184:  * @endcode
185:  * 
186:  * @tparam V The visitor callable type
187:  * @tparam T The expected column type (int32_t, double, std::string, bool, etc.)
188:  * 
189:  * @see row.h for concept definition
190:  * @see TypedRowVisitorConst for read-only variant
191:  */
192: // concept TypedRowVisitor — defined in row.h
193: 
194: /**
195:  * @brief Concept for typed read-only visitors used with visitConst<T>()
196:  * 
197:  * A TypedRowVisitorConst must be invocable with:
198:  * - (size_t index, const T& value)
199:  * 
200:  * Read-only counterpart of TypedRowVisitor. Zero-copy for scalars and strings.
201:  * 
202:  * @code
203:  * double sum = 0;
204:  * row.visitConst<double>(10, [&](size_t, const double& v) {
205:  *     sum += v;
206:  * }, 50);  // Sum columns 10..59
207:  * @endcode
208:  * 
209:  * @tparam V The visitor callable type
210:  * @tparam T The expected column type
211:  * 
212:  * @see row.h for concept definition
213:  * @see TypedRowVisitor for mutable variant
214:  */
215: // concept TypedRowVisitorConst — defined in row.h
216: 
217: // ============================================================================
218: // Visitor Helper Types
219: // ============================================================================
220: 
221: namespace visitors {
222: 
223: /**
224:  * @brief Overload pattern helper for variant-like visitation
225:  * 
226:  * Allows combining multiple lambdas to handle different types:
227:  * 
228:  * @code
229:  * row.visit(overload{
230:  *     [](size_t, const std::string& s) { std::cout << s; },
231:  *     [](size_t, int32_t i) { std::cout << i; },
232:  *     [](size_t, auto) { } // Catch-all for other types
233:  * });
234:  * @endcode
235:  * 
236:  * This is the same pattern as std::visit helper from cppreference.
237:  */
238: template<class... Ts>
239: struct overload : Ts... { 
240:     using Ts::operator()...; 
241: };
242: 
243: // Deduction guide for C++17 compatibility
244: template<class... Ts>
245: overload(Ts...) -> overload<Ts...>;
246: 
247: /**
248:  * @brief CSV serialization visitor example
249:  * 
250:  * Usage:
251:  * @code
252:  * std::ostringstream csv;
253:  * row.visit(csv_visitor{csv});
254:  * std::cout << csv.str(); // "value1,value2,value3"
255:  * @endcode
256:  */
257: struct csv_visitor {
258:     std::ostream& out;
259:     bool first = true;
260:     
261:     void operator()(size_t, const auto& value) {
262:         if (!first) out << ",";
263:         first = false;
264:         
265:         using T = std::decay_t<decltype(value)>;
266:         if constexpr (std::is_same_v<T, bool>) {
267:             out << (value ? "true" : "false");
268:         } else if constexpr (std::is_same_v<T, std::string>) {
269:             // Escape quotes for CSV format
270:             out << '"';
271:             for (char c : value) {
272:                 if (c == '"') out << "\"\"";
273:                 else out << c;
274:             }
275:             out << '"';
276:         } else {
277:             out << value;
278:         }
279:     }
280: };
281: 
282: /**
283:  * @brief Statistics accumulator visitor example
284:  * 
285:  * Computes min/max/sum/count for numeric columns:
286:  * 
287:  * @code
288:  * stats_visitor stats;
289:  * row.visit(stats);
290:  * std::cout << "Min: " << stats.min << ", Max: " << stats.max << "\n";
291:  * @endcode
292:  */
293: struct stats_visitor {
294:     double min = std::numeric_limits<double>::max();
295:     double max = std::numeric_limits<double>::lowest();
296:     double sum = 0.0;
297:     size_t count = 0;
298:     
299:     void operator()(size_t, const auto& value) {
300:         using T = std::decay_t<decltype(value)>;
301:         if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
302:             double d = static_cast<double>(value);
303:             min = std::min(min, d);
304:             max = std::max(max, d);
305:             sum += d;
306:             count++;
307:         }
308:     }
309:     
310:     double mean() const { return count > 0 ? sum / count : 0.0; }
311: };
312: 
313: /**
314:  * @brief Type filter visitor that only processes specific types
315:  * 
316:  * @code
317:  * // Only process strings
318:  * row.visit(type_filter<std::string>([](size_t index, const std::string& s) {
319:  *     std::cout << "String at " << index << ": " << s << "\n";
320:  * }));
321:  * @endcode
322:  */
323: template<typename TargetType, typename Func>
324: struct type_filter {
325:     Func func;
326:     
327:     void operator()(size_t index, const auto& value) {
328:         using T = std::decay_t<decltype(value)>;
329:         if constexpr (std::is_same_v<T, TargetType>) {
330:             func(index, value);
331:         }
332:     }
333: };
334: 
335: /**
336:  * @brief Conditional visitor that applies predicate before processing
337:  * 
338:  * @code
339:  * // Only process non-empty strings
340:  * row.visit(conditional(
341:  *     [](const auto& v) { return !v.empty(); },
342:  *     [](size_t, const std::string& s) { std::cout << s << "\n"; }
343:  * ));
344:  * @endcode
345:  */
346: template<typename Predicate, typename Action>
347: struct conditional {
348:     Predicate pred;
349:     Action action;
350:     
351:     void operator()(size_t index, const auto& value) {
352:         if (pred(value)) {
353:             action(index, value);
354:         }
355:     }
356: };
357: 
358: } // namespace visitors
359: } // namespace bcsv

(End of file - total 359 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/src/tools/cli_common.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/src/tools/cli_common.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: /**
13:  * @file cli_common.h
14:  * @brief Shared utilities for BCSV CLI tools
15:  *
16:  * Provides standardised helpers used across all CLI tools:
17:  *   - columnTypeStr()           — ColumnType → human-readable string
18:  *   - formatBytes()             — byte count → "1.23 MB" / "456 KB" / "789 bytes"
19:  *   - encodingDescription()     — row/file codec + level → summary string
20:  *   - printLayoutSummary()      — tabular layout dump to any ostream
21:  *   - parseFileCodecFlags()     — "--file-codec" string → FileFlags + compression level
22:  *   - validateRowCodec()        — "--row-codec" string validation
23:  *   - VALID_FILE_CODECS / VALID_ROW_CODECS — canonical string lists
24:  *
25:  * Tools opt-in to specific helpers via ordinary #include.
26:  */
27: 
28: #include <cstddef>
29: #include <cstdint>
30: #include <iomanip>
31: #include <iostream>
32: #include <map>
33: #include <sstream>
34: #include <stdexcept>
35: #include <string>
36: #include <bcsv/bcsv.h>
37: 
38: namespace bcsv_cli {
39: 
40: // ── Canonical codec string constants ───────────────────────────────
41: 
42: inline constexpr const char* VALID_FILE_CODECS[] = {
43:     "packet_lz4_batch", "packet_lz4", "packet", "stream_lz4", "stream"
44: };
45: 
46: inline constexpr const char* VALID_ROW_CODECS[] = {
47:     "delta", "zoh", "flat"
48: };
49: 
50: inline constexpr const char* DEFAULT_FILE_CODEC = "packet_lz4_batch";
51: inline constexpr const char* DEFAULT_ROW_CODEC  = "delta";
52: 
53: // ── columnTypeStr ──────────────────────────────────────────────────
54: 
55: /// Convert ColumnType to human-readable string.
56: inline std::string columnTypeStr(bcsv::ColumnType type) {
57:     switch (type) {
58:         case bcsv::ColumnType::BOOL:   return "bool";
59:         case bcsv::ColumnType::INT8:   return "int8";
60:         case bcsv::ColumnType::UINT8:  return "uint8";
61:         case bcsv::ColumnType::INT16:  return "int16";
62:         case bcsv::ColumnType::UINT16: return "uint16";
63:         case bcsv::ColumnType::INT32:  return "int32";
64:         case bcsv::ColumnType::UINT32: return "uint32";
65:         case bcsv::ColumnType::INT64:  return "int64";
66:         case bcsv::ColumnType::UINT64: return "uint64";
67:         case bcsv::ColumnType::FLOAT:  return "float";
68:         case bcsv::ColumnType::DOUBLE: return "double";
69:         case bcsv::ColumnType::STRING: return "string";
70:         default:                       return "unknown";
71:     }
72: }
73: 
74: // ── formatBytes ────────────────────────────────────────────────────
75: 
76: /// Format a byte count as human-readable string.
77: inline std::string formatBytes(uintmax_t bytes) {
78:     std::ostringstream oss;
79:     if (bytes >= 1024 * 1024) {
80:         oss << std::fixed << std::setprecision(2)
81:             << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
82:     } else if (bytes >= 1024) {
83:         oss << std::fixed << std::setprecision(2)
84:             << (static_cast<double>(bytes) / 1024.0) << " KB";
85:     } else {
86:         oss << bytes << " bytes";
87:     }
88:     return oss.str();
89: }
90: 
91: // ── printLayoutSummary ─────────────────────────────────────────────
92: 
93: /// Print vertical layout table: type histogram + full column listing.
94: inline void printLayoutSummary(const std::string& label,
95:                                const bcsv::Layout& layout,
96:                                std::ostream& os = std::cerr) {
97:     const size_t n = layout.columnCount();
98:     if (n == 0) {
99:         os << label << ": (empty)\n";
100:         return;
101:     }
102: 
103:     // Build type histogram
104:     std::map<std::string, size_t> type_counts;
105:     size_t max_name_len = 4;   // minimum width for "Name" header
106:     size_t max_type_len = 4;   // minimum width for "Type" header
107:     for (size_t i = 0; i < n; ++i) {
108:         const std::string name = layout.columnName(i);
109:         const std::string type = columnTypeStr(layout.columnType(i));
110:         type_counts[type]++;
111:         if (name.size() > max_name_len) max_name_len = name.size();
112:         if (type.size() > max_type_len) max_type_len = type.size();
113:     }
114: 
115:     // Type histogram line
116:     os << label << " (" << n << " columns)  [ ";
117:     bool first = true;
118:     for (const auto& [tname, cnt] : type_counts) {
119:         if (!first) os << ", ";
120:         os << cnt << "x" << tname;
121:         first = false;
122:     }
123:     os << " ]\n";
124: 
125:     // Column index width
126:     size_t idx_width = 1;
127:     for (size_t v = n - 1; v >= 10; v /= 10) ++idx_width;
128:     if (idx_width < 3) idx_width = 3;
129: 
130:     // Table header
131:     os << "  " << std::right << std::setw(static_cast<int>(idx_width)) << "Idx"
132:        << "  " << std::left  << std::setw(static_cast<int>(max_name_len)) << "Name"
133:        << "  " << std::left  << std::setw(static_cast<int>(max_type_len)) << "Type"
134:        << "\n";
135: 
136:     // Separator
137:     os << "  " << std::string(idx_width, '-')
138:        << "  " << std::string(max_name_len, '-')
139:        << "  " << std::string(max_type_len, '-')
140:        << "\n";
141: 
142:     // Column rows
143:     for (size_t i = 0; i < n; ++i) {
144:         os << "  " << std::right << std::setw(static_cast<int>(idx_width)) << i
145:            << "  " << std::left  << std::setw(static_cast<int>(max_name_len)) << layout.columnName(i)
146:            << "  " << std::left  << std::setw(static_cast<int>(max_type_len)) << columnTypeStr(layout.columnType(i))
147:            << "\n";
148:     }
149: }
150: 
151: // ── Codec validation / mapping ────────────────────────────────────
152: 
153: /// Validate a --row-codec string.  Throws std::runtime_error on invalid value.
154: inline void validateRowCodec(const std::string& codec) {
155:     if (codec != "delta" && codec != "zoh" && codec != "flat") {
156:         throw std::runtime_error(
157:             "Unknown row codec '" + codec +
158:             "'. Expected: delta, zoh, flat.");
159:     }
160: }
161: 
162: /// Validate a --file-codec string.  Throws std::runtime_error on invalid value.
163: inline void validateFileCodec(const std::string& codec) {
164:     if (codec != "packet_lz4_batch" &&
165:         codec != "packet_lz4" &&
166:         codec != "packet" &&
167:         codec != "stream_lz4" &&
168:         codec != "stream") {
169:         throw std::runtime_error(
170:             "Unknown file codec '" + codec +
171:             "'. Expected: packet_lz4_batch, packet_lz4, packet, stream_lz4, stream.");
172:     }
173: }
174: 
175: /// Result of parsing file-codec settings into FileFlags + compression level.
176: struct FileCodecSettings {
177:     bcsv::FileFlags flags       = bcsv::FileFlags::NONE;
178:     size_t          comp_level  = 1;
179: };
180: 
181: /// Map (file_codec string, row_codec string, user compression_level) → FileFlags + effective comp_level.
182: inline FileCodecSettings resolveCodecFlags(const std::string& file_codec,
183:                                            const std::string& row_codec,
184:                                            size_t compression_level) {
185:     FileCodecSettings s;
186: 
187:     const bool has_lz4   = (file_codec == "packet_lz4_batch" ||
188:                             file_codec == "packet_lz4" ||
189:                             file_codec == "stream_lz4");
190:     const bool has_batch = (file_codec == "packet_lz4_batch");
191:     const bool is_stream = (file_codec == "stream_lz4" ||
192:                             file_codec == "stream");
193: 
194:     if (has_batch) {
195: #ifdef BCSV_HAS_BATCH_CODEC
196:         s.flags = s.flags | bcsv::FileFlags::BATCH_COMPRESS;
197: #else
198:         throw std::runtime_error(
199:             "Batch codec not available (BCSV_ENABLE_BATCH_CODEC=OFF). "
200:             "Use --file-codec packet_lz4 instead.");
201: #endif
202:     }
203:     if (is_stream)
204:         s.flags = s.flags | bcsv::FileFlags::STREAM_MODE;
205: 
206:     // Row codec flags — note:  Writer<…, RowCodecZoH001> and WriterDelta
207:     // inject their own flag via RowCodecFileFlags, so the ZoH flag set here
208:     // is technically redundant but harmless (ORed).
209:     if (row_codec == "zoh")
210:         s.flags = s.flags | bcsv::FileFlags::ZERO_ORDER_HOLD;
211: 
212:     s.comp_level = has_lz4 ? compression_level : 0;
213:     return s;
214: }
215: 
216: /// Build a short encoding description string for summary output.
217: inline std::string encodingDescription(const std::string& row_codec,
218:                                        const std::string& file_codec,
219:                                        size_t compression_level) {
220:     return row_codec + " + " + file_codec
221:          + " (level " + std::to_string(compression_level) + ")";
222: }
223: 
224: /// Reverse-map FileFlags + compression level back to human-readable codec names.
225: struct CodecNames {
226:     std::string row_codec;
227:     std::string file_codec;
228: };
229: 
230: inline CodecNames codecNamesFromFlags(bcsv::FileFlags flags, uint8_t comp_level) {
231:     CodecNames names;
232: 
233:     // Row codec
234:     const bool has_delta = (flags & bcsv::FileFlags::DELTA_ENCODING) != bcsv::FileFlags::NONE;
235:     const bool has_zoh   = (flags & bcsv::FileFlags::ZERO_ORDER_HOLD) != bcsv::FileFlags::NONE;
236:     if (has_delta)
237:         names.row_codec = "delta";
238:     else if (has_zoh)
239:         names.row_codec = "zoh";
240:     else
241:         names.row_codec = "flat";
242: 
243:     // File codec — mirrors resolveFileCodecId() logic
244:     const bool stream    = (flags & bcsv::FileFlags::STREAM_MODE) != bcsv::FileFlags::NONE;
245:     const bool batch     = (flags & bcsv::FileFlags::BATCH_COMPRESS) != bcsv::FileFlags::NONE;
246:     const bool compressed = comp_level > 0;
247: 
248:     if (stream) {
249:         names.file_codec = compressed ? "stream_lz4" : "stream";
250:     } else if (batch && compressed) {
251:         names.file_codec = "packet_lz4_batch";
252:     } else {
253:         names.file_codec = compressed ? "packet_lz4" : "packet";
254:     }
255: 
256:     return names;
257: }
258: 
259: // ── Writer dispatch helper ────────────────────────────────────────
260: 
261: /// Open the appropriate Writer type (delta/zoh/flat) based on row_codec
262: /// string and call `action(writer)`.  The caller provides a generic
263: /// lambda that works with any Writer type (via auto&).
264: ///
265: /// Usage:
266: ///     bcsv_cli::withWriter(layout, row_codec, [&](auto& writer) {
267: ///         writer.open(path, overwrite, comp_level, block_kb, flags);
268: ///         // … write loop …
269: ///         writer.close();
270: ///     });
271: template<typename LayoutType, typename Action>
272: void withWriter(const LayoutType& layout,
273:                 const std::string& row_codec,
274:                 Action&& action) {
275:     if (row_codec == "delta") {
276:         bcsv::WriterDelta<LayoutType> writer(layout);
277:         action(writer);
278:     } else if (row_codec == "zoh") {
279:         bcsv::WriterZoH<LayoutType> writer(layout);
280:         action(writer);
281:     } else {
282:         bcsv::WriterFlat<LayoutType> writer(layout);
283:         action(writer);
284:     }
285: }
286: 
287: } // namespace bcsv_cli
288: 

(End of file - total 288 lines)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 14.1s)

_Thinking:_

Now let me look at a few more key files to understand the reader, writer, and how existing tools work.




Now let me look at the Reader/Writer APIs and a representative CLI tool for patterns:





**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/reader.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/reader.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: #include <cstddef>
13: #include <cstdint>
14: #include <fstream>
15: #include <filesystem>
16: #include <string>
17: #include <vector>
18: #include "layout.h"
19: #include "row.h"
20: #include "codec_row/row_codec_dispatch.h"
21: #include "file_header.h"
22: #include "codec_file/file_codec_dispatch.h"
23: #include "file_footer.h"
24: 
25: namespace bcsv {
26: 
27:     /**
28:      * @brief Class for reading BCSV binary files
29:      */
30:     template<LayoutConcept LayoutType>
31:     class Reader {
32:     public:
33:         using RowType           = typename LayoutType::RowType;
34: 
35:     protected:
36:         using FilePath          = std::filesystem::path;
37:         using RowCodeDisptch    = RowCodecDispatch<LayoutType>;
38: 
39:         std::string             err_msg_;                // last error message description
40: 
41:         FileHeader              file_header_;            // file header for accessing flags and metadata
42:         FilePath                file_path_;              // points to the input file
43:         std::ifstream           stream_;                // input file binary stream
44: 
45:         // File-level codec (framing, decompression, checksums, packet lifecycle)
46:         FileCodecDispatch       file_codec_;             // Runtime-selected file codec
47: 
48:         // Global row tracking
49:         RowCodeDisptch          row_codec_;      // Runtime codec dispatch (Item 11 Phase 7)
50:         size_t                  row_pos_;                // postion of current row in file (0-based row counter)
51:         RowType                 row_;                   // current row, decoded data
52: 
53:         
54:     public:
55:                                 Reader();
56:                                 ~Reader();
57: 
58:         void                    close();
59:         uint8_t                 compressionLevel() const    { return file_header_.getCompressionLevel(); }
60:         FileFlags               fileFlags() const               { return file_header_.getFlags(); }
61:         const FileHeader&       fileHeader() const              { return file_header_; }
62:         const FilePath&         filePath() const            { return file_path_; }
63:         const LayoutType&       layout() const              { return row_.layout(); }
64:         const std::string&      getErrorMsg() const         { return err_msg_; }
65:         
66:         bool                    isOpen() const              { return stream_.is_open(); }
67:         bool                    open(const FilePath& filepath);
68:         bool                    readNext();
69:         const RowType&          row() const                 { return row_; }
70:         size_t                  rowPos() const              { return row_pos_; } // 0-based row index in file
71:         
72:     protected:
73:         bool                    readFileHeader();
74:     };
75: 
76:     /**
77:      * @brief Class for direct access reading of BCSV binary files
78:      *
79:      * Provides O(log P) random access to any row by index (P = number of packets).
80:      * Both compressed (LZ4) and uncompressed codecs cache the entire target
81:      * packet in memory.  Subsequent reads within the same packet are O(1)
82:      * vector-index lookups.  Cross-packet seeks load only the target packet.
83:      *
84:      * Optimized for piecewise-sequential access patterns (head, tail, slice).
85:      */
86:     template<LayoutConcept LayoutType>
87:     class ReaderDirectAccess : public Reader<LayoutType> {
88:     protected:
89:         using Base              = Reader<LayoutType>;
90:         using RowType           = typename LayoutType::RowType;
91:         using FilePath          = std::filesystem::path;
92:         using RowCodeDisptch    = RowCodecDispatch<LayoutType>;
93: 
94:         FileFooter  file_footer_;
95: 
96:         // ── Packet cache ────────────────────────────────────────────────
97:         // When a new packet is needed, the entire packet is read via the
98:         // file codec (which handles decompression transparently) into
99:         // cached_rows_.  Subsequent reads within the same packet are
100:         // O(1) vector-index lookups.
101: 
102:         size_t                          cached_packet_idx_{SIZE_MAX};  ///< Index into PacketIndex of cached packet (SIZE_MAX = none)
103:         size_t                          cached_first_row_{0};          ///< first_row of the cached packet
104:         size_t                          cached_row_count_{0};          ///< Number of rows in the cached packet
105:         std::vector<std::vector<std::byte>> cached_rows_;              ///< Raw row data per row
106: 
107:         // Row codec for direct-access deserialization (separate from sequential)
108:         RowCodeDisptch                  da_row_codec_;
109: 
110:         // ── Watermark decode position (LIB-1 fix) ───────────────────────
111:         // For stateful codecs (ZoH/Delta), tracks the last row within the
112:         // cached packet that has been fed through the codec sequentially.
113:         // Forward reads continue from the watermark; backward reads reset.
114:         size_t                          cached_decode_pos_{SIZE_MAX};  ///< Last decoded row index within packet (SIZE_MAX = none)
115: 
116:         // Tracks the row_pos_ value set by the last successful read().
117:         // If Base::row_pos_ differs, readNext() was called in between
118:         // and the watermark is no longer valid (row_ was overwritten).
119:         size_t                          da_row_pos_{SIZE_MAX};
120: 
121:     public:
122:         void    close();
123:         bool    open(const FilePath& filepath, bool rebuildFooter = false);
124:         bool    read(size_t index);
125:         size_t  rowCount()const             { return file_footer_.rowCount(); }
126:         const FileFooter& fileFooter()const { return file_footer_; }
127: 
128:     protected:
129:         void    buildFileFooter();
130:         bool    loadPacket(size_t packetIdx);
131:         bool    deserializeCachedRow(size_t rowInPacket, size_t index);
132:     };
133: 
134:     
135: 
136: } // namespace bcsv
137: 
138:     

(End of file - total 138 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/writer.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/writer.h</path>
<type>file</type>
<content>
1: #pragma once
2: 
3: /*
4:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
5:  * 
6:  * This file is part of the BCSV library.
7:  * 
8:  * Licensed under the MIT License. See LICENSE file in the project root 
9:  * for full license information.
10:  */
11: #include <cstddef>
12: #include <cstdint>
13: #include <fstream>
14: #include <filesystem>
15: #include <string>
16: 
17: #include "definitions.h"
18: #include "layout.h"
19: #include "row.h"
20: #include "codec_row/row_codec_flat001.h"
21: #include "codec_row/row_codec_zoh001.h"
22: #include "codec_row/row_codec_delta002.h"
23: #include "file_header.h"
24: #include "codec_file/file_codec_dispatch.h"
25: 
26: namespace bcsv {
27: 
28:     // ── Codec → FileFlags mapping ────────────────────────────────────────
29:     // Writer owns the contract between row-codec type and required file
30:     // header flags.  Codecs themselves are wire-format only and have no
31:     // knowledge of FileFlags — this keeps the layers cleanly separated.
32:     // Reader auto-detects the codec from the file header flags it reads.
33: 
34:     /// Default: no extra flags required (covers RowCodecFlat001 and any
35:     /// future codec that does not need a dedicated flag).
36:     template<typename CodecType>
37:     struct RowCodecFileFlags {
38:         static constexpr FileFlags value = FileFlags::NONE;
39:     };
40: 
41:     /// ZoH codec requires the ZERO_ORDER_HOLD flag in the file header.
42:     template<typename LayoutType>
43:     struct RowCodecFileFlags<RowCodecZoH001<LayoutType>> {
44:         static constexpr FileFlags value = FileFlags::ZERO_ORDER_HOLD;
45:     };
46: 
47:     /// Delta codec requires the DELTA_ENCODING flag in the file header.
48:     template<typename LayoutType>
49:     struct RowCodecFileFlags<RowCodecDelta002<LayoutType>> {
50:         static constexpr FileFlags value = FileFlags::DELTA_ENCODING;
51:     };
52: 
53:     /**
54:      * @brief Class for writing BCSV binary files
55:      */
56:     template<
57:         LayoutConcept LayoutType,
58:         typename RowCodec = RowCodecDelta002<LayoutType>
59:     >
60:     class Writer {
61:     public:
62:         using RowType           = typename LayoutType::RowType;
63: 
64:     private:
65:         using FilePath          = std::filesystem::path;
66: 
67:         std::string             err_msg_;                    // last error message description
68:         FileHeader              file_header_;                // File header for accessing flags and metadata
69:         FilePath                file_path_;                  // Always present
70:         std::ofstream           stream_;                     // Always binary file stream
71: 
72:         // File-level codec (framing, compression, checksums, packet lifecycle)
73:         FileCodecDispatch       file_codec_;                 // Runtime-selected file codec
74: 
75:         RowCodec                row_codec_;                  // Compile-time selected row codec
76:         uint64_t                row_cnt_ = 0;                // Total rows written across all packets
77:         RowType                 row_;
78:         
79: 
80:     public:
81:         Writer() = delete;
82:         Writer(const LayoutType& layout);
83:         ~Writer();
84: 
85:         void                    close();
86:         void                    flush();
87:         uint8_t                 compressionLevel() const        { return file_header_.getCompressionLevel(); }
88:         const std::string&      getErrorMsg() const             { return err_msg_; }
89:         const FilePath&         filePath() const                { return file_path_; }
90:         const LayoutType&       layout() const                  { return row_.layout(); }
91:         bool                    isOpen() const                  { return stream_.is_open(); }
92:         bool                    open(const FilePath& filepath, bool overwrite = false, size_t compressionLevel = 1, size_t blockSizeKB = DEFAULT_PACKET_SIZE_KB, FileFlags flags = FileFlags::BATCH_COMPRESS | FileFlags::DELTA_ENCODING);
93:         RowType&                row()                           { return row_; }
94:         const RowType&          row() const                     { return row_; }
95:         size_t                  rowCount() const                { return row_cnt_; }
96:         void                    write(const RowType& row);
97:         void                    writeRow();
98: 
99:     };
100: 
101:     template<LayoutConcept LayoutType>
102:     using WriterFlat = Writer<LayoutType, RowCodecFlat001<LayoutType>>;
103: 
104:     template<LayoutConcept LayoutType>
105:     using WriterZoH = Writer<LayoutType, RowCodecZoH001<LayoutType>>;
106: 
107:     template<LayoutConcept LayoutType>
108:     using WriterDelta = Writer<LayoutType, RowCodecDelta002<LayoutType>>;
109: 
110: } // namespace bcsv

(End of file - total 110 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/src/tools/bcsvRepair.cpp"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/src/tools/bcsvRepair.cpp</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  *
4:  * This file is part of the BCSV library.
5:  *
6:  * Licensed under the MIT License. See LICENSE file in the project root
7:  * for full license information.
8:  */
9: 
10: /**
11:  * @file bcsvRepair.cpp
12:  * @brief CLI tool to repair BCSV files from interrupted write processes
13:  *
14:  * Recovers data from BCSV files where the writer was interrupted before
15:  * completing the write. Supports all five file codecs:
16:  *
17:  *   Packet-based (packet, packet_lz4, packet_lz4_batch):
18:  *     Walks packet-by-packet, rebuilds the packet index, attempts partial
19:  *     recovery of the last incomplete packet, and writes a valid footer.
20:  *
21:  *   Stream-based (stream, stream_lz4):
22:  *     Walks row-by-row using per-row XXH32 checksums, truncates at the
23:  *     last valid row. Stream files have no footer by design.
24:  *
25:  * Two output modes:
26:  *   Copy mode     (-o FILE)    Write repaired file to a new location
27:  *   In-place mode (--in-place) Truncate and append footer to the original file
28:  *
29:  * Note on LZ4 batch codec: The packet_lz4_batch codec wraps the entire packet
30:  * payload in a single LZ4 frame. If the frame is truncated, decompression fails
31:  * and zero rows can be recovered from that packet.
32:  */
33: 
34: #include <algorithm>
35: #include <cstdint>
36: #include <cstring>
37: #include <filesystem>
38: #include <fstream>
39: #include <iomanip>
40: #include <iostream>
41: #include <sstream>
42: #include <stdexcept>
43: #include <string>
44: #include <vector>
45: 
46: #include <bcsv/bcsv.h>
47: #include <bcsv/vle.hpp>
48: #include <bcsv/file_footer.h>
49: #include <bcsv/checksum.hpp>
50: #include "cli_common.h"
51: 
52: #ifdef __has_include
53: #  if __has_include(<unistd.h>)
54: #    include <unistd.h>
55: #    define BCSV_HAS_FTRUNCATE 1
56: #  endif
57: #endif
58: 
59: // ── Configuration ───────────────────────────────────────────────────
60: 
61: struct Config {
62:     std::string input_file;
63:     std::string output_file;                  // empty → no copy mode
64: 
65:     bool in_place   = false;
66:     bool backup     = false;
67:     bool deep       = false;                  // validate payload checksums
68:     bool dry_run    = false;
69:     bool json       = false;
70:     bool verbose    = false;
71:     bool help       = false;
72: };
73: 
74: // ── Repair result ───────────────────────────────────────────────────
75: 
76: struct RepairResult {
77:     size_t   packets_found      = 0;          // total valid packets discovered
78:     size_t   packets_discarded  = 0;          // packets with bad payload checksums or truncated
79:     uint64_t rows_recovered     = 0;          // total rows in valid packets (or valid stream rows)
80:     uint64_t rows_recovered_partial = 0;      // rows recovered from broken last packet
81:     uint64_t rows_discarded     = 0;          // rows lost (stream: from cut point; packet: in discarded packets)
82:     uint64_t file_size_original = 0;
83:     uint64_t bytes_trimmed      = 0;          // bytes removed (broken tail)
84:     uint64_t footer_size        = 0;          // bytes of new footer written
85:     bool     had_damage         = false;      // any damage detected
86:     bool     had_footer         = false;      // original file had a valid footer
87:     bool     is_stream_mode     = false;      // file uses stream codec
88:     std::string error;                        // fatal error message, empty on success
89:     std::vector<std::string> warnings;        // non-fatal issues
90: };
91: 
92: // ── Usage ───────────────────────────────────────────────────────────
93: 
94: static void printUsage(const char* prog) {
95:     std::cout
96:         << "Usage: " << prog
97:         << " -i INPUT [-o OUTPUT | --in-place] [OPTIONS]\n\n"
98: 
99:         << "Repair BCSV files from interrupted write processes.\n\n"
100:         << "Packet-mode files (packet, packet_lz4, packet_lz4_batch):\n"
101:         << "  Walks packet-by-packet, rebuilds the packet index, attempts partial\n"
102:         << "  recovery of the last incomplete packet, and writes a valid footer.\n\n"
103:         << "Stream-mode files (stream, stream_lz4):\n"
104:         << "  Walks row-by-row using per-row XXH32 checksums, truncates at the\n"
105:         << "  last valid row. Stream files have no footer by design.\n\n"
106: 
107:         << "Output (one required unless --dry-run):\n"
108:         << "  -o, --output FILE      Write repaired file to OUTPUT (copy mode)\n"
109:         << "  --in-place             Modify input file directly (truncate + append footer)\n\n"
110: 
111:         << "Options:\n"
112:         << "  -i, --input FILE       Input BCSV file (required)\n"
113:         << "  --backup               With --in-place: copy original to FILE.bak first\n"
114:         << "  --deep                 Validate packet payload checksums (slower, more thorough)\n"
115:         << "                         Auto-enabled for --dry-run\n"
116:         << "  --dry-run              Analyze only — report damage without modifying anything\n"
117:         << "  --json                 Machine-readable JSON output to stdout\n"
118:         << "  -v, --verbose          Print progress (packets scanned, rows counted)\n"
119:         << "  -h, --help             Show this help message\n\n"
120: 
121:         << "Exit codes:\n"
122:         << "  0  Repair successful (or file was already valid)\n"
123:         << "  1  Repair failed or file is not repairable\n"
124:         << "  2  Argument error\n\n"
125: 
126:         << "Notes:\n"
127:         << "  The packet_lz4_batch codec wraps entire packets in a single LZ4 frame;\n"
128:         << "  if the frame is truncated, zero rows can be recovered from that packet.\n"
129:         << "  Non-batch packet codecs attempt partial recovery of rows from the\n"
130:         << "  last broken packet (walks rows until first invalid VLE/data boundary).\n\n"
131: 
132:         << "Examples:\n"
133:         << "  " << prog << " -i broken.bcsv --dry-run\n"
134:         << "  " << prog << " -i broken.bcsv -o repaired.bcsv\n"
135:         << "  " << prog << " -i broken.bcsv --in-place --backup\n"
136:         << "  " << prog << " -i broken.bcsv -o repaired.bcsv --deep --verbose\n";
137: }
138: 
139: // ── Argument parsing ────────────────────────────────────────────────
140: 
141: static Config parseArgs(int argc, char* argv[]) {
142:     Config cfg;
143: 
144:     for (int i = 1; i < argc; ++i) {
145:         std::string arg = argv[i];
146: 
147:         if (arg == "-h" || arg == "--help") {
148:             cfg.help = true;
149:             return cfg;
150:         } else if (arg == "-v" || arg == "--verbose") {
151:             cfg.verbose = true;
152:         } else if (arg == "--json") {
153:             cfg.json = true;
154:         } else if (arg == "--deep") {
155:             cfg.deep = true;
156:         } else if (arg == "--dry-run") {
157:             cfg.dry_run = true;
158:         } else if (arg == "--in-place") {
159:             cfg.in_place = true;
160:         } else if (arg == "--backup") {
161:             cfg.backup = true;
162:         } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
163:             cfg.input_file = argv[++i];
164:         } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
165:             cfg.output_file = argv[++i];
166:         } else if (arg.starts_with("-")) {
167:             throw std::runtime_error("Unknown option: " + arg);
168:         } else {
169:             if (cfg.input_file.empty())
170:                 cfg.input_file = arg;
171:             else
172:                 throw std::runtime_error("Too many positional arguments.");
173:         }
174:     }
175: 
176:     if (cfg.help) return cfg;
177: 
178:     if (cfg.input_file.empty()) {
179:         throw std::runtime_error("Input file is required (-i FILE).");
180:     }
181: 
182:     if (!cfg.dry_run && cfg.output_file.empty() && !cfg.in_place) {
183:         throw std::runtime_error(
184:             "Output mode required: use -o FILE (copy) or --in-place, or --dry-run to analyze only.");
185:     }
186: 
187:     if (!cfg.output_file.empty() && cfg.in_place) {
188:         throw std::runtime_error("-o and --in-place are mutually exclusive.");
189:     }
190: 
191:     if (cfg.backup && !cfg.in_place) {
192:         throw std::runtime_error("--backup is only valid with --in-place.");
193:     }
194: 
195:     // Auto-enable deep validation for dry-run
196:     if (cfg.dry_run) {
197:         cfg.deep = true;
198:     }
199: 
200:     return cfg;
201: }
202: 
203: // ── JSON helpers ────────────────────────────────────────────────────
204: 
205: static std::string jsonStr(const std::string& s) {
206:     std::string out = "\"";
207:     for (char c : s) {
208:         switch (c) {
209:             case '"':  out += "\\\""; break;
210:             case '\\': out += "\\\\"; break;
211:             case '\n': out += "\\n";  break;
212:             case '\r': out += "\\r";  break;
213:             case '\t': out += "\\t";  break;
214:             default:   out += c;      break;
215:         }
216:     }
217:     out += '"';
218:     return out;
219: }
220: 
221: // ── Packet payload validation ───────────────────────────────────────
222: 
223: /// Information about a validated packet
224: struct PacketInfo {
225:     uint64_t byte_offset;       // file offset of packet header
226:     uint64_t first_row;         // first row index
227:     uint64_t end_offset;        // file offset just past the payload checksum (end of packet)
228:     uint64_t row_count;         // rows in this packet (only set for last packet or deep mode)
229:     bool     is_valid;          // payload checksum validated OK
230:     bool     is_last_scanned;   // was this the last packet we could fully scan?
231:     std::string error_detail;   // error info if invalid
232: };
233: 
234: /**
235:  * @brief Count rows in a packet by walking VLE-encoded row lengths.
236:  *
237:  * Reads the entire payload into memory in one bulk read, then walks
238:  * VLE-encoded row lengths in-memory until the PCKT_TERMINATOR is found
239:  * or the buffer is exhausted (truncated packet).
240:  *
241:  * @param is         Input stream positioned after the PacketHeader
242:  * @param isBatch    true if this is a batch-LZ4 packet (different payload structure)
243:  * @param[out] rowCount  Number of complete rows found
244:  * @param[out] endOffset File offset just past the packet's trailing checksum
245:  * @param[out] payloadValid true if the terminator was found (packet is complete)
246:  * @return true if at least the terminator was found (complete packet)
247:  */
248: static bool walkPacketPayload(std::ifstream& is, bool isBatch,
249:                                size_t& rowCount, uint64_t& endOffset,
250:                                bool& payloadValid) {
251:     rowCount = 0;
252:     payloadValid = false;
253: 
254:     if (isBatch) {
255:         // Batch LZ4: [uint32 uncompressed_size][uint32 compressed_size][compressed_data][uint64 checksum]
256:         uint32_t uncompressed_size = 0;
257:         uint32_t compressed_size = 0;
258:         is.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));
259:         if (!is || is.gcount() != sizeof(uncompressed_size)) return false;
260:         is.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
261:         if (!is || is.gcount() != sizeof(compressed_size)) return false;
262: 
263:         if (uncompressed_size > bcsv::MAX_PACKET_SIZE || compressed_size > bcsv::MAX_PACKET_SIZE) {
264:             return false;
265:         }
266: 
267:         // Skip compressed data + checksum
268:         is.seekg(static_cast<std::streamoff>(compressed_size), std::ios::cur);
269:         if (!is) return false;
270:         uint64_t checksum = 0;
271:         is.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
272:         if (!is || is.gcount() != sizeof(checksum)) return false;
273: 
274:         endOffset = static_cast<uint64_t>(is.tellg());
275:         payloadValid = true;
276:         return true;
277:     }
278: 
279:     // Non-batch packet: bulk-read payload, walk VLE in memory.
280:     // Read up to MAX_PACKET_SIZE (1 GB) — actual allocation bounded by remaining file data.
281:     uint64_t start_pos = static_cast<uint64_t>(is.tellg());
282:     constexpr size_t MAX_READ = bcsv::MAX_PACKET_SIZE;
283: 
284:     // Determine how much data is left in the file
285:     is.seekg(0, std::ios::end);
286:     uint64_t file_end = static_cast<uint64_t>(is.tellg());
287:     is.seekg(static_cast<std::streamoff>(start_pos));
288: 
289:     size_t to_read = static_cast<size_t>(std::min(static_cast<uint64_t>(MAX_READ), file_end - start_pos));
290:     std::vector<char> buf(to_read);
291:     is.read(buf.data(), static_cast<std::streamsize>(to_read));
292:     size_t got = static_cast<size_t>(is.gcount());
293:     if (got == 0) return false;
294: 
295:     // Walk VLE rows in the buffer
296:     size_t pos = 0;
297:     while (pos < got) {
298:         uint64_t rowLen = 0;
299:         size_t vle_bytes = 0;
300:         try {
301:             vle_bytes = bcsv::vleDecode<uint64_t, true>(
302:                 rowLen, reinterpret_cast<const std::byte*>(buf.data() + pos), got - pos);
303:         } catch (...) {
304:             return false;  // truncated VLE
305:         }
306: 
307:         if (rowLen == bcsv::PCKT_TERMINATOR) {
308:             pos += vle_bytes;
309:             // Trailing checksum (xxHash64)
310:             if (pos + sizeof(uint64_t) > got) return false;
311:             pos += sizeof(uint64_t);
312:             endOffset = start_pos + pos;
313:             payloadValid = true;
314:             return true;
315:         }
316: 
317:         if (rowLen == 0) {
318:             pos += vle_bytes;
319:             rowCount++;
320:         } else {
321:             if (rowLen > bcsv::MAX_ROW_LENGTH) return false;
322:             pos += vle_bytes + static_cast<size_t>(rowLen);
323:             if (pos > got) return false;  // truncated row data
324:             rowCount++;
325:         }
326:     }
327:     return false;  // EOF before terminator
328: }
329: 
330: /**
331:  * @brief Deep-validate a packet's payload checksum (xxHash64).
332:  *
333:  * For non-batch packets: reads all VLE lengths and row data, feeds them
334:  * to a streaming xxHash64, then compares against the stored checksum.
335:  *
336:  * For batch packets: reads the compressed block, decompresses it, and
337:  * validates the stored checksum against xxHash64 of the uncompressed data.
338:  * Also counts rows by walking the decompressed payload.
339:  *
340:  * @param is        Input stream positioned just after the PacketHeader
341:  * @param isBatch   true if batch-LZ4 codec
342:  * @param[out] rowCount  Number of rows found (set on success)
343:  * @param[out] endOffset File offset after the packet's trailing checksum
344:  * @return true if checksum matches
345:  */
346: static bool deepValidatePacket(std::ifstream& is, bool isBatch,
347:                                 size_t& rowCount, uint64_t& endOffset) {
348:     rowCount = 0;
349: 
350:     if (isBatch) {
351:         // Read sizes
352:         uint32_t uncompressed_size = 0;
353:         uint32_t compressed_size = 0;
354:         is.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));
355:         if (!is || is.gcount() != sizeof(uncompressed_size)) return false;
356:         is.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
357:         if (!is || is.gcount() != sizeof(compressed_size)) return false;
358: 
359:         if (uncompressed_size > bcsv::MAX_PACKET_SIZE || compressed_size > bcsv::MAX_PACKET_SIZE) {
360:             return false;
361:         }
362: 
363:         // Read compressed block
364:         std::vector<char> compressed(compressed_size);
365:         is.read(compressed.data(), compressed_size);
366:         if (!is || is.gcount() != static_cast<std::streamsize>(compressed_size)) return false;
367: 
368:         // Read expected checksum
369:         uint64_t expected_checksum = 0;
370:         is.read(reinterpret_cast<char*>(&expected_checksum), sizeof(expected_checksum));
371:         if (!is || is.gcount() != sizeof(expected_checksum)) return false;
372:         endOffset = static_cast<uint64_t>(is.tellg());
373: 
374:         // Decompress
375:         std::vector<char> decompressed(uncompressed_size);
376:         int decompBytes = LZ4_decompress_safe(compressed.data(), decompressed.data(),
377:                                                static_cast<int>(compressed_size),
378:                                                static_cast<int>(uncompressed_size));
379:         if (decompBytes < 0 || static_cast<uint32_t>(decompBytes) != uncompressed_size) {
380:             return false;  // LZ4 decompression failed
381:         }
382: 
383:         // Validate checksum on uncompressed data
384:         uint64_t actual_checksum = XXH64(decompressed.data(), uncompressed_size, 0);
385:         if (actual_checksum != expected_checksum) return false;
386: 
387:         // Count rows in decompressed payload by walking VLE
388:         std::span<std::byte> buf(reinterpret_cast<std::byte*>(decompressed.data()), uncompressed_size);
389:         while (!buf.empty()) {
390:             uint64_t rowLen = 0;
391:             try {
392:                 size_t consumed = bcsv::vleDecode<uint64_t, true>(rowLen, buf.data(), buf.size());
393:                 buf = buf.subspan(consumed);
394:             } catch (...) {
395:                 break;
396:             }
397: 
398:             if (rowLen == bcsv::PCKT_TERMINATOR) break;
399:             if (rowLen == 0) {
400:                 rowCount++;
401:             } else {
402:                 if (rowLen > buf.size()) break;
403:                 buf = buf.subspan(static_cast<size_t>(rowLen));
404:                 rowCount++;
405:             }
406:         }
407:         return true;
408:     }
409: 
410:     // Non-batch: bulk-read payload into memory, walk VLE rows and compute hash
411:     uint64_t start_pos = static_cast<uint64_t>(is.tellg());
412: 
413:     // Determine remaining file data
414:     is.seekg(0, std::ios::end);
415:     uint64_t file_end = static_cast<uint64_t>(is.tellg());
416:     is.seekg(static_cast<std::streamoff>(start_pos));
417: 
418:     size_t to_read = static_cast<size_t>(
419:         std::min(static_cast<uint64_t>(bcsv::MAX_PACKET_SIZE), file_end - start_pos));
420:     std::vector<char> buf(to_read);
421:     is.read(buf.data(), static_cast<std::streamsize>(to_read));
422:     size_t got = static_cast<size_t>(is.gcount());
423:     if (got == 0) return false;
424: 
425:     // Walk VLE rows in buffer, computing streaming xxHash64
426:     bcsv::Checksum::Streaming hash;
427:     size_t pos = 0;
428:     while (pos < got) {
429:         const auto* ptr = reinterpret_cast<const std::byte*>(buf.data() + pos);
430:         size_t avail = got - pos;
431: 
432:         uint64_t rowLen = 0;
433:         size_t vle_bytes = 0;
434:         try {
435:             vle_bytes = bcsv::vleDecode<uint64_t, true>(rowLen, ptr, avail);
436:         } catch (...) {
437:             return false;
438:         }
439: 
440:         // Feed VLE bytes to hash
441:         hash.update(buf.data() + pos, vle_bytes);
442: 
443:         if (rowLen == bcsv::PCKT_TERMINATOR) {
444:             pos += vle_bytes;
445:             // Trailing checksum (xxHash64)
446:             if (pos + sizeof(uint64_t) > got) return false;
447:             uint64_t expected_checksum = 0;
448:             std::memcpy(&expected_checksum, buf.data() + pos, sizeof(expected_checksum));
449:             pos += sizeof(uint64_t);
450:             endOffset = start_pos + pos;
451:             return hash.finalize() == expected_checksum;
452:         }
453: 
454:         if (rowLen == 0) {
455:             pos += vle_bytes;
456:             rowCount++;
457:         } else {
458:             if (rowLen > bcsv::MAX_ROW_LENGTH) return false;
459:             size_t row_end = pos + vle_bytes + static_cast<size_t>(rowLen);
460:             if (row_end > got) return false;  // truncated
461:             // Feed row data to hash
462:             hash.update(buf.data() + pos + vle_bytes, static_cast<size_t>(rowLen));
463:             pos = row_end;
464:             rowCount++;
465:         }
466:     }
467:     return false;
468: }
469: 
470: // ── Stream row walking ──────────────────────────────────────────────
471: 
472: /**
473:  * @brief Walk a stream file row by row, verifying per-row XXH32 checksums.
474:  *
475:  * Wire format per row:  VLE(row_len) | row_bytes | uint32_t(XXH32)
476:  * ZoH repeat:           VLE(0)       (no payload, no checksum)
477:  *
478:  * Uses bulk I/O with a sliding buffer. Rows are validated in-memory;
479:  * individual read() calls only happen at buffer refill boundaries.
480:  *
481:  * @param is           Input stream positioned at the first row (right after FileHeader)
482:  * @param[out] validRows   Number of rows that passed validation
483:  * @param[out] cutOffset   File offset just past the last valid row
484:  * @param verbose      Print per-row progress
485:  * @return true always (cutOffset marks the last valid position)
486:  */
487: static bool walkStreamRows(std::ifstream& is, uint64_t& validRows,
488:                             uint64_t& cutOffset, bool verbose) {
489:     validRows = 0;
490:     cutOffset = static_cast<uint64_t>(is.tellg());
491: 
492:     // Initial 1 MB buffer; grows dynamically if a row exceeds capacity.
493:     // MAX_ROW_LENGTH is ~16 MB, so worst case we resize once to ~17 MB.
494:     size_t buf_cap = 1024 * 1024;
495:     std::vector<char> buffer(buf_cap);
496:     size_t buf_len = 0;
497:     size_t buf_pos = 0;
498:     uint64_t buf_file_offset = cutOffset;
499:     bool eof_reached = false;
500: 
501:     // Shift remaining data to front and read more from disk
502:     auto refill = [&]() {
503:         if (eof_reached) return;
504:         size_t remaining = buf_len - buf_pos;
505:         if (buf_pos > 0 && remaining > 0) {
506:             std::memmove(buffer.data(), buffer.data() + buf_pos, remaining);
507:         }
508:         buf_file_offset += buf_pos;
509:         buf_len = remaining;
510:         buf_pos = 0;
511:         size_t to_read = buf_cap - buf_len;
512:         if (to_read > 0) {
513:             is.read(buffer.data() + buf_len, static_cast<std::streamsize>(to_read));
514:             auto got = static_cast<size_t>(is.gcount());
515:             buf_len += got;
516:             if (got < to_read) eof_reached = true;
517:         }
518:     };
519: 
520:     // Grow buffer to fit at least `needed` bytes from buf_pos, then refill
521:     auto growAndRefill = [&](size_t needed) {
522:         if (needed <= buf_cap) return;
523:         buf_cap = needed + 1024 * 1024;  // add 1 MB headroom
524:         buffer.resize(buf_cap);
525:         refill();
526:     };
527: 
528:     refill();
529: 
530:     while (buf_pos < buf_len) {
531:         size_t avail = buf_len - buf_pos;
532:         const auto* ptr = reinterpret_cast<const std::byte*>(buffer.data() + buf_pos);
533: 
534:         // Decode VLE row length
535:         uint64_t rowLen = 0;
536:         size_t vle_bytes = 0;
537:         try {
538:             vle_bytes = bcsv::vleDecode<uint64_t, true>(rowLen, ptr, avail);
539:         } catch (...) {
540:             // Possibly truncated VLE at buffer boundary — try refill
541:             if (!eof_reached && avail < 10) {
542:                 refill();
543:                 avail = buf_len - buf_pos;
544:                 ptr = reinterpret_cast<const std::byte*>(buffer.data() + buf_pos);
545:                 try {
546:                     vle_bytes = bcsv::vleDecode<uint64_t, true>(rowLen, ptr, avail);
547:                 } catch (...) {
548:                     break;
549:                 }
550:             } else {
551:                 break;
552:             }
553:         }
554: 
555:         if (rowLen == 0) {
556:             // ZoH repeat — no payload, no checksum
557:             buf_pos += vle_bytes;
558:             cutOffset = buf_file_offset + buf_pos;
559:             validRows++;
560:             continue;
561:         }
562: 
563:         if (rowLen > bcsv::MAX_ROW_LENGTH) break;
564: 
565:         // Total bytes for this row: VLE + payload + XXH32
566:         size_t row_total = vle_bytes + static_cast<size_t>(rowLen) + sizeof(uint32_t);
567: 
568:         // Ensure the full row is in the buffer
569:         if (buf_pos + row_total > buf_len) {
570:             if (!eof_reached) {
571:                 growAndRefill(row_total);
572:                 if (buf_pos + row_total > buf_len) break;  // still truncated
573:             } else {
574:                 break;  // truncated
575:             }
576:         }
577: 
578:         // Validate XXH32 checksum
579:         const char* row_data = buffer.data() + buf_pos + vle_bytes;
580:         uint32_t expected_hash = 0;
581:         std::memcpy(&expected_hash, row_data + rowLen, sizeof(expected_hash));
582:         uint32_t actual_hash = bcsv::Checksum::compute32(row_data, static_cast<size_t>(rowLen));
583: 
584:         if (actual_hash != expected_hash) {
585:             if (verbose) {
586:                 std::cerr << "    Row " << validRows
587:                           << " @ offset " << (buf_file_offset + buf_pos)
588:                           << ": checksum mismatch\n";
589:             }
590:             break;
591:         }
592: 
593:         buf_pos += row_total;
594:         cutOffset = buf_file_offset + buf_pos;
595:         validRows++;
596: 
597:         if (verbose && (validRows % 10000 == 0)) {
598:             std::cerr << "    " << validRows << " rows validated ...\r";
599:         }
600: 
601:         // Proactive refill when buffer is running low (but only if buffer is large enough)
602:         if (buf_len - buf_pos < std::min<size_t>(65536, buf_cap / 16) && !eof_reached) {
603:             refill();
604:         }
605:     }
606: 
607:     if (verbose && validRows >= 10000) {
608:         std::cerr << "\n";
609:     }
610:     return true;
611: }
612: 
613: // walkStreamLZ4Rows is identical to walkStreamRows — the XXH32 checksum
614: // covers the compressed block in both cases, so no separate function needed.
615: 
616: // ── Partial packet recovery ─────────────────────────────────────────
617: 
618: /**
619:  * @brief Attempt to recover valid rows from a broken (last) packet.
620:  *
621:  * For non-batch packets only. Reads the remaining file into memory and
622:  * walks VLE-encoded rows in-buffer, checking plausibility and data
623:  * boundaries. Stops at the first incomplete row.
624:  *
625:  * @param is           Input stream positioned just after the PacketHeader
626:  * @param[out] rowCount    Number of complete rows recovered
627:  * @param[out] endOffset   File offset just past the last valid row
628:  * @return true if at least one row was recovered
629:  */
630: static bool recoverPartialPacket(std::ifstream& is, size_t& rowCount,
631:                                   uint64_t& endOffset) {
632:     rowCount = 0;
633:     uint64_t start_pos = static_cast<uint64_t>(is.tellg());
634:     endOffset = start_pos;
635: 
636:     // Read remaining file data (capped at MAX_PACKET_SIZE)
637:     is.seekg(0, std::ios::end);
638:     uint64_t file_end = static_cast<uint64_t>(is.tellg());
639:     is.seekg(static_cast<std::streamoff>(start_pos));
640: 
641:     constexpr size_t MAX_READ = bcsv::MAX_PACKET_SIZE;
642:     size_t to_read = static_cast<size_t>(std::min(static_cast<uint64_t>(MAX_READ), file_end - start_pos));
643:     if (to_read == 0) return false;
644: 
645:     std::vector<char> buf(to_read);
646:     is.read(buf.data(), static_cast<std::streamsize>(to_read));
647:     size_t got = static_cast<size_t>(is.gcount());
648:     if (got == 0) return false;
649: 
650:     // Walk VLE rows in the buffer
651:     size_t pos = 0;
652:     while (pos < got) {
653:         uint64_t rowLen = 0;
654:         size_t vle_bytes = 0;
655:         try {
656:             vle_bytes = bcsv::vleDecode<uint64_t, true>(
657:                 rowLen, reinterpret_cast<const std::byte*>(buf.data() + pos), got - pos);
658:         } catch (...) {
659:             break;  // Truncated VLE
660:         }
661: 
662:         if (rowLen == bcsv::PCKT_TERMINATOR) {
663:             // Packet was actually complete — read trailing checksum
664:             pos += vle_bytes;
665:             if (pos + sizeof(uint64_t) <= got) {
666:                 pos += sizeof(uint64_t);
667:                 endOffset = start_pos + pos;
668:             }
669:             return rowCount > 0;
670:         }
671: 
672:         if (rowLen == 0) {
673:             pos += vle_bytes;
674:             endOffset = start_pos + pos;
675:             rowCount++;
676:             continue;
677:         }
678: 
679:         if (rowLen > bcsv::MAX_ROW_LENGTH) break;
680: 
681:         // Check if row data fits
682:         size_t row_end = pos + vle_bytes + static_cast<size_t>(rowLen);
683:         if (row_end > got) break;  // Truncated row data
684: 
685:         pos = row_end;
686:         endOffset = start_pos + pos;
687:         rowCount++;
688:     }
689: 
690:     return rowCount > 0;
691: }
692: 
693: /**
694:  * @brief Compute the packet xxHash64 for the given byte range.
695:  *
696:  * Reads from startOffset to endOffset, feeding all bytes into a
697:  * streaming xxHash64. Used to produce a correct checksum when writing
698:  * a partial packet terminator.
699:  *
700:  * @param filepath      Path to the BCSV file
701:  * @param startOffset   First byte (right after PacketHeader)
702:  * @param endOffset     One past the last byte to hash
703:  * @param[out] vleTermBytes  VLE-encoded PCKT_TERMINATOR bytes (to append after hash)
704:  * @param[out] vleTermLen    Number of bytes in vleTermBytes
705:  * @return The finalized xxHash64 covering [startOffset..endOffset) + PCKT_TERMINATOR VLE
706:  */
707: static uint64_t computePartialPacketHash(const std::string& filepath,
708:                                           uint64_t startOffset, uint64_t endOffset,
709:                                           uint8_t* vleTermBytes, size_t& vleTermLen) {
710:     bcsv::Checksum::Streaming hash;
711: 
712:     // Feed all row data (VLE lengths + payloads) to the hash
713:     std::ifstream is(filepath, std::ios::binary);
714:     is.seekg(static_cast<std::streamoff>(startOffset));
715: 
716:     constexpr size_t BUF_SIZE = 64 * 1024;
717:     char buf[BUF_SIZE];
718:     uint64_t remaining = endOffset - startOffset;
719:     while (remaining > 0 && is) {
720:         size_t to_read = std::min(static_cast<size_t>(remaining), BUF_SIZE);
721:         is.read(buf, static_cast<std::streamsize>(to_read));
722:         auto got = is.gcount();
723:         if (got <= 0) break;
724:         hash.update(buf, static_cast<size_t>(got));
725:         remaining -= static_cast<uint64_t>(got);
726:     }
727: 
728:     // Encode PCKT_TERMINATOR as VLE and feed to hash
729:     uint64_t vle_buf;
730:     vleTermLen = bcsv::vleEncode<uint64_t, true>(bcsv::PCKT_TERMINATOR, &vle_buf, sizeof(vle_buf));
731:     std::memcpy(vleTermBytes, &vle_buf, vleTermLen);
732:     hash.update(vleTermBytes, vleTermLen);
733: 
734:     return hash.finalize();
735: }
736: 
737: // ── Output helpers ──────────────────────────────────────────────────
738: 
739: /// Copy exactly `count` bytes from src to dst using 1 MB chunks.
740: /// Returns empty string on success, error message on failure.
741: static std::string copyFileRange(std::ifstream& src, std::ostream& dst, uint64_t count) {
742:     constexpr size_t BUF_SIZE = 1024 * 1024;
743:     std::vector<char> buf(BUF_SIZE);
744:     uint64_t remaining = count;
745:     while (remaining > 0 && src && dst) {
746:         size_t to_read = std::min(static_cast<size_t>(remaining), BUF_SIZE);
747:         src.read(buf.data(), static_cast<std::streamsize>(to_read));
748:         auto got = src.gcount();
749:         if (got <= 0) break;
750:         dst.write(buf.data(), got);
751:         remaining -= static_cast<uint64_t>(got);
752:     }
753:     if (!dst) return "Write error during copy.";
754:     return {};
755: }
756: 
757: /// Create a backup and truncate the file to `offset` bytes.
758: /// Returns empty string on success, error message on failure.
759: static std::string backupAndTruncate(const std::string& path, uint64_t offset,
760:                                       bool do_backup, bool verbose) {
761:     if (do_backup) {
762:         std::string backup_path = path + ".bak";
763:         try {
764:             std::filesystem::copy_file(
765:                 path, backup_path,
766:                 std::filesystem::copy_options::overwrite_existing);
767:         } catch (const std::exception& ex) {
768:             return std::string("Failed to create backup: ") + ex.what();
769:         }
770:         if (verbose) {
771:             std::cerr << "  Backup written to: " << backup_path << "\n";
772:         }
773:     } else {
774:         std::cerr << "Warning: --in-place modifies the file directly. "
775:                      "Consider --backup to keep a copy.\n";
776:     }
777: 
778: #ifdef BCSV_HAS_FTRUNCATE
779:     if (::truncate(path.c_str(), static_cast<off_t>(offset)) != 0) {
780:         return "ftruncate failed on: " + path;
781:     }
782: #else
783:     std::vector<char> valid_data(static_cast<size_t>(offset));
784:     {
785:         std::ifstream src(path, std::ios::binary);
786:         src.read(valid_data.data(), static_cast<std::streamsize>(offset));
787:         if (src.gcount() != static_cast<std::streamsize>(offset)) {
788:             return "Failed to read valid portion for in-place repair.";
789:         }
790:     }
791:     {
792:         std::ofstream dst(path, std::ios::binary | std::ios::trunc);
793:         dst.write(valid_data.data(), static_cast<std::streamsize>(offset));
794:         if (!dst) return "Failed to rewrite file during in-place repair.";
795:     }
796: #endif
797:     return {};
798: }
799: 
800: // ── Core repair analysis ────────────────────────────────────────────
801: 
802: /**
803:  * @brief Try to read a PacketHeader at the current stream position.
804:  *
805:  * Reads exactly at the given offset and validates magic + checksum.
806:  * Preferred over PacketHeader::readNext() for repair because sequential
807:  * walking is more predictable than heuristic scanning.
808:  */
809: static bool readPacketHeaderAt(std::ifstream& is, uint64_t offset, bcsv::PacketHeader& hdr) {
810:     is.clear();
811:     is.seekg(static_cast<std::streamoff>(offset));
812:     return is.good() && hdr.read(is);
813: }
814: 
815: /**
816:  * @brief Analyze a BCSV file and optionally repair it.
817:  *
818:  * Walks the file packet-by-packet from the start, validates packet
819:  * headers (and optionally payloads), builds a new packet index,
820:  * and reports the results.
821:  *
822:  * @param cfg     Parsed CLI configuration
823:  * @return RepairResult with analysis/repair outcome
824:  */
825: static RepairResult analyzeAndRepair(const Config& cfg) {
826:     RepairResult result;
827: 
828:     // Check file exists
829:     if (!std::filesystem::exists(cfg.input_file)) {
830:         result.error = "File does not exist: " + cfg.input_file;
831:         return result;
832:     }
833: 
834:     result.file_size_original = std::filesystem::file_size(cfg.input_file);
835: 
836:     // Open input file
837:     std::ifstream is(cfg.input_file, std::ios::binary);
838:     if (!is) {
839:         result.error = "Cannot open file: " + cfg.input_file;
840:         return result;
841:     }
842: 
843:     // Read and validate file header
844:     bcsv::FileHeader file_header;
845:     bcsv::Layout layout;
846:     try {
847:         file_header.readFromBinary(is, layout);
848:     } catch (const std::exception& ex) {
849:         result.error = std::string("Invalid BCSV header: ") + ex.what();
850:         return result;
851:     }
852: 
853:     if (!file_header.isValidMagic()) {
854:         result.error = "Not a BCSV file (bad magic number).";
855:         return result;
856:     }
857: 
858:     // Detect codec type from header flags
859:     const bool isStreamMode = file_header.hasFlag(bcsv::FileFlags::STREAM_MODE);
860:     const bool isCompressed = file_header.getCompressionLevel() > 0;
861:     const bool isBatch = file_header.hasFlag(bcsv::FileFlags::BATCH_COMPRESS);
862:     const size_t header_size = bcsv::FileHeader::getBinarySize(layout);
863: 
864:     result.is_stream_mode = isStreamMode;
865: 
866:     // Check if existing footer is already valid (packet-mode only)
867:     if (!isStreamMode) {
868:         bcsv::FileFooter existing_footer;
869:         std::ifstream check_stream(cfg.input_file, std::ios::binary);
870:         if (check_stream && existing_footer.read(check_stream)) {
871:             result.had_footer = true;
872:         }
873:     }
874: 
875:     // ─── Stream-mode repair path ────────────────────────────────────
876:     //
877:     // Variables shared with packet path for unified output:
878:     //   cut_offset            — truncation point (valid data ends here)
879:     //   need_partial_write    — write packet terminator before footer?
880:     //   partial_data_start    — payload start offset (for hash computation)
881:     //   partial_cut           — end of recovered rows in partial packet
882:     //   new_footer            — footer to append (empty for stream)
883:     //
884:     uint64_t cut_offset = header_size;
885:     bool need_partial_write = false;
886:     uint64_t partial_data_start = 0;
887:     uint64_t partial_cut = 0;
888:     bcsv::FileFooter new_footer;
889: 
890:     if (isStreamMode) {
891:         // Stream files: walk row-by-row using per-row XXH32 checksums
892:         uint64_t valid_rows = 0;
893: 
894:         is.clear();
895:         is.seekg(static_cast<std::streamoff>(header_size));
896: 
897:         // Both raw and LZ4 stream formats use the same wire layout for validation
898:         // (XXH32 covers the on-disk bytes in both cases)
899:         walkStreamRows(is, valid_rows, cut_offset, cfg.verbose);
900:         is.close();
901: 
902:         result.rows_recovered = valid_rows;
903: 
904:         // Detect damage: any trailing bytes beyond the last valid row?
905:         if (cut_offset < result.file_size_original) {
906:             result.had_damage = true;
907:             result.bytes_trimmed = result.file_size_original - cut_offset;
908:             result.warnings.push_back(
909:                 "Trailing " + std::to_string(result.bytes_trimmed)
910:                 + " bytes after last valid row (truncated/corrupt)");
911:         }
912: 
913:         if (cfg.verbose) {
914:             std::cerr << "  Stream mode (" << (isCompressed ? "LZ4" : "raw")
915:                       << "): " << valid_rows << " valid rows, cut @ offset " << cut_offset << "\n";
916:         }
917: 
918:         // Stream has no footer — new_footer stays empty.
919:         // Fall through to unified output phase.
920: 
921:     } else {
922: 
923:     // ─── Packet-mode repair path ────────────────────────────────────
924: 
925:     // --- Phase 1: Scan for packets ---
926:     std::vector<PacketInfo> packet_infos;
927: 
928:     uint64_t next_offset = header_size;
929: 
930:     while (true) {
931:         bcsv::PacketHeader pkt_header;
932:         if (!readPacketHeaderAt(is, next_offset, pkt_header)) {
933:             break;
934:         }
935: 
936:         PacketInfo info;
937:         info.byte_offset = next_offset;
938:         info.first_row   = pkt_header.first_row_index;
939:         info.is_valid    = true;
940:         info.row_count   = 0;
941:         info.end_offset  = 0;
942:         info.is_last_scanned = false;
943: 
944:         if (cfg.verbose) {
945:             std::cerr << "  Packet " << packet_infos.size()
946:                       << " @ offset " << info.byte_offset
947:                       << ", first_row=" << info.first_row << "\n";
948:         }
949: 
950:         // Position stream after packet header for payload inspection
951:         is.clear();
952:         is.seekg(static_cast<std::streamoff>(info.byte_offset) + sizeof(bcsv::PacketHeader));
953: 
954:         if (cfg.deep) {
955:             size_t rows = 0;
956:             uint64_t end = 0;
957:             bool ok = deepValidatePacket(is, isBatch, rows, end);
958:             info.row_count = rows;
959:             info.end_offset = end;
960:             if (!ok) {
961:                 info.is_valid = false;
962:                 info.error_detail = isBatch
963:                     ? "LZ4 batch decompression/checksum failed — entire packet discarded (0 rows recoverable)"
964:                     : "Packet payload checksum mismatch or truncated";
965:                 if (cfg.verbose) {
966:                     std::cerr << "    INVALID: " << info.error_detail << "\n";
967:                 }
968:                 result.warnings.push_back(
969:                     "Packet " + std::to_string(packet_infos.size()) + ": " + info.error_detail);
970:                 result.had_damage = true;
971:                 result.packets_discarded++;
972:                 packet_infos.push_back(info);
973:                 break;
974:             }
975:         } else {
976:             // Shallow: just walk the payload to find end_offset
977:             size_t rows = 0;
978:             uint64_t end = 0;
979:             bool complete = false;
980:             bool ok = walkPacketPayload(is, isBatch, rows, end, complete);
981:             info.row_count = rows;
982:             info.end_offset = end;
983:             if (!ok || !complete) {
984:                 info.is_valid = false;
985:                 info.error_detail = "Incomplete packet (truncated before terminator/checksum)";
986:                 if (cfg.verbose) {
987:                     std::cerr << "    INCOMPLETE: " << info.error_detail << "\n";
988:                 }
989:                 result.warnings.push_back(
990:                     "Packet " + std::to_string(packet_infos.size()) + ": " + info.error_detail);
991:                 result.had_damage = true;
992:                 result.packets_discarded++;
993:                 packet_infos.push_back(info);
994:                 break;
995:             }
996:         }
997: 
998:         packet_infos.push_back(info);
999: 
1000:         // Next packet starts right after this one's payload checksum
1001:         if (info.end_offset > 0) {
1002:             next_offset = info.end_offset;
1003:         } else {
1004:             // Should not happen for valid packets, but fallback
1005:             break;
1006:         }
1007:     }
1008:     is.close();
1009: 
1010:     // --- Phase 2: Build new footer from valid packets ---
1011:     uint64_t total_rows = 0;
1012: 
1013:     for (size_t i = 0; i < packet_infos.size(); ++i) {
1014:         const auto& pi = packet_infos[i];
1015:         if (!pi.is_valid) {
1016:             // Partial packet recovery (non-batch only)
1017:             if (!isBatch) {
1018:                 std::ifstream recover_is(cfg.input_file, std::ios::binary);
1019:                 uint64_t payload_start = pi.byte_offset + sizeof(bcsv::PacketHeader);
1020:                 recover_is.seekg(static_cast<std::streamoff>(payload_start));
1021: 
1022:                 size_t recovered_rows = 0;
1023:                 uint64_t recovered_end = 0;
1024: 
1025:                 if (recoverPartialPacket(recover_is, recovered_rows, recovered_end)) {
1026:                     if (cfg.verbose) {
1027:                         std::cerr << "  Partial recovery: " << recovered_rows
1028:                                   << " rows salvaged from broken packet " << i << "\n";
1029:                     }
1030:                     result.warnings.push_back(
1031:                         "Packet " + std::to_string(i) + ": " + std::to_string(recovered_rows)
1032:                         + " rows recovered from broken packet (partial recovery)");
1033:                     result.rows_recovered_partial = recovered_rows;
1034: 
1035:                     // Rows lost = rows in broken packet that couldn't be recovered
1036:                     if (pi.row_count > recovered_rows) {
1037:                         result.rows_discarded += pi.row_count - recovered_rows;
1038:                     }
1039: 
1040:                     need_partial_write = true;
1041:                     new_footer.addPacketEntry(pi.byte_offset, pi.first_row);
1042:                     partial_data_start = payload_start;
1043:                     partial_cut = recovered_end;
1044:                     total_rows += recovered_rows;
1045:                 } else {
1046:                     // No rows recovered from partial packet
1047:                     result.rows_discarded += pi.row_count;
1048:                 }
1049:             } else {
1050:                 // Batch codec: entire packet lost, no partial recovery possible
1051:                 result.rows_discarded += pi.row_count;
1052:             }
1053:             break;
1054:         }
1055: 
1056:         new_footer.addPacketEntry(pi.byte_offset, pi.first_row);
1057:         if (pi.end_offset > 0) cut_offset = pi.end_offset;
1058: 
1059:         // Track row count
1060:         if (i + 1 < packet_infos.size() && packet_infos[i + 1].is_valid) {
1061:             total_rows = packet_infos[i + 1].first_row;
1062:         } else {
1063:             if (pi.row_count > 0) {
1064:                 total_rows = pi.first_row + pi.row_count;
1065:             } else {
1066:                 std::ifstream count_is(cfg.input_file, std::ios::binary);
1067:                 count_is.seekg(static_cast<std::streamoff>(pi.byte_offset) + sizeof(bcsv::PacketHeader));
1068:                 size_t rows = 0;
1069:                 uint64_t end = 0;
1070:                 bool complete = false;
1071:                 walkPacketPayload(count_is, isBatch, rows, end, complete);
1072:                 total_rows = pi.first_row + rows;
1073:                 if (end > 0) cut_offset = end;
1074:             }
1075:         }
1076:     }
1077: 
1078:     new_footer.rowCount() = total_rows;
1079:     result.packets_found = new_footer.packetIndex().size();
1080:     result.rows_recovered = total_rows;
1081: 
1082:     if (!result.had_footer && result.packets_found > 0) {
1083:         result.had_damage = true;
1084:     }
1085: 
1086:     uint64_t effective_cut = need_partial_write ? partial_cut : cut_offset;
1087:     if (effective_cut < result.file_size_original) {
1088:         result.bytes_trimmed = result.file_size_original - effective_cut;
1089:     }
1090:     result.footer_size = new_footer.encodedSize();
1091: 
1092:     } // end of stream/packet analysis branches
1093: 
1094:     // ─── Unified output phase ───────────────────────────────────────
1095: 
1096:     if (cfg.dry_run) {
1097:         return result;
1098:     }
1099: 
1100:     // Prepare packet terminator writer (only used for partial packet recovery)
1101:     auto writePacketTerminator = [&](std::ostream& os) -> bool {
1102:         uint8_t vle_term[8];
1103:         size_t vle_len = 0;
1104:         uint64_t checksum = computePartialPacketHash(
1105:             cfg.input_file, partial_data_start, partial_cut,
1106:             vle_term, vle_len);
1107:         os.write(reinterpret_cast<const char*>(vle_term),
1108:                  static_cast<std::streamsize>(vle_len));
1109:         os.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
1110:         return os.good();
1111:     };
1112: 
1113:     uint64_t trunc_point = need_partial_write ? partial_cut : cut_offset;
1114:     bool has_footer = !isStreamMode && new_footer.packetIndex().size() > 0;
1115: 
1116:     if (!cfg.output_file.empty()) {
1117:         // Copy mode
1118:         std::ifstream src(cfg.input_file, std::ios::binary);
1119:         if (!src) { result.error = "Cannot reopen input file for reading."; return result; }
1120:         std::ofstream dst(cfg.output_file, std::ios::binary | std::ios::trunc);
1121:         if (!dst) { result.error = "Cannot open output file: " + cfg.output_file; return result; }
1122: 
1123:         auto err = copyFileRange(src, dst, trunc_point);
1124:         if (!err.empty()) { result.error = err; return result; }
1125: 
1126:         if (need_partial_write && !writePacketTerminator(dst)) {
1127:             result.error = "Failed to write partial packet terminator.";
1128:             return result;
1129:         }
1130:         if (has_footer && !new_footer.write(dst)) {
1131:             result.error = "Failed to write footer to output file.";
1132:             return result;
1133:         }
1134:         return result;
1135:     }
1136: 
1137:     if (cfg.in_place) {
1138:         auto err = backupAndTruncate(cfg.input_file, trunc_point, cfg.backup, cfg.verbose);
1139:         if (!err.empty()) { result.error = err; return result; }
1140: 
1141:         // Append terminator + footer if needed
1142:         if (need_partial_write || has_footer) {
1143:             std::ofstream append_stream(cfg.input_file, std::ios::binary | std::ios::app);
1144:             if (!append_stream) { result.error = "Cannot reopen file for appending."; return result; }
1145: 
1146:             if (need_partial_write && !writePacketTerminator(append_stream)) {
1147:                 result.error = "Failed to write partial packet terminator.";
1148:                 return result;
1149:             }
1150:             if (has_footer && !new_footer.write(append_stream)) {
1151:                 result.error = "Failed to write footer during in-place repair.";
1152:                 return result;
1153:             }
1154:         }
1155:         return result;
1156:     }
1157: 
1158:     return result;
1159: }
1160: 
1161: // ── Output formatting ───────────────────────────────────────────────
1162: 
1163: static void printResultText(const RepairResult& r, const Config& cfg) {
1164:     std::ostream& os = std::cerr;
1165: 
1166:     os << "bcsvRepair: " << cfg.input_file << "\n";
1167:     os << "  File size:          " << bcsv_cli::formatBytes(r.file_size_original) << "\n";
1168:     os << "  Codec:              " << (r.is_stream_mode ? "stream" : "packet") << "\n";
1169: 
1170:     if (!r.is_stream_mode) {
1171:         os << "  Original footer:    " << (r.had_footer ? "present" : "MISSING") << "\n";
1172:         os << "  Packets found:      " << r.packets_found << "\n";
1173:         os << "  Packets discarded:  " << r.packets_discarded << "\n";
1174:     }
1175: 
1176:     os << "  Rows recovered:     " << r.rows_recovered << "\n";
1177: 
1178:     if (r.rows_recovered_partial > 0) {
1179:         os << "  Rows from partial:  " << r.rows_recovered_partial << "\n";
1180:     }
1181:     if (r.rows_discarded > 0) {
1182:         os << "  Rows discarded:     " << r.rows_discarded << "\n";
1183:     }
1184: 
1185:     os << "  Damage detected:    " << (r.had_damage ? "YES" : "no") << "\n";
1186: 
1187:     if (r.rows_recovered + r.rows_discarded > 0) {
1188:         double pct = static_cast<double>(r.rows_recovered)
1189:                    / static_cast<double>(r.rows_recovered + r.rows_discarded) * 100.0;
1190:         os << std::fixed << std::setprecision(1);
1191:         os << "  Recovery:           " << pct << "%\n";
1192:     }
1193: 
1194:     if (r.bytes_trimmed > 0) {
1195:         os << "  Bytes trimmed:      " << bcsv_cli::formatBytes(r.bytes_trimmed) << "\n";
1196:     }
1197:     if (r.footer_size > 0) {
1198:         os << "  New footer size:    " << bcsv_cli::formatBytes(r.footer_size) << "\n";
1199:     }
1200: 
1201:     for (const auto& w : r.warnings) {
1202:         os << "  Warning: " << w << "\n";
1203:     }
1204: 
1205:     if (!r.error.empty()) {
1206:         os << "  ERROR: " << r.error << "\n";
1207:     }
1208: 
1209:     if (cfg.dry_run) {
1210:         os << "  Mode: dry-run (no files modified)\n";
1211:     } else if (cfg.in_place) {
1212:         os << "  Mode: in-place repair\n";
1213:     } else {
1214:         os << "  Mode: copy to " << cfg.output_file << "\n";
1215:     }
1216: }
1217: 
1218: static void printResultJson(const RepairResult& r, const Config& cfg) {
1219:     std::cout << "{\n"
1220:         << "  \"input\": " << jsonStr(cfg.input_file) << ",\n"
1221:         << "  \"file_size\": " << r.file_size_original << ",\n"
1222:         << "  \"is_stream_mode\": " << (r.is_stream_mode ? "true" : "false") << ",\n"
1223:         << "  \"had_footer\": " << (r.had_footer ? "true" : "false") << ",\n"
1224:         << "  \"packets_found\": " << r.packets_found << ",\n"
1225:         << "  \"packets_discarded\": " << r.packets_discarded << ",\n"
1226:         << "  \"rows_recovered\": " << r.rows_recovered << ",\n"
1227:         << "  \"rows_recovered_partial\": " << r.rows_recovered_partial << ",\n"
1228:         << "  \"rows_discarded\": " << r.rows_discarded << ",\n";
1229: 
1230:     // Recovery percentage: 100% if no damage or no rows at all
1231:     uint64_t total_known = r.rows_recovered + r.rows_discarded;
1232:     double recovery_pct = (total_known > 0)
1233:         ? (static_cast<double>(r.rows_recovered) / static_cast<double>(total_known) * 100.0)
1234:         : (r.had_damage ? 0.0 : 100.0);
1235:     std::cout << std::fixed << std::setprecision(1);
1236:     std::cout << "  \"recovery_pct\": " << recovery_pct << ",\n"
1237:         << "  \"had_damage\": " << (r.had_damage ? "true" : "false") << ",\n"
1238:         << "  \"bytes_trimmed\": " << r.bytes_trimmed << ",\n"
1239:         << "  \"footer_size\": " << r.footer_size << ",\n"
1240:         << "  \"dry_run\": " << (cfg.dry_run ? "true" : "false") << ",\n"
1241:         << "  \"deep\": " << (cfg.deep ? "true" : "false") << ",\n";
1242: 
1243:     if (!r.error.empty()) {
1244:         std::cout << "  \"error\": " << jsonStr(r.error) << ",\n";
1245:     }
1246: 
1247:     std::cout << "  \"warnings\": [";
1248:     for (size_t i = 0; i < r.warnings.size(); ++i) {
1249:         if (i > 0) std::cout << ", ";
1250:         std::cout << jsonStr(r.warnings[i]);
1251:     }
1252:     std::cout << "],\n"
1253:         << "  \"success\": " << (r.error.empty() ? "true" : "false") << "\n"
1254:         << "}\n";
1255: }
1256: 
1257: // ── main ────────────────────────────────────────────────────────────
1258: 
1259: int main(int argc, char* argv[]) {
1260:     try {
1261:         Config cfg = parseArgs(argc, argv);
1262: 
1263:         if (cfg.help) {
1264:             printUsage(argv[0]);
1265:             return 0;
1266:         }
1267: 
1268:         if (cfg.verbose) {
1269:             std::cerr << "bcsvRepair: analyzing " << cfg.input_file << " ...\n";
1270:             if (cfg.deep) std::cerr << "  Deep validation enabled\n";
1271:         }
1272: 
1273:         RepairResult result = analyzeAndRepair(cfg);
1274: 
1275:         if (cfg.json) {
1276:             printResultJson(result, cfg);
1277:         } else {
1278:             printResultText(result, cfg);
1279:         }
1280: 
1281:         if (!result.error.empty()) {
1282:             return 1;
1283:         }
1284: 
1285:         return 0;
1286: 
1287:     } catch (const std::exception& ex) {
1288:         std::cerr << "Error: " << ex.what() << "\n";
1289:         return 2;
1290:     }
1291: }

(End of file - total 1291 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/src/tools/CMakeLists.txt"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/src/tools/CMakeLists.txt</path>
<type>file</type>
<content>
1: # Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
2: # 
3: # This file is part of the BCSV library.
4: # 
5: # Licensed under the MIT License. See LICENSE file in the project root 
6: # for full license information.
7: 
8: # CLI Tools CMakeLists.txt
9: # ========================
10: # BCSV command-line tools for file conversion, inspection, and processing.
11: #
12: # Converters:
13: #   csv2bcsv      - Convert CSV files to BCSV format
14: #   bcsv2csv      - Convert BCSV files to CSV format
15: #
16: # Inspection:
17: #   bcsvHead      - Display first N rows in CSV format
18: #   bcsvTail      - Display last N rows in CSV format
19: #   bcsvHeader    - Display file structure / schema
20: #
21: # Processing:
22: #   bcsvSampler   - Filter & project BCSV files using expressions
23: #   bcsvGenerator - Generate synthetic BCSV test datasets
24: 
25: # CSV to BCSV converter
26: add_executable(csv2bcsv csv2bcsv.cpp)
27: target_link_libraries(csv2bcsv PRIVATE bcsv)
28: 
29: # BCSV to CSV converter
30: add_executable(bcsv2csv bcsv2csv.cpp)
31: target_link_libraries(bcsv2csv PRIVATE bcsv)
32: 
33: # Display first N rows
34: add_executable(bcsvHead bcsvHead.cpp)
35: target_link_libraries(bcsvHead PRIVATE bcsv)
36: 
37: # Display last N rows
38: add_executable(bcsvTail bcsvTail.cpp)
39: target_link_libraries(bcsvTail PRIVATE bcsv)
40: 
41: # Display file structure / schema
42: add_executable(bcsvHeader bcsvHeader.cpp)
43: target_link_libraries(bcsvHeader PRIVATE bcsv)
44: 
45: # Streaming filter & project
46: add_executable(bcsvSampler bcsvSampler.cpp)
47: target_link_libraries(bcsvSampler PRIVATE bcsv)
48: 
49: # Synthetic dataset generator
50: add_executable(bcsvGenerator bcsvGenerator.cpp)
51: target_link_libraries(bcsvGenerator PRIVATE bcsv)
52: target_include_directories(bcsvGenerator PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)
53: 
54: # Validation tool
55: add_executable(bcsvValidate bcsvValidate.cpp)
56: target_link_libraries(bcsvValidate PRIVATE bcsv)
57: target_include_directories(bcsvValidate PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)
58: 
59: # Repair tool
60: add_executable(bcsvRepair bcsvRepair.cpp)
61: target_link_libraries(bcsvRepair PRIVATE bcsv)
62: 
63: # Comparison tool
64: add_executable(bcsvCompare bcsvCompare.cpp)
65: target_link_libraries(bcsvCompare PRIVATE bcsv)
66: target_include_directories(bcsvCompare PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)
67: 
68: # Set output directory for all tools
69: set_target_properties(csv2bcsv bcsv2csv bcsvHead bcsvTail bcsvHeader bcsvSampler bcsvGenerator bcsvValidate bcsvRepair bcsvCompare PROPERTIES
70:     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
71: )

(End of file - total 71 lines)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 12.5s)

_Thinking:_

Now I have a good understanding of the project. Let me also look at how the csv2bcsv tool does type optimization, since it already has some scanning logic. And also check the row.hpp for visit implementation details.




Let me also check how `csv2bcsv` does type detection and look at the `visit` implementation:



**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/src/tools/csv2bcsv.cpp"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/src/tools/csv2bcsv.cpp</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: /**
11:  * @file csv2bcsv.cpp
12:  * @brief CLI tool to convert CSV files to BCSV format
13:  * 
14:  * This tool reads a CSV file and converts it to the binary BCSV format.
15:  * It automatically detects data types and creates an appropriate layout.
16:  */
17: 
18: #include <iostream>
19: #include <fstream>
20: #include <string>
21: #include <vector>
22: #include <map>
23: #include <filesystem>
24: #include <chrono>
25: #include <iomanip>
26: #include <bcsv/bcsv.h>
27: #include "cli_common.h"
28: 
29: struct Config {
30:     std::string input_file;
31:     std::string output_file;
32:     char delimiter = '\0';  // '\0' means auto-detect
33:     char decimal_separator = '.';  // Default to point, can be changed to comma
34:     bool has_header = true;
35:     bool verbose = false;
36:     bool help = false;
37:     bool force_delimiter = false;  // True if user explicitly set delimiter
38:     bool overwrite = false;
39:     bool benchmark = false;  // Print timing stats to stderr
40:     bool json_output = false;  // Emit JSON timing blob to stdout
41: 
42:     // Codec selection (standardised with bcsvGenerator / bcsvSampler)
43:     std::string row_codec  = bcsv_cli::DEFAULT_ROW_CODEC;   // delta | zoh | flat
44:     std::string file_codec = bcsv_cli::DEFAULT_FILE_CODEC;  // packet_lz4_batch | ...
45:     size_t compression_level = 1;
46:     size_t block_size_kb     = bcsv::DEFAULT_PACKET_SIZE_KB;
47: };
48: 
49: // Enhanced data type detection with range analysis
50: struct ColumnStats {
51:     int64_t min_int = INT64_MAX;
52:     int64_t max_int = INT64_MIN;
53:     double min_double = std::numeric_limits<double>::max();
54:     double max_double = std::numeric_limits<double>::lowest();
55:     bool has_decimals = false;
56:     bool all_integers = true;
57:     bool all_booleans = true;
58:     bool all_empty = true;
59:     bool all_float_compatible = true;  // New: tracks if all values can be represented exactly in float
60:     uint32_t max_decimal_places = 0;  // Track the maximum decimal places in input strings
61:     bool requires_high_precision = false;  // True if any value has >6 decimal places or >7 total digits
62:     size_t sample_count = 0;
63: };
64: 
65: // Detect optimal data type based on column statistics
66: bcsv::ColumnType detectOptimalType(const ColumnStats& stats) {
67:     if (stats.all_empty || stats.sample_count == 0) {
68:         return bcsv::ColumnType::STRING;
69:     }
70:     
71:     if (stats.all_booleans && stats.sample_count > 0) {
72:         return bcsv::ColumnType::BOOL;
73:     }
74:     
75:     if (stats.all_integers && !stats.has_decimals) {
76:         // Choose smallest integer type that can hold the range
77:         if (stats.min_int >= 0 && stats.max_int <= 255) {
78:             return bcsv::ColumnType::UINT8;
79:         } else if (stats.min_int >= -128 && stats.max_int <= 127) {
80:             return bcsv::ColumnType::INT8;
81:         } else if (stats.min_int >= 0 && stats.max_int <= 65535) {
82:             return bcsv::ColumnType::UINT16;
83:         } else if (stats.min_int >= -32768 && stats.max_int <= 32767) {
84:             return bcsv::ColumnType::INT16;
85:         } else if (stats.min_int >= 0 && stats.max_int <= static_cast<int64_t>(4294967295ULL)) {
86:             return bcsv::ColumnType::UINT32;
87:         } else if (stats.min_int >= INT32_MIN && stats.max_int <= INT32_MAX) {
88:             return bcsv::ColumnType::INT32;
89:         } else if (stats.min_int >= 0) {
90:             return bcsv::ColumnType::UINT64;
91:         } else {
92:             return bcsv::ColumnType::INT64;
93:         }
94:     }
95:     
96:     if (stats.has_decimals) {
97:         // Use string-based precision analysis to choose optimal floating-point type
98:         if (stats.requires_high_precision) {
99:             // High precision explicitly requested by user (>6 decimal places or >7 total digits)
100: #if BCSV_HAS_FLOAT128
101:             // Use 128-bit quadruple precision for maximum accuracy
102:             return bcsv::ColumnDataType::FLOAT128;
103: #else
104:             // Fall back to double precision
105:             return bcsv::ColumnType::DOUBLE;
106: #endif
107:         } else if (stats.max_decimal_places <= 2) {
108:             // Very low precision requirements - consider half precision types
109: #if BCSV_HAS_FLOAT16
110:             // Use 16-bit half precision for maximum space efficiency
111:             return bcsv::ColumnDataType::FLOAT16;
112: #else
113:             // Fall back to single precision
114:             return bcsv::ColumnType::FLOAT;
115: #endif
116:         } else if (stats.max_decimal_places <= 6) {
117:             // User provided reasonable precision - use single precision
118:             // Float provides ~7 decimal digits, which is sufficient for ≤6 decimal places
119:             return bcsv::ColumnType::FLOAT;
120:         } else {
121:             // Higher precision requirements need double precision
122:             return bcsv::ColumnType::DOUBLE;
123:         }
124:     }
125:     
126:     return bcsv::ColumnType::STRING;
127: }
128: 
129: // Analyze the precision requirements from the original string
130: std::pair<uint32_t, bool> analyzeStringPrecision(const std::string& value, char decimal_separator = '.') {
131:     // Handle scientific notation (e.g., "1.0233453e+23")
132:     std::string mantissa_part = value;
133:     size_t exp_pos = value.find_first_of("eE");
134:     if (exp_pos != std::string::npos) {
135:         mantissa_part = value.substr(0, exp_pos);
136:     }
137:     
138:     // Find decimal point in mantissa
139:     size_t decimal_pos = mantissa_part.find(decimal_separator);
140:     if (decimal_pos == std::string::npos) {
141:         // No decimal point - check if this is an integer or scientific notation without decimal
142:         if (exp_pos != std::string::npos) {
143:             // Scientific notation without decimal (e.g., "123e+5")
144:             // Count digits in the integer part as significant digits
145:             uint32_t significant_digits = 0;
146:             bool found_first_nonzero = false;
147:             
148:             for (char c : mantissa_part) {
149:                 if (std::isdigit(c)) {
150:                     if (c != '0' || found_first_nonzero) {
151:                         significant_digits++;
152:                         if (c != '0') found_first_nonzero = true;
153:                     }
154:                 }
155:             }
156:             
157:             // No decimal places, but check if total significant digits require high precision
158:             bool high_precision = significant_digits > 7;
159:             return {0, high_precision};
160:         }
161:         return {0, false}; // Plain integer, no decimal places, not high precision
162:     }
163:     
164:     // Count decimal places - RESPECT USER INTENT by keeping trailing zeros
165:     // The user explicitly wrote those trailing zeros, indicating desired precision
166:     std::string decimal_part = mantissa_part.substr(decimal_pos + 1);
167:     uint32_t decimal_places = static_cast<uint32_t>(decimal_part.length());
168:     
169:     // Count total significant digits in the mantissa
170:     std::string digits_only;
171:     bool found_first_nonzero = false;
172:     bool after_decimal = false;
173:     
174:     for (char c : mantissa_part) {
175:         if (c == decimal_separator) {
176:             after_decimal = true;
177:         } else if (std::isdigit(c)) {
178:             if (c != '0' || found_first_nonzero || after_decimal) {
179:                 digits_only += c;
180:                 if (c != '0') found_first_nonzero = true;
181:             }
182:         }
183:     }
184:     
185:     // For scientific notation, the meaningful precision is determined by the mantissa
186:     // Example: "1.0233453e+23" has 8 meaningful digits (1 + 7 after decimal)
187:     uint32_t total_significant_digits = static_cast<uint32_t>(digits_only.length());
188:     
189:     // Determine if high precision is required
190:     // Use >6 decimal places or >7 total significant digits as threshold
191:     bool high_precision = (decimal_places > 6) || (total_significant_digits > 7);
192:     
193:     return {decimal_places, high_precision};
194: }
195: 
196: // Enhanced data type detection helper
197: void analyzeValue(const std::string& value, ColumnStats& stats, char decimal_separator = '.') {
198:     if (value.empty()) {
199:         return; // Don't count empty values
200:     }
201:     
202:     stats.all_empty = false;
203:     stats.sample_count++;
204:     
205:     // Check for boolean
206:     std::string lower_val = value;
207:     std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), 
208:         [](char c) { return static_cast<char>(::tolower(c)); });
209:     if (lower_val == "true" || lower_val == "false" || lower_val == "1" || lower_val == "0") {
210:         if (stats.all_booleans) {
211:             return; // Still could be boolean
212:         }
213:     } else {
214:         stats.all_booleans = false;
215:     }
216:     
217:     // Normalize decimal separator for parsing
218:     std::string normalized_value = value;
219:     if (decimal_separator != '.') {
220:         std::replace(normalized_value.begin(), normalized_value.end(), decimal_separator, '.');
221:     }
222:     
223:     // Try to parse as number
224:     char* end;
225:     errno = 0;
226:     
227:     // Try integer first (only if no decimal separator in original value)
228:     if (value.find(decimal_separator) == std::string::npos) {
229:         long long int_val = strtoll(normalized_value.c_str(), &end, 10);
230:         if (errno == 0 && *end == '\0' && end != normalized_value.c_str()) {
231:             // Valid integer
232:             stats.min_int = std::min(stats.min_int, static_cast<int64_t>(int_val));
233:             stats.max_int = std::max(stats.max_int, static_cast<int64_t>(int_val));
234:             return;
235:         }
236:     }
237:     
238:     // Try double
239:     double double_val = strtod(normalized_value.c_str(), &end);
240:     if (errno == 0 && *end == '\0' && end != normalized_value.c_str()) {
241:         // Valid double
242:         stats.all_integers = false;
243:         stats.has_decimals = true;
244:         stats.min_double = std::min(stats.min_double, double_val);
245:         stats.max_double = std::max(stats.max_double, double_val);
246:         
247:         // Analyze the precision requirements from the original string
248:         auto [decimal_places, high_precision] = analyzeStringPrecision(value, decimal_separator);
249:         stats.max_decimal_places = std::max(stats.max_decimal_places, decimal_places);
250:         if (high_precision) {
251:             stats.requires_high_precision = true;
252:         }
253:         
254:         // Check if this specific value can be represented exactly in float
255:         // (only relevant if string precision doesn't already require double)
256:         if (stats.all_float_compatible && !high_precision) {
257:             float float_val = static_cast<float>(double_val);
258:             if (static_cast<double>(float_val) != double_val) {
259:                 stats.all_float_compatible = false;
260:             }
261:         }
262:         
263:         return;
264:     }
265:     
266:     // Must be string
267:     stats.all_integers = false;
268:     stats.all_booleans = false;
269:     stats.has_decimals = false;
270: }
271: 
272: // Automatic delimiter detection
273: char detectDelimiter(const std::string& sample_line) {
274:     const std::vector<char> delimiters = {',', ';', '\t', '|'};
275:     std::map<char, int> delimiter_counts;
276:     
277:     bool in_quotes = false;
278:     char quote_char = '"';
279:     
280:     for (char c : sample_line) {
281:         if (c == quote_char) {
282:             in_quotes = !in_quotes;
283:         } else if (!in_quotes) {
284:             for (char delim : delimiters) {
285:                 if (c == delim) {
286:                     delimiter_counts[delim]++;
287:                 }
288:             }
289:         }
290:     }
291:     
292:     // Return delimiter with highest count
293:     char best_delimiter = ',';
294:     int max_count = 0;
295:     for (const auto& pair : delimiter_counts) {
296:         if (pair.second > max_count) {
297:             max_count = pair.second;
298:             best_delimiter = pair.first;
299:         }
300:     }
301:     
302:     return best_delimiter;
303: }
304: 
305: // Parse CSV line with proper quote handling
306: std::vector<std::string> parseCSVLine(const std::string& line, char delimiter, char quote_char) {
307:     std::vector<std::string> fields;
308:     std::string current_field;
309:     bool in_quotes = false;
310:     bool quote_started = false;
311:     
312:     for (size_t i = 0; i < line.length(); ++i) {
313:         char c = line[i];
314:         
315:         if (c == quote_char) {
316:             if (!quote_started && current_field.empty()) {
317:                 // Starting quoted field
318:                 in_quotes = true;
319:                 quote_started = true;
320:             } else if (in_quotes) {
321:                 // Check for escaped quote (double quote)
322:                 if (i + 1 < line.length() && line[i + 1] == quote_char) {
323:                     current_field += quote_char;
324:                     ++i; // Skip the next quote
325:                 } else {
326:                     // End of quoted field
327:                     in_quotes = false;
328:                 }
329:             } else {
330:                 // Quote in unquoted field (treat as regular character)
331:                 current_field += c;
332:             }
333:         } else if (c == delimiter && !in_quotes) {
334:             // Field separator
335:             fields.push_back(current_field);
336:             current_field.clear();
337:             quote_started = false;
338:         } else {
339:             current_field += c;
340:         }
341:     }
342:     
343:     // Add the last field
344:     fields.push_back(current_field);
345:     return fields;
346: }
347: 
348: void printUsage(const char* program_name) {
349:     std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE [OUTPUT_FILE]\n\n";
350:     std::cout << "Convert CSV file to BCSV format.\n\n";
351:     std::cout << "Arguments:\n";
352:     std::cout << "  INPUT_FILE     Input CSV file path\n";
353:     std::cout << "  OUTPUT_FILE    Output BCSV file path (default: INPUT_FILE.bcsv)\n\n";
354:     std::cout << "Options:\n";
355:     std::cout << "  -d, --delimiter CHAR    Field delimiter (default: auto-detect)\n";
356:     std::cout << "  --no-header             CSV file has no header row\n";
357:     std::cout << "  --decimal-separator CHAR  Decimal separator: '.' or ',' (default: '.')\n";
358:     std::cout << "  -v, --verbose           Enable verbose output\n";
359:     std::cout << "  -f, --overwrite         Overwrite output file if it exists\n";
360:     std::cout << "  --benchmark             Print timing stats (wall clock, rows/s, MB/s) to stderr\n";
361:     std::cout << "  --json                  With --benchmark: emit JSON timing blob to stderr\n";
362:     std::cout << "  -h, --help              Show this help message\n\n";
363:     std::cout << "Encoding (defaults: packet_lz4_batch + delta):\n";
364:     std::cout << "  --row-codec CODEC       Row codec: delta (default), zoh, flat\n";
365:     std::cout << "  --file-codec CODEC      File codec: packet_lz4_batch (default),\n";
366:     std::cout << "                          packet_lz4, packet, stream_lz4, stream\n";
367:     std::cout << "  --compression-level N   LZ4 compression level (default: 1)\n";
368:     std::cout << "  --block-size N          Block size in KB (default: 64)\n\n";
369:     std::cout << "Examples:\n";
370:     std::cout << "  " << program_name << " data.csv\n";
371:     std::cout << "  " << program_name << " -d ';' data.csv output.bcsv\n";
372:     std::cout << "  " << program_name << " --no-header -v data.csv\n";
373:     std::cout << "  " << program_name << " --decimal-separator ',' german_data.csv\n";
374:     std::cout << "  " << program_name << " --row-codec zoh data.csv\n";
375:     std::cout << "  " << program_name << " --row-codec flat --file-codec stream data.csv\n";
376: }
377: 
378: Config parseArgs(int argc, char* argv[]) {
379:     Config config;
380:     
381:     for (int i = 1; i < argc; ++i) {
382:         std::string arg = argv[i];
383:         
384:         if (arg == "-h" || arg == "--help") {
385:             config.help = true;
386:             return config;
387:         } else if (arg == "-d" || arg == "--delimiter") {
388:             if (i + 1 < argc) {
389:                 config.delimiter = argv[++i][0];
390:                 config.force_delimiter = true;
391:             } else {
392:                 throw std::runtime_error("Option " + arg + " requires an argument");
393:             }
394:         } else if (arg == "--no-header") {
395:             config.has_header = false;
396:         } else if (arg == "-f" || arg == "--overwrite") {
397:             config.overwrite = true;
398:         } else if (arg == "-v" || arg == "--verbose") {
399:             config.verbose = true;
400:         } else if (arg == "--benchmark") {
401:             config.benchmark = true;
402:         } else if (arg == "--json") {
403:             config.json_output = true;
404:         } else if (arg == "--row-codec" && i + 1 < argc) {
405:             config.row_codec = argv[++i];
406:             bcsv_cli::validateRowCodec(config.row_codec);
407:         } else if (arg == "--file-codec" && i + 1 < argc) {
408:             config.file_codec = argv[++i];
409:             bcsv_cli::validateFileCodec(config.file_codec);
410:         } else if (arg == "--compression-level" && i + 1 < argc) {
411:             try {
412:                 int lvl = std::stoi(argv[++i]);
413:                 if (lvl < 0) throw std::runtime_error("Compression level must be non-negative");
414:                 config.compression_level = static_cast<size_t>(lvl);
415:             } catch (const std::invalid_argument&) {
416:                 throw std::runtime_error(std::string("Invalid compression level: ") + argv[i]);
417:             }
418:         } else if (arg == "--block-size" && i + 1 < argc) {
419:             try {
420:                 int bs = std::stoi(argv[++i]);
421:                 if (bs <= 0) throw std::runtime_error("Block size must be positive");
422:                 config.block_size_kb = static_cast<size_t>(bs);
423:             } catch (const std::invalid_argument&) {
424:                 throw std::runtime_error(std::string("Invalid block size: ") + argv[i]);
425:             }
426:         } else if (arg == "--decimal-separator") {
427:             if (i + 1 < argc) {
428:                 std::string sep = argv[++i];
429:                 if (sep.length() == 1 && (sep[0] == '.' || sep[0] == ',')) {
430:                     config.decimal_separator = sep[0];
431:                 } else {
432:                     throw std::runtime_error("Decimal separator must be '.' or ','");
433:                 }
434:             } else {
435:                 throw std::runtime_error("Option " + arg + " requires an argument");
436:             }
437:         } else if (arg.starts_with("-")) {
438:             throw std::runtime_error("Unknown option: " + arg);
439:         } else {
440:             // Positional arguments
441:             if (config.input_file.empty()) {
442:                 config.input_file = arg;
443:             } else if (config.output_file.empty()) {
444:                 config.output_file = arg;
445:             } else {
446:                 throw std::runtime_error("Too many arguments");
447:             }
448:         }
449:     }
450:     
451:     if (config.input_file.empty() && !config.help) {
452:         throw std::runtime_error("Input file is required");
453:     }
454:     
455:     // Set default output file if not specified
456:     if (config.output_file.empty() && !config.input_file.empty()) {
457:         std::filesystem::path input_path(config.input_file);
458:         config.output_file = input_path.stem().string() + ".bcsv";
459:     }
460:     
461:     // Validate character conflicts
462:     if (!config.help) {
463:         if (config.delimiter == config.decimal_separator && config.delimiter != '\0') {
464:             throw std::runtime_error("Delimiter and decimal separator cannot be the same ('" + 
465:                                    std::string(1, config.delimiter) + "')");
466:         }
467:     }
468:     
469:     return config;
470: }
471: 
472: int main(int argc, char* argv[]) {
473:     try {
474:         Config config = parseArgs(argc, argv);
475:         
476:         if (config.help) {
477:             printUsage(argv[0]);
478:             return 0;
479:         }
480:         
481:         if (config.verbose) {
482:             std::cerr << "Converting: " << config.input_file << " -> " << config.output_file << std::endl;
483:             std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
484:             std::cerr << "Header: " << (config.has_header ? "yes" : "no") << std::endl;
485:             std::cerr << "Decimal separator: '" << config.decimal_separator << "'" << std::endl;
486:             std::cerr << "Encoding: " << bcsv_cli::encodingDescription(
487:                 config.row_codec, config.file_codec, config.compression_level) << std::endl;
488:         }
489:         
490:         // Check if input file exists
491:         if (!std::filesystem::exists(config.input_file)) {
492:             throw std::runtime_error("Input file does not exist: " + config.input_file);
493:         }
494:         
495:         // Get input file size for compression statistics
496:         auto input_file_size = std::filesystem::file_size(config.input_file);
497:         
498:         // Start timing the conversion process
499:         auto start_time = std::chrono::steady_clock::now();
500:         
501:         std::ifstream input(config.input_file);
502:         if (!input.is_open()) {
503:             throw std::runtime_error("Cannot open input file: " + config.input_file);
504:         }
505: 
506:         std::string line;
507:         std::vector<std::string> headers;
508:         std::vector<bcsv::ColumnType> column_types;
509:         std::vector<std::vector<std::string>> sample_data;
510:         
511:         // Read first line for auto-detection
512:         if (!std::getline(input, line)) {
513:             throw std::runtime_error("Input file is empty");
514:         }
515:         
516:         // Trim carriage return (Windows line endings)
517:         if (!line.empty() && line.back() == '\r') {
518:             line.pop_back();
519:         }
520:         
521:         // Auto-detect delimiter if not specified
522:         if (!config.force_delimiter) {
523:             config.delimiter = detectDelimiter(line);
524:             if (config.verbose) {
525:                 std::cout << "Auto-detected delimiter: '" << config.delimiter << "'" << std::endl;
526:             }
527:         }
528:         
529:         if (config.verbose) {
530:             std::cerr << "Converting: " << config.input_file << " -> " << config.output_file << std::endl;
531:             std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
532:             std::cerr << "Header: " << (config.has_header ? "yes" : "no") << std::endl;
533:         }
534:         
535:         std::vector<std::string> first_row = parseCSVLine(line, config.delimiter, '"');
536:         
537:         if (config.has_header) {
538:             headers = first_row;
539:             // Filter out empty column names (from trailing delimiters)
540:             while (!headers.empty() && headers.back().empty()) {
541:                 headers.pop_back();
542:             }
543:         } else {
544:             // Filter out empty trailing fields from first row
545:             while (!first_row.empty() && first_row.back().empty()) {
546:                 first_row.pop_back();
547:             }
548:             // Generate column names
549:             for (size_t i = 0; i < first_row.size(); ++i) {
550:                 headers.push_back("column_" + std::to_string(i + 1));
551:             }
552:             sample_data.push_back(first_row);
553:         }
554:         
555:         // Read sample data to analyze types (up to 1000 rows for better detection)
556:         size_t sample_count = 0;
557:         while (std::getline(input, line) && sample_count < 1000) {
558:             // Trim carriage return (Windows line endings)
559:             if (!line.empty() && line.back() == '\r') {
560:                 line.pop_back();
561:             }
562:             
563:             auto row_data = parseCSVLine(line, config.delimiter, '"');
564:             
565:             // Trim trailing empty fields to match header count
566:             while (row_data.size() > headers.size() && !row_data.empty() && row_data.back().empty()) {
567:                 row_data.pop_back();
568:             }
569:             
570:             // Accept rows that match header count, or pad/truncate if close
571:             if (row_data.size() == headers.size()) {
572:                 sample_data.push_back(row_data);
573:                 sample_count++;
574:             } else if (row_data.size() < headers.size()) {
575:                 // Pad with empty strings if row is shorter
576:                 row_data.resize(headers.size(), "");
577:                 sample_data.push_back(row_data);
578:                 sample_count++;
579:             } else if (row_data.size() <= headers.size() + 3) {
580:                 // If only a few extra fields, truncate (likely trailing delimiters)
581:                 row_data.resize(headers.size());
582:                 sample_data.push_back(row_data);
583:                 sample_count++;
584:             } else if (config.verbose) {
585:                 // Only warn for significantly different row sizes
586:                 std::cerr << "Warning: Row " << (sample_count + 1) << " has " << row_data.size() 
587:                           << " fields, expected " << headers.size() << ". Skipping." << std::endl;
588:             }
589:         }
590:         
591:         if (sample_data.empty()) {
592:             throw std::runtime_error("No valid data rows found");
593:         }
594:         
595:         // Analyze column statistics for optimal type detection
596:         std::vector<ColumnStats> column_stats(headers.size());
597:         
598:         for (const auto& row : sample_data) {
599:             for (size_t col = 0; col < std::min(row.size(), headers.size()); ++col) {
600:                 analyzeValue(row[col], column_stats[col], config.decimal_separator);
601:             }
602:         }
603:         
604:         // Determine optimal types based on statistics
605:         column_types.resize(headers.size());
606:         for (size_t col = 0; col < headers.size(); ++col) {
607:             column_types[col] = detectOptimalType(column_stats[col]);
608:         }
609:         
610:         if (config.verbose) {
611:             std::cerr << "Detected " << headers.size() << " columns:" << std::endl;
612:             for (size_t i = 0; i < headers.size(); ++i) {
613:                 std::cerr << "  " << headers[i] << " -> " << column_types[i] << std::endl;
614:             }
615:         }
616:         
617:         // Create BCSV layout
618:         bcsv::Layout layout;
619:         for (size_t i = 0; i < headers.size(); ++i) {
620:             bcsv::ColumnDefinition col(headers[i], column_types[i]);
621:             layout.addColumn(col);
622:         }
623:         
624:         // Close the first-pass input stream (CsvReader will open its own)
625:         input.close();
626:         
627:         // Create BCSV writer and convert data using CsvReader for the second pass
628:         auto codec_settings = bcsv_cli::resolveCodecFlags(
629:             config.file_codec, config.row_codec, config.compression_level);
630:         size_t row_count = 0;
631:         auto write_rows = [&](auto& writer) {
632:             if (!writer.open(config.output_file, config.overwrite,
633:                         codec_settings.comp_level, config.block_size_kb,
634:                         codec_settings.flags)) {
635:                 throw std::runtime_error("Cannot open output file: " + config.output_file +
636:                     " (" + writer.getErrorMsg() + ")");
637:             }
638: 
639:             // Use CsvReader with the detected layout for type-safe CSV parsing
640:             bcsv::CsvReader<bcsv::Layout> csv_reader(layout, config.delimiter, config.decimal_separator);
641:             if (!csv_reader.open(config.input_file, config.has_header)) {
642:                 throw std::runtime_error("Cannot open CSV file with CsvReader: " + csv_reader.getErrorMsg());
643:             }
644: 
645:             while (csv_reader.readNext()) {
646:                 // Copy parsed row data from CsvReader to BCSV writer via visitConst
647:                 csv_reader.row().visitConst([&](size_t col, const auto& val) {
648:                     writer.row().set(col, val);
649:                 });
650: 
651:                 writer.writeRow();
652:                 ++row_count;
653: 
654:                 if (config.verbose && (row_count & 0x3FFF) == 0) {
655:                     std::cerr << "Processed " << row_count << " rows..." << std::endl;
656:                 }
657:             }
658: 
659:             csv_reader.close();
660:             writer.close();
661:         };
662: 
663:         bcsv_cli::withWriter(layout, config.row_codec, write_rows);
664: 
665:         // Calculate conversion timing and statistics
666:         auto end_time = std::chrono::steady_clock::now();
667:         auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
668:         auto output_file_size = std::filesystem::file_size(config.output_file);
669: 
670:         // Ensure minimum duration for throughput calculation
671:         long long duration_ms = duration.count();
672:         if (duration_ms == 0) duration_ms = 1;  // Minimum 1ms for calculation
673:         double duration_seconds = duration_ms / 1000.0;
674: 
675:         // Calculate compression ratio and throughput
676:         double compression_ratio = (static_cast<double>(input_file_size - output_file_size) / input_file_size) * 100.0;
677:         double throughput_mb_s = (static_cast<double>(input_file_size) / (1024.0 * 1024.0)) / duration_seconds;
678:         double rows_per_sec = static_cast<double>(row_count) / duration_seconds;
679:         
680:         // Display comprehensive conversion statistics
681:         std::cerr << "\n=== Conversion Complete ==="<< std::endl;
682:         std::cerr << "Successfully converted " << row_count << " rows to " << config.output_file << std::endl;
683:         std::cerr << "Columns detected: " << headers.size() << std::endl;
684:         std::cerr << layout << std::endl;
685:         std::cerr << "Performance Statistics:" << std::endl;
686:         std::cerr << "  Conversion time: " << duration.count() << " ms" << std::endl;
687:         std::cerr << "  Throughput: " << std::fixed << std::setprecision(2) << throughput_mb_s << " MB/s" << std::endl;
688:         std::cerr << "  Rows/second: " << std::fixed << std::setprecision(0) << rows_per_sec << " rows/s" << std::endl;
689:         std::cerr << "\nCompression Statistics:" << std::endl;
690:         std::cerr << "  Input CSV size: " << input_file_size << " bytes (" << std::fixed << std::setprecision(2) << (input_file_size / 1024.0) << " KB)" << std::endl;
691:         std::cerr << "  Output BCSV size: " << output_file_size << " bytes (" << std::fixed << std::setprecision(2) << (output_file_size / 1024.0) << " KB)" << std::endl;
692:         
693:         if (output_file_size <= input_file_size) {
694:             std::cerr << "  Compression ratio: " << std::fixed << std::setprecision(1) << compression_ratio << "%" << std::endl;
695:             std::cerr << "  Space saved: " << (input_file_size - output_file_size) << " bytes" << std::endl;
696:         } else {
697:             double size_increase_ratio = (static_cast<double>(output_file_size - input_file_size) / input_file_size) * 100.0;
698:             std::cerr << "  File size increase: " << std::fixed << std::setprecision(1) << size_increase_ratio << "% (overhead from binary format and metadata)" << std::endl;
699:             std::cerr << "  Additional space used: " << (output_file_size - input_file_size) << " bytes" << std::endl;
700:         }
701:         std::cerr << "  Compression mode: " << bcsv_cli::encodingDescription(
702:             config.row_codec, config.file_codec, config.compression_level) << std::endl;
703: 
704:         // --benchmark: structured timing output
705:         if (config.benchmark) {
706:             if (config.json_output) {
707:                 // JSON blob to stderr (keeps stdout clean for piping)
708:                 std::cerr << "{\"tool\":\"csv2bcsv\""
709:                           << ",\"input_file\":\"" << config.input_file << "\""
710:                           << ",\"output_file\":\"" << config.output_file << "\""
711:                           << ",\"rows\":" << row_count
712:                           << ",\"columns\":" << headers.size()
713:                           << ",\"input_bytes\":" << input_file_size
714:                           << ",\"output_bytes\":" << output_file_size
715:                           << ",\"wall_ms\":" << duration_ms
716:                           << ",\"throughput_mb_s\":" << std::fixed << std::setprecision(2) << throughput_mb_s
717:                           << ",\"rows_per_sec\":" << std::fixed << std::setprecision(0) << rows_per_sec
718:                           << ",\"compression_ratio\":" << std::fixed << std::setprecision(1) << compression_ratio
719:                           << ",\"row_codec\":\"" << config.row_codec << "\""
720:                           << ",\"file_codec\":\"" << config.file_codec << "\""
721:                           << "}" << std::endl;
722:             } else {
723:                 std::cerr << "[benchmark] csv2bcsv: "
724:                           << row_count << " rows, "
725:                           << duration_ms << " ms, "
726:                           << std::fixed << std::setprecision(2) << throughput_mb_s << " MB/s, "
727:                           << std::fixed << std::setprecision(0) << rows_per_sec << " rows/s\n";
728:             }
729:         }
730:         
731:     } catch (const std::exception& e) {
732:         std::cerr << "Error: " << e.what() << std::endl;
733:         return 1;
734:     }
735:     
736:     return 0;
737: }

(End of file - total 737 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/row.hpp",
  "limit": 100
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/row.hpp</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: /**
13:  * @file row.hpp
14:  * @brief Binary CSV (BCSV) Library - Row implementations
15:  * 
16:  * This file contains the implementations for the Row and RowStatic classes.
17:  *
18:  * ## Strict Aliasing & Type-Punning Notes
19:  *
20:  * Row stores scalar column values in a packed std::vector<std::byte> buffer (data_).
21:  * Access to typed values uses reinterpret_cast<T*>(&data_[offset]) in get<T>(),
22:  * ref<T>(), visitConst(), and visit(). This is well-defined for the following reasons:
23:  *
24:  * 1. All scalar column types (int8..uint64, float, double) are implicit-lifetime
25:  *    types (trivially constructible and destructible). Under C++20 P0593R6
26:  *    ("Implicit creation of objects for low-level object manipulation"),
27:  *    operations that write into byte-type storage — such as std::memcpy (used
28:  *    by the row codecs during deserialization and by set<T>()) and std::memset
29:  *    (used by clear()) — implicitly create objects of implicit-lifetime types
30:  *    in that storage.
31:  *
32:  * 2. Codec deserialization (RowCodecFlat001, RowCodecZoH001, RowCodecDelta002)
33:  *    populates data_ exclusively via std::memcpy, which implicitly starts the
34:  *    lifetime of the typed objects at the target offsets.
35:  *
36:  * 3. set<T>() uses std::memcpy to write values into data_, which likewise
37:  *    implicitly creates the typed object.
38:  *
39:  * 4. Subsequent reads via reinterpret_cast<const T*>(&data_[offset]) in
40:  *    get<T>(), ref<T>(), and the visitor dispatch are therefore accessing
41:  *    objects whose lifetime has already been started by a prior write.
42:  *
43:  * 5. clear() uses std::memset(..., 0, ...) which implicitly creates
44:  *    zero-valued objects of implicit-lifetime types (all arithmetic types
45:  *    have a valid zero representation).
46:  *
47:  * References:
48:  *   - P0593R6: https://wg21.link/P0593R6
49:  *   - [intro.object] §6.7.2.11-13 (implicit object creation)
50:  *   - [basic.types] §6.8 (implicit-lifetime types)
51:  */
52: 
53: #include "bcsv/bitset.h"
54: #include "bcsv/definitions.h"
55: #include "row.h"
56: #include "layout.h"
57: #include "std_charconv_compat.h"
58: 
59: #include <cstddef>
60: #include <cstdint>
61: #include <cstring>
62: #include <stdexcept>
63: #include <string>
64: #include <string_view>
65: #include <array>
66: #include <cassert>
67: #include <type_traits>
68: #include <ostream>
69: 
70: namespace bcsv {
71: 
72:     // ========================================================================
73:     // Row Implementation
74:     // ========================================================================
75: 
76:     inline Row::Row(const Layout& layout)
77:         : layout_(layout)
78:         , bits_(0)
79:         , data_()
80:         , strg_()
81:     {
82:         // Register as observer for layout changes
83:         layout_.registerCallback(this, {
84:                [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
85:             }
86:         );
87: 
88:         const size_t colCount = layout_.columnCount();
89: 
90:         // Count bool and string columns to size bits_ and strg_
91:         size_t boolCount = 0;
92:         size_t strgCount = 0;
93:         uint32_t dataSize = 0;
94:         for (size_t i = 0; i < colCount; ++i) {
95:             ColumnType type = layout_.columnType(i);
96:             if (type == ColumnType::BOOL)   ++boolCount;
97:             else if (type == ColumnType::STRING) ++strgCount;
98:         }
99: 
100:         // Compute dataSize from layout offsets

(Showing lines 1-100 of 1239. Use offset=101 to continue.)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 7.8s)

_Thinking:_

Now I have a thorough understanding of the project. Let me look at the visitor implementation to understand how to efficiently iterate over rows.






**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/row.hpp",
  "offset": 100,
  "limit": 200
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/row.hpp</path>
<type>file</type>
<content>
100:         // Compute dataSize from layout offsets
101:         {
102:             std::vector<uint32_t> tmpOffsets;
103:             Layout::Data::computeOffsets(layout_.columnTypes(), tmpOffsets, dataSize);
104:         }
105: 
106:         // Initialize storage
107:         data_.resize(dataSize);  // zero-initialized = scalar defaults (0 for all arithmetic types)
108:         strg_.resize(strgCount); // default-constructed empty strings
109:         bits_.resize(boolCount, false);  // just bool values, default false
110:     }
111: 
112:     inline Row::Row(const Row& other)
113:         : layout_(other.layout_)  // Share layout (shallow copy of shared_ptr inside)
114:         , bits_(other.bits_)
115:         , data_(other.data_)      // trivial byte copy (no string objects inside!)
116:         , strg_(other.strg_)      // vector copy handles deep string copies automatically
117:     {
118:         // Register as observer for layout changes (independent from 'other')
119:         layout_.registerCallback(this, {
120:                [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
121:             }
122:         );
123:     }
124: 
125:     inline Row::Row(Row&& other) noexcept
126:         : layout_(other.layout_)
127:         , bits_(std::move(other.bits_))
128:         , data_(std::move(other.data_))
129:         , strg_(std::move(other.strg_))
130:     {
131:         // Take over other's callback slot instead of allocating a new one.
132:         // This is noexcept (in-place update), fixes the moved-from callback leak,
133:         // and enables std::vector<Row> to use move during reallocation.
134:         layout_.replaceCallbackOwner(&other, this, {
135:             [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
136:         });
137:     }
138: 
139:     inline Row::~Row()
140:     {
141:         // Unregister from layout callbacks
142:         layout_.unregisterCallback(this);
143:     }
144: 
145:     /** Clear the row to its default state (default values) */
146:     inline void Row::clear()
147:     {
148:         // Zero all scalar data
149:         std::memset(data_.data(), 0, data_.size());
150: 
151:         // Clear all strings
152:         for (auto& s : strg_) {
153:             s.clear();
154:         }
155: 
156:         // Reset all bits (bools = false)
157:         bits_.reset();
158:     }
159: 
160:     // =========================================================================
161:     // 1. RAW POINTER ACCESS (caller must ensure type safety)
162:     // =========================================================================
163:     inline const void* Row::get(size_t index) const {
164:         if constexpr (RANGE_CHECKING) {
165:             if (index >= layout_.columnCount()) {
166:                 return nullptr;
167:             }
168:         } else {
169:             assert(index < layout_.columnCount() && "Row::get(index): Index out of bounds");
170:         }
171:         ColumnType type = layout_.columnType(index);
172:         uint32_t offset = layout_.columnOffset(index);
173:         if (type == ColumnType::BOOL) {
174:             // WARNING: Returns pointer to thread-local storage. The pointer is only valid until
175:             // the next get(size_t) call for a BOOL column on the same thread. Two consecutive
176:             // bool get() calls will alias. Prefer get<bool>(index) for safe by-value access.
177:             static thread_local bool tl_bool[2];
178:             static thread_local unsigned tl_idx = 0;
179:             tl_idx = 1 - tl_idx;  // alternate between [0] and [1]
180:             tl_bool[tl_idx] = bits_[offset];
181:             return &tl_bool[tl_idx];
182:         } else if (type == ColumnType::STRING) {
183:             return &strg_[offset];
184:         } else {
185:             return &data_[offset];
186:         }
187:     }
188:    
189:     // =========================================================================
190:     // 2. TYPED CONST ACCESS (Row Only) - STRICT, ZERO-COPY
191:     // Returns const T& for scalars and strings, bool by value for bools.
192:     // String views (string_view, span<const char>) return lightweight views.
193:     // Type must match exactly (no conversions).
194:     // For flexible access with type conversions, use get(index, T& dst) instead.
195:     //
196:     // Aliasing safety: The reinterpret_cast read of data_ is well-defined
197:     // because a T object has been implicitly created at the target offset by
198:     // a prior std::memcpy (codec deserialization or set<T>()) or std::memset
199:     // (clear()). See file-level documentation for details (P0593R6).
200:     // =========================================================================
201:     template<typename T>
202:     BCSV_ALWAYS_INLINE decltype(auto) Row::get(size_t index) const {
203:         // Compile-time type validation
204:         static_assert(
205:             std::is_trivially_copyable_v<T> || 
206:             std::is_same_v<T, std::string> || 
207:             std::is_same_v<T, std::string_view> || 
208:             std::is_same_v<T, std::span<const char>>,
209:             "Row::get<T>: Type T must be either trivially copyable (primitives) or string-related (std::string, std::string_view, std::span<const char>)"
210:         );
211: 
212:         if constexpr (RANGE_CHECKING) {
213:             if (index >= layout_.columnCount()) [[unlikely]] {
214:                 throw std::out_of_range("Row::get<T>: Index out of range");
215:             }
216:             if (toColumnType<T>() != layout_.columnType(index)) [[unlikely]] {
217:                 throw std::runtime_error(
218:                     "Row::get<T>: Type mismatch at index " + std::to_string(index) + 
219:                     ". Requested: " + std::string(toString(toColumnType<T>())) + 
220:                     ", Actual: " + std::string(toString(layout_.columnType(index))));
221:             }
222:         }
223:         
224:         uint32_t offset = layout_.columnOffset(index);
225: 
226:         if constexpr (std::is_same_v<T, bool>) {
227:             return bits_[offset];  // bool by value (Bitset has no addressable storage)
228:         } else if constexpr (std::is_same_v<T, std::string>) {
229:             return static_cast<const std::string&>(strg_[offset]);  // const ref, zero-copy
230:         } else if constexpr (std::is_same_v<T, std::string_view>) {
231:             return std::string_view(strg_[offset]);  // lightweight view into internal storage
232:         } else if constexpr (std::is_same_v<T, std::span<const char>>) {
233:             return std::span<const char>(strg_[offset].data(), strg_[offset].size());
234:         } else {
235:             return static_cast<const T&>(*reinterpret_cast<const T*>(&data_[offset]));  // const ref, zero-copy
236:         }
237:     }
238: 
239:     /** Vectorized access to multiple columns of same type - STRICT. */
240:     template<typename T>
241:     inline void Row::get(size_t index, std::span<T> &dst) const
242:     {
243:         static_assert(std::is_arithmetic_v<T>, "vectorized get() supports arithmetic types only");
244:         constexpr ColumnType targetType = toColumnType<T>();
245: 
246:         if (index + dst.size() > layout_.columnCount()) [[unlikely]] {
247:             throw std::out_of_range("vectorized get(): range out of bounds");
248:         }
249: 
250:         if (RANGE_CHECKING) {
251:             const ColumnType* types = layout_.columnTypes().data();
252:             for (size_t i = index; i < index+dst.size(); ++i) {
253:                 if (targetType != types[i]) [[unlikely]]{
254:                     throw std::runtime_error("vectorized get() types must match exactly");
255:                 }
256:             }
257:         }
258: 
259:         if constexpr (std::is_same_v<T, bool>) {
260:             // Bit-by-bit extraction from bits_
261:             for (size_t i = 0; i < dst.size(); ++i) {
262:                 dst[i] = bits_[layout_.columnOffset(index + i)];
263:             }
264:         } else {
265:             // Consecutive same-type scalars are contiguous in aligned data_
266:             uint32_t offset = layout_.columnOffset(index);
267:             memcpy(dst.data(), &data_[offset], dst.size() * sizeof(T));
268:         }
269:     }
270: 
271:     // =========================================================================
272:     // 3. FLEXIBLE / CONVERTING ACCESS
273:     // =========================================================================
274:     template<typename T>
275:     inline bool Row::get(size_t index, T &dst) const {
276:         bool success = false;
277:         
278:         visitConst(index, [&dst, &success](size_t, const auto& value) {
279:             using SrcType = std::decay_t<decltype(value)>;
280:             
281:             if constexpr (std::is_assignable_v<T&, SrcType>) {
282:                 if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<SrcType>) {
283:                     dst = static_cast<T>(value);
284:                     success = true;
285:                 } else if constexpr (!std::is_arithmetic_v<SrcType>) {
286:                     dst = value;
287:                     success = true;
288:                 }
289:             } else if constexpr (std::is_same_v<SrcType, std::string>) {
290:                 if constexpr (std::is_same_v<T, std::string>) {
291:                     dst = value;
292:                     success = true;
293:                 } else if constexpr (std::is_same_v<T, std::string_view>) {
294:                     dst = std::string_view(value);
295:                     success = true;
296:                 } else if constexpr (std::is_same_v<T, std::span<const char>>) {
297:                     dst = std::span<const char>(value);
298:                     success = true;
299:                 } else if constexpr (std::is_assignable_v<T&, std::string>) {

(Showing lines 100-299 of 1239. Use offset=300 to continue.)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 7.5s)

_Thinking:_

I have a solid understanding of the project now. Let me look at the visit/visitConst implementations to understand the visitor pattern.







**Tool: grep**

**Input:**
```json
{
  "pattern": "template<RowVisitorConst Visitor>\\s*void Row::visitConst",
  "path": "/home/tobias/ws/bcsv/include/bcsv/row.hpp"
}
```

**Output:**
```
No files found
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/bcsv.h"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/bcsv.h</path>
<type>file</type>
<content>
1: /*
2:  * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
3:  * 
4:  * This file is part of the BCSV library.
5:  * 
6:  * Licensed under the MIT License. See LICENSE file in the project root 
7:  * for full license information.
8:  */
9: 
10: #pragma once
11: 
12: /**
13:  * @file bcsv.h
14:  * @brief Binary CSV (BCSV) Library - Main Header with Declarations
15:  * 
16:  * A C++20 header-only library for reading and writing binary CSV files
17:  * with type safety, compression support, and efficient I/O operations.
18:  * 
19:  * This header includes all BCSV component declarations:
20:  * - FileHeader: Binary file header management
21:  * - ColumnLayout: Column definitions and metadata
22:  * - Row: Individual data rows
23:  * - Packet: Data packet abstraction
24:  * - Reader: Template-based file reading
25:  * - Writer: Template-based file writing
26:  */
27: 
28: #include <string>
29: 
30: // Core definitions first
31: #include "definitions.h"
32: 
33: // Core component declarations
34: #include "file_header.h"
35: #include "layout.h"
36: #include "layout_guard.h"
37: #include "packet_header.h"
38: #include "reader.h"
39: #include "row.h"
40: #include "codec_row/row_codec_flat001.h"        // Flat001 codec
41: #include "codec_row/row_codec_zoh001.h"         // ZoH001 codec
42: #include "codec_row/row_codec_delta002.h"       // Delta row codec
43: #include "codec_row/row_codec_dispatch.h"       // Runtime dispatch
44: #include "codec_file/file_codec_concept.h"      // FileCodec concept + sentinels
45: #include "codec_file/file_codec_stream001.h"    // Stream-raw file codec
46: #include "codec_file/file_codec_stream_lz4_001.h" // Stream-LZ4 file codec
47: #include "codec_file/file_codec_packet001.h"    // Packet-raw file codec
48: #include "codec_file/file_codec_packet_lz4_001.h" // Packet-LZ4 file codec
49: #ifdef BCSV_HAS_BATCH_CODEC
50: #include "codec_file/file_codec_packet_lz4_batch001.h" // Batch-LZ4 async file codec
51: #endif
52: #include "codec_file/file_codec_dispatch.h"     // FileCodec runtime dispatch
53: #include "row_visitors.h"  // Row visitor pattern concepts and helpers
54: #include "writer.h"
55: #include "reader_concept.h"          // ReaderConcept
56: #include "writer_concept.h"          // WriterConcept
57: #include "csv_writer.h"              // CsvWriter
58: #include "csv_reader.h"              // CsvReader
59: 
60: // Include implementations
61: #include "bcsv.hpp"
62: #include "column_name_index.hpp"
63: #include "file_header.hpp"
64: #include "layout.hpp"
65: #include "reader.hpp"
66: #include "row.hpp"
67: #include "codec_row/row_codec_flat001.hpp"      // Flat001 codec implementation
68: #include "codec_row/row_codec_zoh001.hpp"       // ZoH001 codec implementation
69: #include "codec_row/row_codec_delta002.hpp"     // Delta row codec implementation
70: #include "writer.hpp"
71: #include "csv_writer.hpp"         // CsvWriter implementation
72: #include "csv_reader.hpp"         // CsvReader implementation

(End of file - total 72 lines)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 4.5s)

_Thinking:_

Let me quickly check the visitConst implementation to see how visitors work for efficient per-column access.






**Tool: grep**

**Input:**
```json
{
  "path": "/home/tobias/ws/bcsv/include/bcsv/row.hpp",
  "pattern": "void Row::visitConst"
}
```

**Output:**
```
Found 3 matches
/home/tobias/ws/bcsv/include/bcsv/row.hpp:
  Line 443:     BCSV_ALWAYS_INLINE void Row::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {

  Line 509:     inline void Row::visitConst(Visitor&& visitor) const {

  Line 652:     inline void Row::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {

```

---

## Assistant (Plan · Qwen3.6-27B Dense · 5.0s)

_Thinking:_

Now I have enough understanding. Let me also check how the file header is read, as well as the existing tests for reference.






**Tool: read**

**Input:**
```json
{
  "offset": 430,
  "limit": 100,
  "filePath": "/home/tobias/ws/bcsv/include/bcsv/row.hpp"
}
```

**Output:**
```
<path>/home/tobias/ws/bcsv/include/bcsv/row.hpp</path>
<type>file</type>
<content>
430:     }
431: 
432:     // =========================================================================
433:     // Visit (const)
434:     // =========================================================================
435: 
436:     // Aliasing safety: The reinterpret_cast reads in visitConst() and the
437:     // reinterpret_cast read-modify-write in visit() are well-defined because
438:     // typed objects have been implicitly created at these offsets by prior
439:     // memcpy (codec deserialization / set<T>()) or memset (clear()).
440:     // See file-level documentation for the full P0593R6 rationale.
441: 
442:     template<RowVisitorConst Visitor>
443:     BCSV_ALWAYS_INLINE void Row::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
444:         if (count == 0) 
445:             return;
446:         
447:         size_t endIndex = startIndex + count;
448:         if constexpr (RANGE_CHECKING) {
449:             if (endIndex > layout_.columnCount()) {
450:                 throw std::out_of_range("Row::visit: Invalid start index " + std::to_string(startIndex));
451:             }
452:         } else {
453:             assert(endIndex <= layout_.columnCount() && "Row::visit: Start index out of bounds");
454:         }
455:         
456:         const ColumnType* types   = layout_.columnTypes().data();
457:         const uint32_t*   offsets = layout_.columnOffsets().data();
458: 
459:         for (size_t i = startIndex; i < endIndex; ++i) {
460:             const ColumnType type = types[i];
461:             const uint32_t offset = offsets[i];
462:             
463:             switch(type) {
464:                 case ColumnType::BOOL: {
465:                     bool val = bits_[offset];
466:                     visitor(i, val);
467:                     break;
468:                 }
469:                 case ColumnType::INT8:
470:                     visitor(i, *reinterpret_cast<const int8_t*>(&data_[offset]));
471:                     break;
472:                 case ColumnType::INT16:
473:                     visitor(i, *reinterpret_cast<const int16_t*>(&data_[offset]));
474:                     break;
475:                 case ColumnType::INT32:
476:                     visitor(i, *reinterpret_cast<const int32_t*>(&data_[offset]));
477:                     break;
478:                 case ColumnType::INT64:
479:                     visitor(i, *reinterpret_cast<const int64_t*>(&data_[offset]));
480:                     break;
481:                 case ColumnType::UINT8:
482:                     visitor(i, *reinterpret_cast<const uint8_t*>(&data_[offset]));
483:                     break;
484:                 case ColumnType::UINT16:
485:                     visitor(i, *reinterpret_cast<const uint16_t*>(&data_[offset]));
486:                     break;
487:                 case ColumnType::UINT32:
488:                     visitor(i, *reinterpret_cast<const uint32_t*>(&data_[offset]));
489:                     break;
490:                 case ColumnType::UINT64:
491:                     visitor(i, *reinterpret_cast<const uint64_t*>(&data_[offset]));
492:                     break;
493:                 case ColumnType::FLOAT:
494:                     visitor(i, *reinterpret_cast<const float*>(&data_[offset]));
495:                     break;
496:                 case ColumnType::DOUBLE:
497:                     visitor(i, *reinterpret_cast<const double*>(&data_[offset]));
498:                     break;
499:                 case ColumnType::STRING:
500:                     visitor(i, strg_[offset]);
501:                     break;
502:                 default: [[unlikely]]
503:                     throw std::runtime_error("Row::visit() unsupported column type");
504:             }
505:         }
506:     }
507: 
508:     template<RowVisitorConst Visitor>
509:     inline void Row::visitConst(Visitor&& visitor) const {
510:         visitConst(0, std::forward<Visitor>(visitor), layout_.columnCount());
511:     }
512: 
513:     // =========================================================================
514:     // Visit (mutable)
515:     // =========================================================================
516: 
517:     template<RowVisitor Visitor>
518:     BCSV_ALWAYS_INLINE void Row::visit(size_t startIndex, Visitor&& visitor, size_t count) {
519:         if (count == 0) return;
520:         
521:         size_t endIndex = startIndex + count;
522:         
523:         if constexpr (RANGE_CHECKING) {
524:             if (endIndex > layout_.columnCount()) {
525:                 throw std::out_of_range("Row::visit: Invalid start index " + std::to_string(startIndex));
526:             }
527:         } else {
528:             assert(endIndex <= layout_.columnCount() && "Row::visit: Start index out of bounds");
529:         }

(Showing lines 430-529 of 1239. Use offset=530 to continue.)
</content>
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 467.3s)

_Thinking:_

Now I have a comprehensive understanding of the project. Let me summarize what I've learned and formulate clarifying questions, then present my plan.

Key findings:
1. The project has 9 CLI tools, all in `src/tools/`, following consistent patterns
2. Types are defined in `definitions.h`: BOOL, UINT8-64, INT8-64, FLOAT, DOUBLE, STRING
3. Reader/Writer use a streaming row-by-row model, never load entire file
4. Row has visitor pattern with visit/visitConst that dispatches per column type
5. Layout stores column names and types, can be modified
6. File codecs: stream, stream_lz4, packet, packet_lz4, packet_lz4_batch
7. Row codecs: flat, zoh, delta
8. CLI tools use manual arg parsing, no external library
9. cli_common.h provides shared utilities and withWriter dispatch

Now, the request is for bcsvNarrowType which:
1. Scans a BCSV file to find columns that could be narrowed
2. Reports findings to user
3. Converts file to new layout with narrower types
4. Preserves same encoding (file codec + row codec)
5. Supports output file, overwrite, and in-place modes

Let me think about the design:

### Type Narrowing Rules
For integer columns:
- INT64 → check if all values fit in UINT64 (≥0) → UINT32 → UINT16 → UINT8
- INT64 → check if all values fit in INT32 → INT16 → INT8
- Prefer unsigned over signed when all values are non-negative

For float columns:
- DOUBLE → FLOAT if no precision loss (static_cast<double>(static_cast<float>(v)) == v for all v)
- FLOAT/DOUBLE → integer types if all values are whole numbers and fit in the integer range

For bool:
- INT* columns where all values are 0 or 1 → BOOL

### Two-Pass Approach (required)
Pass 1: Scan all rows, track min/max and special properties per column
Pass 2: If narrowing possible, read all rows and convert to new layout

### Key Design Decisions to Discuss

1. **How to handle the second pass when types changed?**
   - The Row object is tied to a Layout. We need two Row+Layout pairs.
   - Read from source layout into source Row, then convert each cell to the target type and write to target Row.
   - Since types can differ between source and target, we need type coercion.

2. **Performance considerations:**
   - Single-pass scan accumulating min/max per column is cache-friendly
   - Second pass requires converting each value to the narrower type
   - We should reuse the same Row objects between iterations to avoid allocation

3. **Encoding preservation:**
   - Same file codec, same row codec, same compression level/block size
   - The Writer's template parameter is the row codec, which is fixed at compile time
   - We use `withWriter` dispatch from cli_common.h

4. **In-place handling:**
   - Since types change, the file layout changes, so we can't truly do in-place like bcsvRepair
   - We'd need to write to a temp file and rename, or write to a temp file and copy back
   - This is different from bcsvRepair which preserves the layout

5. **Dry-run / analysis-only mode:**
   - Should be the default or first thing we offer - scan and report without converting

Now let me think about potential issues and ask clarifying questions.

### Questions to Ask
1. Should the tool also consider narrowing STRING columns? The user didn't mention this.
2. For float-to-int conversion: if a DOUBLE column contains values like 1.0, 2.0, 3.0, should we convert to integer? The user mentioned "if float or double columns only contain integers, convert this column to integers". So yes.
3. Should we consider the reverse direction? What if a column could go from INT64 to UINT8, but UINT8 range is smaller - is the preference always toward the smallest type, or should we consider some tradeoff?
4. For in-place: since the layout changes, should we write to a temp file and atomically rename?
5. What about validation before conversion - should we round-trip verify?
6. Should we support a --dry-run or --analyze mode that only reports findings without converting?

Actually, I think #5 and #6 are clear from the description - "we inform the user, on the finding and convert the file to the new layout". So default behavior is: scan, report, then ask for confirmation or auto-convert.

Let me also consider: the file could be huge. We scan once (pass 1), determine optimal types, then convert (pass 2). Both passes stream row-by-row, which is memory efficient.

For the type coercion in pass 2, I need to handle all cross-type conversions. The existing `Row::get(index, T& dst)` already supports flexible type conversions, but I need to verify it handles all cases.

Actually, looking more carefully, the best approach for the second pass would be:
- For each source column with a new narrower type, read the value, verify it fits, write to the target row.

Let me structure my questions and plan.




I have a thorough understanding of the project. Before presenting the plan, I have a few clarifying questions:



**Tool: question**

**Input:**
```json
{
  "questions": [
    {
      "question": "When converting float/double to integer, should we verify the round-trip (e.g., static_cast<double>(narrowed_int) == original_double) for every value, or just check min/max range + all-whole-numbers?",
      "header": "Float→Int safety",
      "options": [
        {
          "label": "Full round-trip verification",
          "description": "Every value round-trips exactly. Safer but slower second pass."
        },
        {
          "label": "Range + whole-number check",
          "description": "Check during scan phase: all values are whole numbers AND fit in target int range. Faster, but edge case: a value like 1.9999999 might be considered \"whole\" due to precision."
        }
      ],
      "multiple": false
    },
    {
      "question": "For the default behavior: should the tool scan-and-report (dry-run), or should it scan-report-convert in one go?",
      "header": "Default behavior",
      "options": [
        {
          "label": "Scan + report only (Recommended)",
          "description": "Default is --analyze mode. User must add --convert to actually write the file."
        },
        {
          "label": "Scan + report + convert",
          "description": "Default converts. Add --dry-run to skip conversion."
        }
      ],
      "multiple": false
    },
    {
      "question": "Since layout changes prevent true in-place editing, which approach do you prefer for --in-place?",
      "header": "In-place strategy",
      "options": [
        {
          "label": "Temp + rename (Recommended)",
          "description": "Write to temp file in same directory, then atomically rename over original."
        },
        {
          "label": "Temp + remove original",
          "description": "Write to temp, remove original, rename temp."
        },
        {
          "label": "No in-place support",
          "description": "Omit --in-place entirely. Only support -o output or overwrite existing file with -f."
        }
      ],
      "multiple": false
    },
    {
      "question": "Should we also attempt to narrow STRING columns (e.g., detect fixed-width, all-ASCII, etc.) or focus only on numeric/bool types?",
      "header": "String columns",
      "options": [
        {
          "label": "Numeric + bool only (Recommended)",
          "description": "BCSV strings are already VLE-encoded. No meaningful narrowing."
        },
        {
          "label": "Include strings",
          "description": "Attempt string optimizations too."
        }
      ],
      "multiple": false
    }
  ]
}
```

**Output:**
```
User has answered your questions: "When converting float/double to integer, should we verify the round-trip (e.g., static_cast<double>(narrowed_int) == original_double) for every value, or just check min/max range + all-whole-numbers?"="Full round-trip verification", "For the default behavior: should the tool scan-and-report (dry-run), or should it scan-report-convert in one go?"="Scan + report only (Recommended)", "Since layout changes prevent true in-place editing, which approach do you prefer for --in-place?"="Temp + rename (Recommended)", "Should we also attempt to narrow STRING columns (e.g., detect fixed-width, all-ASCII, etc.) or focus only on numeric/bool types?"="Include strings". You can now continue with the user's answers in mind.
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 233.2s)

_Thinking:_

Important answers:
1. **Full round-trip verification** for float/double → integer conversions (not just range checks)
2. **Scan + report only** as default (--convert flag to actually write)
3. **Temp + rename** for in-place
4. **Include strings** in narrowing analysis

Let me think about string narrowing. In BCSV, strings use VLE encoding with uint16_t length prefix. Possible string "narrowing" could be:
- Not really applicable since strings are already efficiently encoded
- The user says "include strings" but there's not much to narrow... maybe detect if a string column could be reduced? Actually strings don't have types like int8 vs int64. They're stored as VLE + bytes.

Actually wait - maybe the user means: if a string column contains only short strings, we could... no, that doesn't change the wire format. Strings in BCSV are already optimal - each string is stored as length-prefix + raw bytes. There's no "narrower" string type.


I think the user likely just wants strings analyzed for completeness—checking that all values round-trip correctly and that no string exceeds MAX_STRING_LENGTH (65535 bytes)—so they're included in the analysis report even if no meaningful narrowing is possible. Let me move forward with the implementation plan.

I'm structuring this as a two-phase tool: first, I scan the file to compute min/max values and track type properties for each column, then report findings. Only if conversions are needed and the user specifies `--convert` do I do a second pass to actually rewrite the file. The CLI will support input/output files, in-place modification via temp+rename, dry-run analysis, JSON output, verbose logging, and standard help options.

For the analysis phase, I track min/max for integers (sign-aware), full round-trip verification for floats converting to integers or narrower floats, and bool detection for integer columns. For the conversion phase, I read source rows and write target rows with coercions using the flexible getter approach.

Now I'm weighing implementation strategies. The most performant option uses the existing Visitor pattern with a specialized visitor that accumulates type statistics, though it runs a runtime switch per column per row. The fastest approach would be type-specific visitors using `visit<T>()` for homogeneous columns with zero overhead, but requires knowing column types upfront and tracking which columns are homogeneous. There's also the raw pointer access route through the data buffer directly, which sidesteps the visitor pattern entirely. , type is the column type (runtime), not compile-time known. So `visit<T>()` won't work because T must be compile-time known.

Actually, I can do better. I know all the column types from the layout (compile-time... well, runtime from file header, but then fixed during the scan). I can:
1. Group columns by type (e.g., all INT64 columns at indices 2,5,8,11)
2. For each type group, use typed iteration directly on the row storage

But the typed visit<T>() requires consecutive columns and compile-time T known. Since we know the column types after reading the file header, we can organize the scan loop by grouping columns by type.

Actually, the simplest approach that's still efficient: in the scan loop, for each row, iterate by type groups. First handle all BOOL columns via the bits_ directly, then handle scalars in a type-sorted order, then handle STRING columns. This is how the codecs work internally.

But we're working with the user-facing Row API. The visit/visitConst already uses a switch-per-type dispatch, which the compiler can optimize. Good enough for a scan pass. The second pass (conversion) has to do type coercions anyway, so the visit/visitConst + set() approach is fine.

Performance should be O(N) where N is the number of rows with constant memory per row. File I/O will dominate, so the per-row overhead is minimal even with the switch.

Now I'm laying out the file structure: bcsvNarrowType.cpp for the CLI, with CMakeLists.txt updated, CLI_TOOLS.md docs updated, and unit tests for the logic. But the user is also asking to include strings in the analysis, and I need to think through what string-specific optimizations actually make sense.

Strings already use VLE encoding in the wire format, so there's no "narrower" type like with numerics. I could report string length statistics (min, max, average) to give users insight into storage characteristics, though that's informational rather than actionable for optimization. I'll include this in the implementation and then finalize the plan.




Thanks for the answers. Two quick follow-ups before I finalize the plan:



**Tool: question**

**Input:**
```json
{
  "questions": [
    {
      "header": "Round-trip scope",
      "question": "Full round-trip verification requires re-reading every value. For the scan phase, should we track min/max/whole-number flag during scan, then do a THIRD pass to verify round-trips? Or do full round-trip checks inline during the scan (slower scan, but fewer passes)?",
      "options": [
        {
          "label": "Inline during scan (Recommended)",
          "description": "One pass, checks each value against its target type on the fly. Scan is slower but total pass count stays at 1+1=2."
        },
        {
          "label": "Separate verification pass",
          "description": "Fast scan (min/max/flags), then a separate pass if conversion involves type-changing columns. Up to 3 passes total."
        }
      ],
      "multiple": false
    },
    {
      "header": "String analysis",
      "question": "For string columns: BCSV strings are already VLE-encoded with no 'narrower' type. What would you like the string analysis to report?",
      "options": [
        {
          "label": "Length statistics (Recommended)",
          "description": "Report min/max/avg string length per column. No narrowing possible, but provides visibility into string storage."
        },
        {
          "label": "Skip strings",
          "description": "Report '(no narrowing possible)' for string columns and move on."
        }
      ],
      "multiple": false
    }
  ]
}
```

**Output:**
```
User has answered your questions: "Full round-trip verification requires re-reading every value. For the scan phase, should we track min/max/whole-number flag during scan, then do a THIRD pass to verify round-trips? Or do full round-trip checks inline during the scan (slower scan, but fewer passes)?"="Inline during scan (Recommended)", "For string columns: BCSV strings are already VLE-encoded with no 'narrower' type. What would you like the string analysis to report?"="Try to convert string into a number. If it is a number apply the usualy methods to identifiy what variable type is required. Usecase: somebody accidentially wrote numerical values into a string column. we want to catch and compress these cases.". You can now continue with the user's answers in mind.
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 166.0s)

_Thinking:_

The user wants string columns to be analyzed as potential numeric conversions. This is an interesting case - if all values in a string column are parseable as numbers, convert the column type to the appropriate numeric type.

Now I have all the information needed for the plan. Let me present it.




Good. Now I have everything I need. Here's the plan.

---

## bcsvNarrowType — Implementation Plan

### 1. Overview

A two-phase streaming tool:

| Phase | Description | Passes |
|-------|-------------|--------|
| **Analyze** (default) | Scan file, report optimal types | 1 |
| **Convert** (`--convert`) | Rewrite file with narrower types | 2 (re-scan + write) |

Single `Row` object reused across iterations (no per-row allocations after scan starts). Same file codec, row codec, compression level, and block size are preserved.

### 2. CLI Interface

```
bcsvNarrowType [OPTIONS] INPUT_FILE

Scan BCSV file for type-narrowing opportunities and optionally convert.

Arguments:
  INPUT_FILE         Input BCSV file

Output (one of, in priority order):
  -o, --output FILE  Write converted file to new location (default when --convert)
  -f, --overwrite    Overwrite INPUT_FILE (writes to temp, then renames)
  --in-place         Alias for -f (temp-file + atomic rename)

Options:
  --analyze          Scan-only mode (default, explicit alias)
  --convert          Analyze + rewrite file with narrower types
  -v, --verbose      Per-column findings and progress
  --json             Machine-readable JSON output to stdout
  --force            Skip confirmation prompt for destructive operations
  -h, --help         Show help message

Exit codes:
  0  Success (analysis complete, or conversion done)
  1  Error (invalid file, conversion failed)
  2  Argument error
```

**Default behavior**: scan-only (analyze), print findings, exit 0.

**Conversion output**: if `--convert` without `-o`, writes to `INPUT-narrowed.bcsv` (same directory). `-f`/`--in-place` writes to temp file, then renames over the original.

### 3. Type Narrowing Rules

#### Integer columns (INT8/16/32/64, UINT8/16/32/64)

For each column, track during scan: `min`, `max`, `all_zero_or_one`.

Narrowing order (greedy, smallest-first):
1. If `all_zero_or_one` and row count > 0 → **BOOL**
2. If `min >= 0` → try unsigned: UINT8 ≤ UINT16 ≤ UINT32 ≤ UINT64
3. If `min < 0` → try signed: INT8 ≤ INT16 ≤ INT32 ≤ INT64
4. If `min >= 0` and unsigned fits, prefer unsigned. If `min < 0` and signed range fits better than unsigned, prefer signed.
5. Keep original type if nothing narrower fits.

#### Floating-point columns (FLOAT, DOUBLE)

Track during scan: `min`, `max`, `all_whole_number`, `all_float_roundtrips`, `all_int_roundtrips`.

Narrowing order:
1. **DOUBLE → FLOAT**: if `static_cast<double>(static_cast<float>(v)) == v` for every value.
2. **FLOAT/DOUBLE → integer**: if all values are whole numbers (`v == floor(v)`) AND all values round-trip through the target integer type (`static_cast<double>(static_cast<Target>(v)) == v` for every value).

For float→int, try in order: BOOL (if all 0/1), UINT8, INT8, UINT16, INT16, UINT32, INT32, UINT64, INT64.

#### Bool columns (BOOL)

Already minimal. Report "no narrowing possible".

#### String columns (STRING)

Track during scan: min/max/avg length, `all_numeric`, `all_bool`.

If ALL values are parseable as numbers:
- Parse each value, track min/max/whole-number/etc. as above.
- Apply the same narrowing rules to determine the best numeric type.
- This catches accidental numeric data stored as strings.

If ALL values are parseable as booleans ("true"/"false"/"0"/"1"/"yes"/"no", case-insensitive):
- Convert to BOOL.

Otherwise: report length statistics, no narrowing.

#### Inline round-trip verification

Every value in every candidate column is checked during the scan against any proposed narrower type. This is ONE pass, not three. The per-value overhead is a comparison operation (cheap). The alternative of a third pass would require re-parsing the entire file.

### 4. Data Structures

```cpp
// Per-column scan state
struct ColumnAnalysis {
    size_t col_index;
    ColumnType original_type;
    ColumnType optimal_type;  // best type found
    bool   convertible;       // true if optimal_type != original_type
    
    // Integer tracking
    int64_t  min_int = INT64_MAX;
    uint64_t max_uint = 0;
    bool     all_zero_or_one = true;
    
    // Float tracking
    double   min_f64 = std::numeric_limits<double>::max();
    double   max_f64 = std::numeric_limits<double>::lowest();
    bool     all_whole = true;
    bool     all_float_roundtrip = true;  // double→float→double survives
    
    // Int-from-float tracking
    bool     all_int8_roundtrip  = true;
    bool     all_int16_roundtrip = true;
    bool     all_int32_roundtrip = true;
    bool     all_int64_roundtrip = true;
    // ... similar for uint variants
    
    // String tracking
    size_t   min_str_len = SIZE_MAX;
    size_t   max_str_len = 0;
    size_t   str_total_len = 0;
    bool     all_numeric = true;
    bool     all_bool_str = true;
    
    // Violation tracking (for reporting)
    size_t   violation_count = 0;  // rows that fail a proposed narrowing
    
    size_t   row_count = 0;  // total rows processed
};
```

### 5. Scan Phase Algorithm

```
For each column:
  Determine column type from Layout
  
While reader.readNext():
  For each column i:
    switch(type_i):
      INT*/UINT*: record min/max, check all_zero_or_one
      FLOAT:    cast to double, record min/max, check whole, check float roundtrip, check int roundtrips
      DOUBLE:   record min/max, check whole, check float roundtrip, check int roundtrips
      BOOL:     increment row_count
      STRING:   attempt numeric parse, if successful track as numeric, else all_numeric=false

After scan: For each column, determine optimal type from tracked statistics
```

The visitor pattern (`row.visitConst`) dispatches efficiently with a single `switch` per column per row. The compiler can devirtualize the lambda calls.

### 6. Conversion Phase Algorithm

If `--convert` and `convertible` columns exist:

```
1. Build new Layout from original column names + optimal types
2. Detect source encoding (file codec, row codec, compression level, block size)
3. Open source file with Reader (auto-detected codec)
4. Open target file with Writer (same codec, new layout) via withWriter dispatch
5. Stream rows:
   
   While reader.readNext():
     For each column i:
       visitor reads value from source row
       if source type != target type:
         perform conversion (with round-trip assertion — should always succeed per scan)
       write converted value to target row
     writer.writeRow()
```

Type coercion during conversion uses `std::visit` or a switch on source type, with explicit casts to target type. Since we verified round-trips during scan, conversions are lossless by construction.

### 7. Output Format

**Text output (default):**
```
bcsvNarrowType: analysis of data.bcsv
Rows: 100000, Columns: 12

  Idx  Name             Original  →  Optimal      Savings
  ---  ----             --------     -------      -------
    0  timestamp        int64       int64          —       
    1  sensor_id        int64       uint16       87.5%   
    2  temperature      double      float        50.0%   
    3  humidity         float       float          —
    4  active           int64       uint8        87.5%   
    5  status_flag      int64       bool         98.8%
    6  label            string      int32       62.5%   (string→number conversion)
  ...

Total potential savings: 23% (1.23 MB → 947 KB)
Run with --convert to apply changes.
```

**JSON output (`--json`):**
```json
{
  "input_file": "data.bcsv",
  "rows": 100000,
  "columns": 12,
  "convertible_columns": 5,
  "original_size_bytes": 1234567,
  "estimated_new_size_bytes": 947000,
  "savings_pct": 23.0,
  "column_changes": [
    { "index": 1, "name": "sensor_id", "from": "int64", "to": "uint16", "savings_pct": 87.5 },
    ...
  ],
  "encoding": { "file_codec": "packet_lz4_batch", "row_codec": "delta", "compression_level": 1 }
}
```

### 8. In-Place / Overwrite

**`-f` / `--in-place` / `--overwrite`**:
1. Write to `INPUT_FILE.tmp.XXXXXX` (mkstemp-style, same filesystem for atomic rename)
2. On success: `std::filesystem::rename(tmp, original)` (atomic on POSIX)
3. On failure: delete temp file, report error

**`-o FILE`**: Write to specified path. Refuse to overwrite unless `--force`.

### 9. File Organization

Single file, following existing CLI tool pattern:

```
src/tools/bcsvNarrowType.cpp   — tool implementation (~800-1000 lines)
src/tools/CMakeLists.txt       — add entry
src/tools/CLI_TOOLS.md         — add documentation section
```

### 10. Approach Comparison

| Approach | Pros | Cons |
|----------|------|------|
| **Visitor-based** (recommended) | Leverages existing `visitConst`/`visit` infrastructure. Single switch per column, compiler optimizes inner loop. | Generic lambda with `if constexpr` chains — slightly more template instantiation |
| **Per-type visitor** (`visitConst<T>`) | Type-specific inner loop, no runtime switch. | Requires knowing type at compile time; can't mix types. Would need one visitor per type family per call. Overhead for dynamic layout. |
| **Variant + std::visit** | Flexible, type-safe. | Allocates variant per cell per row if not careful. Slower than direct reinterpret_cast. |
| **Raw byte copying** | Fastest possible. | Only works for same-type or compatible-endian conversions. Can't handle cross-type (double→float) or string→number. |

**Recommendation**: Use the generic visitor pattern (`visitConst` with `if constexpr` chains). It matches the existing codebase (`csv2bcsv.cpp:647-649` already does this), the compiler effectively devirtualizes the dispatch, and it handles all cross-type conversions naturally.

### 11. Testing Strategy

1. **Unit tests** (`tests/test_narrow_type.cpp`):
   - int64 → uint8, int64 → bool, int64 → int32
   - double → float, double → int32, float → int16
   - string → int32, string → bool
   - No-op cases (already optimal)
   - Mixed file with convertible and non-convertible columns

2. **Integration tests** (shell scripts):
   - End-to-end: generate, narrow, verify with bcsvCompare
   - JSON output parsing
   - In-place overwrite
   - Error cases (empty file, no convertible columns)

3. **Correctness verification**:
   - After conversion, `bcsvCompare --mode value original.bcsv narrowed.bcsv` must return 0 (identical values).

### 12. Performance Considerations

- **No deep copies**: Reuse Row object between iterations. Only `std::string` values for string columns trigger allocation, which is unavoidable.
- **Cache-friendly**: The visitor pattern accesses `data_` sequentially per column type group, same as the codec serialization.
- **Single scan**: Inline round-trip verification means one pass for analysis, not three.
- **Streaming**: Never loads more than one row into memory at a time.
- **Estimating size**: Use `Layout::Data::computeOffsets` to compute per-row serialized size, multiply by row count for estimate.

### 13. Implementation Order

1. `src/tools/bcsvNarrowType.cpp` — scaffold: CLI parsing, file opening, scan loop, visitor, type detection
2. Wire up round-trip verification in the scan visitor
3. Conversion phase: new layout construction, read+convert+write loop, type coercion
4. Output formatting: text table + JSON
5. In-place / overwrite handling
6. CMakeLists.txt + CLI_TOOLS.md updates
7. Tests

---

Does this plan look right, or are there aspects you'd like me to reconsider before I start implementation?

---

## User

looks good. but I don't understand why we should use json outputs. simple user interaction on the terminal should be fine. explain if there is any benefit on the json outputs. than consider performance (disucss), with respect to what hyptothesis we need to check for each value. I would consider that we only need to compare against the widest type already identifed for that columne. Consider an int32 column. if we have identified an int16 already some rows before, we don't need to probe for uint8 or int8. We already now that we have to consider signed values of that size. Thus ratching the type up. Same on a string column. if we recognize one value that cannot be converted, no need to continue scanning this column for the comming rows, it is going to stay string. Thus when the maximum type required, hits the current type used, we can stop scanning this column. if we don't need to scan any column, we can stop scanning the file. Understood? 

---

## Assistant (Plan · Qwen3.6-27B Dense · 30.5s)

_Thinking:_

The user is making two important points:

1. **JSON output**: Questions whether it's needed - agree that for a CLI tool, simple terminal output is sufficient.

2. **Performance optimization**: A very clever observation about early termination of per-column scanning. The key insight is:
   - Track the "widest type needed so far" for each column
   - If any value forces the type to widen (ratchet up), update the required type
   - Once the required type equals the current column type, stop scanning that column entirely
   - When ALL columns are in that "stop scanning" state, we can stop scanning the file entirely
   - For string columns: once a single non-numeric value is found, it stays string - no more parsing needed

This is a significant performance optimization that could avoid reading entire files when types don't need narrowing.

---

## User

what are your thoughts? I don't see any response

---

## Assistant (Plan · Qwen3.6-27B Dense · 97.0s)

_Thinking:_

The user wants me to:
1. Explain why JSON output might not be needed (they're skeptical of it)
2. Significantly improve the performance model through early termination/short-circuiting of column analysis
3. Present the updated plan

Let me think through their performance observations carefully:

**Key insight**: Instead of tracking all possible narrowing hypotheses for every column across every row, we should maintain a single "current best type" per column and only verify against that. When the current best matches the original type, the column is done. When all columns are done, the entire scan terminates early.

Let me work through the logic:

**Integer columns (originally INT32):**
- Start with hypothesis = original type (INT32)
- For each value, try to narrow: if value fits in INT16, hypothesis = INT16; if fits INT8, hypothesis = INT16... wait, that's not right.


- We need to track min/max values to determine if narrowing is possible

The current approach of tracking min/max for integers is fine - that doesn't require per-value hypothesis checking. The question is about floats and strings. For floats, we can still track min/max and maintain flags for all_float_roundtrip and all_int_X_roundtrip, but once those flags become false, we could stop checking that particular hypothesis.

Actually, let me think about this more carefully with the user's idea: for integer columns, we maintain a `current_type` starting at the original, and as we see min/max values, we narrow it to the smallest type that fits. Once `current_type` reaches the original type and can't narrow further, that column is done. For float columns, similarly narrow from FLOAT down to the smallest integer type that all values fit into, or stop if we can't. For string columns, track whether all values are numeric or all are boolean, and once we find a counterexample, that hypothesis is dead.

The real optimization is tracking which hypotheses are "alive" per column and stopping the scan entirely once all columns have no remaining live hypotheses. This means on every row we check if all columns are done, and if so we break early from the file scan. The JSON output question is separate—I can probably drop that to simplify the CLI and just use a human-readable table format.




Good points. Let me think through both.

### On JSON Output

You're right to question it. Looking at the existing tools: only `bcsvValidate` and `bcsvRepair` have `--json`, and only for CI/automation pipelines. For a file-optimization tool the user almost always wants:
1. A clear table of what can be narrowed
2. A total savings estimate
3. Then they say `--convert` and it's done

JSON adds a flag but no value for interactive use. **I'll drop it.**

---

### On Performance — Early Termination

Your observation is sharp. Let me work through the implications:

**Integer column (originally INT32):**
- Maintain a single `current_type`, initialized to INT32
- Track min/max as before
- After each value, check: does min/max fit a narrower type than `current_type`? If yes, narrow.
- If `current_type` is already the narrowest possible (e.g., INT8) or can't narrow further, the column is **done** — skip it on all subsequent rows.

**Float/Double column:**
- Similar ratchet: start hypothesizing from the original type downward (DOUBLE→FLOAT→INT64→INT32→...). Once a value disproves a hypothesis, eliminate it permanently. No need to test UINT8, INT8, etc. if we already know INT32 is the ceiling.
- If `current_type` reaches the original type, the column is **done**.

**String column:**
- This is the big win. Currently we'd parse every string in every row. With ratcheting: as soon as we hit one value that can't parse as a number, `all_numeric` flips false permanently. One more bad value kills `all_bool_str`. Column is **done** — remaining rows skip the column entirely.

**Global early exit:**
- Maintain `alive_columns` count. When a column finishes, decrement.
- When `alive_columns == 0`, **break out of the file scan entirely**. We don't need to read further rows.

The practical impact: on a file with 1M rows where most columns stabilize within the first few thousand rows, we could avoid reading 95%+ of the file. And with the streaming Reader we'd just close early — no penalty.

**But there's a subtlety I want to flag**: the `all_whole`, `all_float_roundtrip`, and `all_int_X_roundtrip` flags are independent. A value can disprove `all_float_roundtrip` (so DOUBLE→FLOAT is dead) but still leave the path to INT32 alive. So the "ratchet" isn't a single type — it's the current set of alive hypotheses. However, we can structure it as an ordered ladder: check the narrower type first, and once any hypothesis at that level dies, move to the next. When the ladder reaches the original type, the column is done regardless of which hypothesis survives.

**Proposed structure** — per column, a `ColumnProbeState`:

```
state: ALIVE | DONE
current_type: starts at original_type  (the narrowest type admitted so far)

For INT columns:
  min_val, max_val → directly determines narrowest integer type
  Ratchet: each row tightens min/max; once current_type == original_type, DONE

For FLOAT/DOUBLE columns:
  Track: current_type, and independent "alive" flags:
    alive_float      (if DOUBLE → can we go to float?)
    alive_int_ladder (if current int hypothesis is wider than current_type, keep probing)
  Ratchet: 
    - If alive_float and value doesn't round-trip → kill alive_float
    - If alive_int and value isn't whole → kill entire int ladder
    - If alive_int and value's round-trip fails for current int type → widen int type
      (e.g., int8 → int16). If widened type == original_type → DONE
    - Once all alive flags killed and current_type == original_type → DONE

For STRING columns:
  alive_numeric, alive_bool_str (both start true)
  Ratchet:
    - fail to parse as number → kill alive_numeric
    - fail to parse as bool   → kill alive_bool_str
    - once both dead → DONE (immediately, first non-numeric string kills both)
```

This means for a STRING column where the first row has `"hello"`, we kill both hypotheses and mark DONE after **one row**. For an INT64 column where values stay in UINT8 range for 10k rows, then one row has 40000 — we widen to UINT16. No rewind needed because min/max captures everything.

**One catch**: for float→int conversion, we can't ratchet up from int8 to int16 purely from min/max on a DOUBLE column, because the min/max are doubles. But we can check: does `min >= INT8_MIN && max <= INT8_MAX`? If a later row violates this, widen to INT16 check, etc. The min/max are monotonically widening, so we never miss a value.

**Net effect**: The visitor per row checks `is_done[col]` as the first gate. Once alive columns drop to zero, the entire while-loop breaks. This is a genuinely large optimization for realistic files where type signatures stabilize quickly.

---

Does this match your thinking? Should I update the plan with the ratchet logic and drop JSON, or do you want to adjust the hypothesis tracking further?

---

## User

alright. please provide a full summary of the plan. what is the intend. what is the specified tool behaviour. how you plan to implement it and what are the key technical approches and why. I want to pass that response to a 3rd-party to review and comment. thus be precise.

---

## Assistant (Plan · Qwen3.6-27B Dense · 251.0s)

_Thinking:_

The user wants a comprehensive, precise plan document that they can pass to a 3rd party for review. Let me synthesize everything we've discussed - including the early termination/ratcheting optimization - into a clean, self-contained specification.




---

## bcsvNarrowType — Specification & Implementation Plan

### 1. Intent

`bcsvNarrowType` is a CLI tool that scans a BCSV file and determines whether any column can be stored in a narrower data type without losing information. When narrower types are found and the user requests conversion, the tool rewrites the file with the same number of columns, same column names, same encoding (file codec, row codec, compression), but reduced per-column types — yielding a smaller file.

Example: an `int64` column whose values range `[0..200]` becomes `uint8`. A `double` column whose values are all whole numbers in `[0..10000]` becomes `uint16`. A `string` column containing only `"true"`/`"false"` becomes `bool`.

---

### 2. Specified Tool Behaviour

#### 2.1. Command-Line Interface

```
bcsvNarrowType [OPTIONS] INPUT_FILE

Scan BCSV file for type-narrowing opportunities and optionally convert.

Arguments:
  INPUT_FILE         Input BCSV file

Output:
  -o, --output FILE  Write converted file to new location
  -f, --overwrite    Overwrite INPUT_FILE (temp-file + atomic rename)
  --in-place         Alias for --overwrite

Options:
  --analyze          Scan-only, print findings (default, explicit alias)
  --convert          Scan + rewrite file with narrower types
  -v, --verbose      Per-column details and progress
  --force            Skip confirmation for overwrite operations
  -h, --help         Show help message

Exit codes:
  0  Success
  1  Error (invalid file, conversion failed)
  2  Argument error
```

**Default behaviour** (`bcsvNarrowType input.bcsv`): analyze-only mode. Prints a summary table to stderr and exits 0. No files are modified.

**Conversion** (`--convert`): requires either `-o FILE` (write new file) or `-f`/`--in-place` (overwrite original). With `--in-place`, writes to a temp file on the same filesystem, then atomically renames. On failure, the temp file is cleaned up.

#### 2.2. Output Format (Analyze Mode)

Printed to stderr. Terminal table showing:

```
bcsvNarrowType: analysis of data.bcsv
Rows scanned: 10000 (out of 100000, scan terminated early)
Columns: 12

  Idx  Name            Original     Optimal      Savings   Status
  ---  ----            --------     -------      -------   ------
    0  timestamp       int64        int64            —     unchanged
    1  sensor_id       int64         uint16       75.0%   narrower
    2  temperature     double        float        50.0%   narrower
    3  humidity        float         float          —     unchanged
    4  active          int64         bool         98.8%   narrower
    5  label           string        int32       62.5%   type changed
  ...

Original file:  1.23 MB
Estimated:       947 KB
Potential savings: 23.0%

Use --convert -o output.bcsv to apply, or --convert -f to overwrite.
```

If no column can be narrowed:
```
All 12 columns are already at their narrowest type.
No conversion possible.
```

#### 2.3. Type Narrowing Rules

**Integer columns** (INT8/16/32/64, UINT8/16/32/64):
- Track `min` and `max` across all scanned values.
- Narrowest candidate: if `min >= 0`, search unsigned ladder (UINT8 → UINT16 → UINT32 → UINT64); if `min < 0`, search signed ladder (INT8 → INT16 → INT32 → INT64).
- Special case: if all values are `0` or `1`, candidate is `bool`.
- Result is the narrowest type in the appropriate ladder that still contains `[min, max]`.

**Float columns** (FLOAT) and **Double columns** (DOUBLE):
- Track `min`, `max`, and per-value round-trip validity.
- Candidate types, checked in order:
  1. `bool` — if all values are exactly `0.0` or `1.0`.
  2. Smallest unsigned/signed integer — if all values are whole numbers (`v == floor(v)`) AND each round-trips: `static_cast<double>(static_cast<Target>(v)) == v`.
  3. `float` (only if original is `double`) — if `static_cast<double>(static_cast<float>(v)) == v` for every value.
- Result is the first candidate in the above order where all round-trip checks pass.

**Bool columns** (BOOL):
- Already minimal. No narrowing possible.

**String columns** (STRING):
- Attempt to parse every value as a number (integer or float, using the same rules as numeric columns).
- If ALL values parse successfully as numbers, treat the column as numeric and apply the above integer/float narrowing rules to find the best numeric type.
- If ALL values parse as boolean (`"true"`, `"false"`, `"0"`, `"1"`, case-insensitive), candidate is `bool`.
- If even one value fails to parse as a number or boolean, the column stays `string`.

#### 2.4. Encoding Preservation

The output file uses the **same** file codec (stream/packet/packet_lz4/packet_lz4_batch/stream_lz4), row codec (flat/zoh/delta), compression level, and block size as the input. Only the column types in the layout change.

---

### 3. Implementation Plan

#### 3.1. Source Layout

```
src/tools/bcsvNarrowType.cpp    — single-file tool (~900 lines)
src/tools/CMakeLists.txt         — add build target
src/tools/CLI_TOOLS.md           — add documentation section
tests/test_narrow_type.cpp       — unit tests (visitor logic + type detection)
tests/integration/test_narrow.sh — shell integration tests
```

No changes to the library (`include/bcsv/`). The tool uses only existing public APIs.

#### 3.2. Core Algorithm

**Two-phase streaming, single Row reuse, early termination:**

```
Phase 1 — SCAN (always runs):
  Open input file with Reader<Layout> (codec auto-detected)
  Initialize per-column ColumnProbeState (one per column)
  alive_count = column_count

  While reader.readNext() AND alive_count > 0:
    For each column i where probe[i].is_alive:
      probe[i].visit_value(reader.row().column(i))

    // After visiting all alive columns in this row:
    For each column i where probe[i].is_alive:
      if probe[i].has_stabilized():
        probe[i].is_alive = false
        alive_count -= 1

  For remaining alive columns:
    Determine optimal type from accumulated statistics

Phase 2 — CONVERT (only with --convert and convertible columns exist):
  Build new Layout: same names, optimal types
  Open source with Reader<Layout>, target with Writer<NewLayout>
  
  While reader.readNext():
    For each column i:
      read value from source row
      if types differ: coerce via explicit cast
      set value in writer row
    writer.writeRow()
```

A single `Row` object in each phase — no per-row allocation beyond the Row's internal buffers.

#### 3.3. ColumnProbeState — Per-Column Tracking

Each column has a state object that starts in `ALIVE` and transitions to `DONE` when no further scanning is useful.

```cpp
struct ColumnProbeState {
    size_t    col_index;
    ColumnType original_type;
    bool      alive = true;        // false = column done, skip in future rows
    bool      convertible = false; // true = optimal_type != original_type
    ColumnType optimal_type;       // narrowed type (starts = original_type)
    size_t    rows_probed = 0;

    // Integer tracking
    int64_t   min_int = INT64_MAX;
    int64_t   max_int = INT64_MIN;
    bool      int_all_zero_or_one = true;

    // Float/double tracking
    double    min_f = std::numeric_limits<double>::max();
    double    max_f = std::numeric_limits<double>::lowest();
    bool      f_all_whole = true;                    // v == floor(v)?
    bool      f_all_positive = true;                 // for unsigned ladder
    bool      f_to_float_ok = true;                  // double→float round-trips?

    // Integer-from-float: ordered ladder, widen on violation
    // Once a type is eliminated, it's gone forever.
    enum class IntLadder { INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64, DONE };
    IntLadder int_ladder_pos = IntLadder::INT8;      // signed ladder position
    IntLadder int_ladder_neg = IntLadder::INT8;      // unsigned ladder position
    bool      int_ladder_alive = true;               // whole-number path still viable

    // String tracking
    bool      str_all_numeric = true;                // every value parsed as number?
    bool      str_all_bool = true;                   // every value is "true"/"false"/etc.?
    size_t    str_min_len = SIZE_MAX;
    size_t    str_max_len = 0;
    size_t    str_total_len = 0;
    // If str_all_numeric, track same fields as float (str_min_f, str_max_f, etc.)
    double    str_min_f = std::numeric_limits<double>::max();
    double    str_max_f = std::numeric_limits<double>::lowest();
    bool      str_all_whole = true;
    bool      str_all_positive = true;
    bool      str_to_float_ok = true;
    IntLadder str_ladder_pos = IntLadder::INT8;
    IntLadder str_ladder_neg = IntLadder::INT8;
    bool      str_ladder_alive = true;
```

**Stabilization check** (`has_stabilized()` — called after each value):

- **Integer column**: if `optimal_type` (derived from current `[min, max]`) equals `original_type`, the column is done. No further value can widen beyond `original_type`.
- **Float/Double column**: if all candidate paths are dead (not whole numbers, not float-roundtrippable, int ladders both at `DONE`) OR `optimal_type == original_type`, done.
- **String column**: if `!str_all_numeric && !str_all_bool`, done immediately. If `str_all_numeric` and int ladders both at `DONE` and `!str_to_float_ok`, done.
- **Bool column**: done immediately (no narrowing possible).

When `alive_count` reaches zero across all columns, the entire while-loop breaks. The scanner never reads more rows than necessary.

#### 3.4. Ratchet Logic — How Narrowing Is Computed

**Integer columns:**

After each value, update `min_int`/`max_int`. Derive `optimal_type` by walking the appropriate ladder from narrowest to widest:

```
if min >= 0:
  UINT8 fits?  (max <= 255)        → optimal = UINT8
  UINT16 fits? (max <= 65535)      → optimal = UINT16
  UINT32 fits? (max <= 2^32-1)     → optimal = UINT32
  else                                   → optimal = UINT64
if min < 0:
  INT8 fits?  (min >= -128 && max <= 127)   → optimal = INT8
  INT16 fits? (min >= -32768 && max <= 32767) → optimal = INT16
  INT32 fits?                                → optimal = INT32
  else                                            → optimal = INT64
if all_zero_or_one: optimal = bool
```

Special: if the column was originally unsigned but `min` goes negative (impossible for an unsigned column), the signed ladder takes over. Since BCSV stores unsigned as unsigned, `min` is always >= 0 for UINT columns.

Ratchet: because `min` monotonically decreases and `max` monotonically increases, `optimal_type` can only widen (never narrow). Once `optimal_type == original_type`, the column stabilizes.

**Float/Double columns:**

After each value `v` (cast to `double`):

```
1. Track min_f, max_f
2. f_all_whole &&= (v == floor(v))
3. f_all_positive &&= (v >= 0)
4. f_to_float_ok &&= (static_cast<double>(static_cast<float>(v)) == v)

5. If f_all_whole:
     // Check which integer type in the ladder fits [min_f, max_f]
     // Walk signed ladder (prefer signed if v can be negative):
     ladder_pos: INT8 → INT16 → INT32 → INT64
     For each candidate, check every value round-trips:
       static_cast<double>(static_cast<T>(v)) == v
     If current candidate fails for this value, widen to next.
     Once at INT64 and still fails, int_ladder_alive = false.

     If f_all_positive, walk unsigned latch in parallel and take narrower of signed/unsigned.

6. optimal_type is the narrowest survivor among:
     bool (if all 0/1) < unsigned ladder < signed ladder < float (if double) < double
```

Each hypothesis (`f_all_whole`, `f_to_float_ok`, `int_ladder_alive`) only flips from `true` to `false`. Once the ladder position reaches the original type (or the int path dies and float dies), the column stabilizes.

**Key insight**: we do NOT maintain per-hypothesis violation counters or re-test eliminated types. A type is dead the moment a value disproves it. This makes `visit_value` for a row O(narrowing_candidates), not O(all_types).

#### 3.5. Conversion Phase — Type Coercion

When `--convert` is active, Phase 2 reads every row from the source and writes it to the target with the new layout. Values are coerced using an explicit visitor:

```cpp
writer_row.visit([&](size_t col, auto& val) {
    // val is the target-type field in the writer's row
    // We read from the source row and cast to the target type
    ColumnType src_type = src_layout.columnType(col);
    ColumnType dst_type = dst_layout.columnType(col);
    if (src_type == dst_type) {
        // Same type — direct copy via visitConst + set
        return;
    }
    // Coerce: the scan phase guarantees lossless conversion
    reader_row.visitConst(col, [&](size_t, const auto& src) {
        using Src = std::decay_t<decltype(src)>;
        val = static_cast<std::decay_t<decltype(val)>>(src);
    }, 1);
});
```

For string→number conversion, `strtod`/`strtoll` is used during scan. During conversion, the same parsing is applied to the string value and stored as the numeric type.

**No assertion overhead**: The scan guarantees round-trip validity. The conversion casts are therefore safe by construction. No per-value runtime check is needed in Phase 2.

#### 3.6. Encoding Preservation

The source file's encoding is read from the `FileHeader`:

```cpp
FileFlags flags = reader.fileHeader().getFlags();
uint8_t comp_level = reader.fileHeader().getCompressionLevel();
// Derive codec names from flags (using cli_common.h helpers)
CodecNames codecs = codecNamesFromFlags(flags, comp_level);
```

The writer is constructed with the same settings via `resolveCodecFlags()` and `withWriter()` from `cli_common.h`.

---

### 4. Key Technical Approaches & Rationale

| Decision | Approach | Rationale |
|----------|----------|-----------|
| **File organization** | Single `.cpp` in `src/tools/` | Matches all 10 existing CLI tools. No library changes needed. |
| **Row traversal** | `row.visitConst()` with generic lambda | Existing API; compiler devirtualizes `if constexpr` dispatch. No variant overhead. Matches `csv2bcsv.cpp:647`. |
| **Early termination** | Per-column `alive` flag + global `alive_count` | For realistic data, column types stabilize within first few rows. Avoids reading 95%+ of large files during scan. |
| **Ratchet widening** | `optimal_type` only widens | Monotonic min/max means we never miss a type. Once `optimal_type == original_type`, done. O(1) check per column per row. |
| **Hypothesis elimination** | Flip-flop flags (only true→false) | `f_all_whole`, `f_to_float_ok`, `str_all_numeric` are one-way. A dead hypothesis is never rechecked. |
| **Single Row object** | Reuse Row across loop | No per-row allocation. The Reader already reuses its internal Row. |
| **Round-trip verification** | Inline during scan, not separate pass | One pass total. Per-value cost is a comparison. Avoids re-opening and re-parsing the file. |
| **String→number** | Attempt `strtod` in scan visitor | Catches accidental numeric-in-string storage. First non-numeric value kills the hypothesis immediately. |
| **In-place rewrite** | Temp file + `std::filesystem::rename` | Atomic on POSIX. No partial file on crash. Same pattern as common databases. |
| **No JSON output** | Terminal table only | Tool is interactive by nature. No CI integration benefit identified. Existing `bcsvHeader`/`bcsvValidate` handle machine-readable output. |
| **Size estimation** | `wireSizeOf(target_type)` × rows_probed, extrapolated | Gives user a savings estimate before conversion. Not exact (codec-dependent overhead), but informative. |

---

### 5. Test Strategy

**Unit tests** (`tests/test_narrow_type.cpp`):

| Test | Verifies |
|------|----------|
| `Int64ToUint8` | `[0..200]` narrows to uint8 |
| `Int64ToBool` | `[0,1]` narrows to bool |
| `Int64ToUint16` | `[0..70000]` narrows to uint16 |
| `Int64Unchanged` | Full int64 range stays int64 |
| `DoubleToFloat` | Values that round-trip → float |
| `DoubleToInt32` | Whole numbers in int32 range → int32 |
| `DoubleUnchanged` | Non-roundtrippable stays double |
| `StringToInt32` | `["1", "2", "3"]` → int32 |
| `StringToBool` | `["true", "false"]` → bool |
| `StringUnchanged` | `["1", "abc"]` stays string |
| `EarlyTermination` | Scan stops when all columns stabilize |
| `MixedFile` | Some columns narrow, some don't |

**Integration tests** (`tests/integration/test_narrow.sh`):

- `bcsvGenerator -p mixed_generic -n 1000 → bcsvNarrowType --analyze → verify output`
- `bcsvNarrowType --convert -o out.bcsv → bcsvCompare --mode value in.bcsv out.bcsv → exit 0`
- `bcsvNarrowType --convert -f → verify file overwritten, content matches`
- Empty file, single-column file, all-unchanged file → no conversion

**Correctness invariant**: After conversion, `bcsvCompare --mode value original.bcsv converted.bcsv` must exit 0 (all values identical under coercion).

---

### 6. Implementation Order

1. Scaffold: CLI parsing, Config struct, help text, `main()`
2. Scan visitor: `ColumnProbeState` per column, `visit_value` for each type
3. Stabilization logic + early termination + `alive_count`
4. Analyze output: terminal table, size estimation
5. Conversion phase: new layout, read+coerce+write loop
6. In-place handling: temp file + rename
7. String→number parsing in scan visitor
8. CMakeLists.txt entry, CLI_TOOLS.md documentation
9. Tests

---

## User

Please consider the following feedback and update our plan: Here's the consolidated, corrected review for the implementer.

---

# Review: `bcsvNarrowType` — Specification & Implementation Plan

## Summary

The plan is well-structured and aligns with existing conventions. Verified against the codebase: the cli_common.h helpers it relies on all exist and are used correctly by other tools (`resolveCodecFlags`, `withWriter`, `codecNamesFromFlags`, `columnTypeStr`, `formatBytes`), the single-column `Row::visitConst(col, lambda, 1)` overload exists (row.h), and `bcsvCompare --mode value` exists with cross-type coercion so the correctness invariant is buildable. The `Reader<Layout>` auto-detects the row codec on read, so no `withReader` analogue is needed.

The early-termination design is **correct** (see clarification below). The remaining items range from one premise-level concern (savings estimation vs. VLE) down to spec gaps and polish.

---

## 1. Savings premise collides with VLE / delta encoding — **highest priority**

The default codec is `delta` (row) + `packet_lz4_batch` (file). The delta codec stores integers with **zigzag + variable-length encoding** (row_codec_delta002.hpp) and floats with XOR + leading-zero byte stripping. A value of `200` in an `int64` column already encodes to ~1–2 bytes, not 8 — then LZ4 compresses further. Narrowing the declared type to `uint8` therefore yields little or no on-disk savings for the default codec.

Fixed-width storage is only true for the **flat** codec (row_codec_flat001.h: `WIRE_DATA_SIZE = Σ sizeof(type)`).

Consequences:
- The estimate `wireSizeOf(target_type) × rows_probed` is accurate only for flat + uncompressed. For the default codec it will routinely over-promise (claim "23% savings" then deliver ~0%).
- The tool's real value is narrower than stated: it mainly helps (a) flat-codec files on disk, and (b) fixed-width *consumers* (C API direct-access, Unity P/Invoke, mmap) regardless of on-disk size.

**Action:** Don't estimate from type widths. Either perform the conversion to a tmp file and report *measured* before/after bytes, or drop the percentage and qualify that on-disk gains depend on the codec. At minimum, state that estimates assume the flat codec.

---

## 2. Early termination — algorithm is correct; only the wording/output needs fixing

**Confirmed correct and agreed:** A column cannot hold a value wider than its stored type, so once a column's observed `[min,max]` requires the original type, it stabilizes — mark it `alive = false` and skip it for all remaining rows. Per-column probing stops independently. The whole-file scan exits early only when `alive_count` reaches 0 (every column has reached its original type). If all columns stabilize at row 900 of 1000, skipping the last 100 rows is valid. This machinery (§3.2–3.3) is sound — keep it.

The only problems are two **descriptions** that overstate the whole-file exit:

- **Reword the performance claim.** "Avoids reading 95%+ of large files during scan" is true only when *every* column stabilizes early — which by definition means nothing was narrowable. As long as one genuine narrowing candidate exists, the scan runs to EOF. Replace with: *"Per-column probing stops as soon as a column reaches its original type; the whole scan exits early only if all columns stabilize before EOF."*
- **Fix the sample output.** It shows `Rows scanned: 10000 (out of 100000, scan terminated early)` *while* reporting narrowed columns (`sensor_id → uint16`, `active → bool`). That combination is impossible: narrowed columns stay `alive`, keeping `alive_count ≥ 1`, so the scan cannot have stopped at row 10,000. Show early termination only with an all-unchanged result, or show narrowing with a full scan.

(Optional: if you want genuine early exit for the common case, add an opt-in `--sample N` prefix scan — but then a later out-of-range row would silently corrupt the conversion, so `--convert` must run a validating pass; see §7.)

---

## 3. `uint64` values above `INT64_MAX` overflow the tracker

`min_int`/`max_int` are `int64_t` (§3.3). A `UINT64` column holding values `> 2^63−1` wraps to negative, corrupting both the ladder decision and the round-trip math. The whole-number-float path has the same issue at large magnitudes.

**Action:** Track unsigned columns in `uint64_t` (or keep parallel signed/unsigned accumulators and select based on `original_type` signedness).

---

## 4. Empty / zero-row columns produce bogus narrowing

With no values, sentinels remain `min_int = INT64_MAX`, `max_int = INT64_MIN`, `int_all_zero_or_one = true`. The ladder check (`min >= 0 && max <= 255`) passes and `all_zero_or_one` is still true, so an empty column would be "narrowed" to `bool`/`uint8`. The plan lists an empty-file test but the algorithm doesn't guard it.

**Action:** If `rows_probed == 0`, force `optimal_type = original_type`.

---

## 5. String→number narrowing is semantically lossy and under-specified

`strtod`/`strtoll` accept `"007"`, `"+1"`, `"1.0"`, `" 3 "`, `"1e3"`, and partial parses like `"12abc"` (without an `endptr` full-consumption check). Converting `string → int` then discards the original text (leading zeros, sign, decimal point, whitespace). Whether that is "lossless" depends entirely on how `bcsvCompare --mode value` coerces a string column against a numeric column — which the plan assumes but never verifies.

§2.3 also has a precedence ambiguity: `"0"`/`"1"` qualify as both numeric and bool, `"true"`/`"false"` as bool-only, yet `str_all_numeric` and `str_all_bool` are tracked independently with no defined winner.

**Action:**
- Require full-string consumption (check `endptr`), reject empty strings, and define whitespace handling.
- Make string→numeric **opt-in** (e.g. `--strings`, default off), since it changes column semantics, not just width.
- Define explicit precedence (bool > integer > float > string) and verify the `bcsvCompare` coercion assumption before relying on it for the test invariant.

---

## 6. NaN/Inf and signed-zero break the float round-trip checks

`static_cast<double>(static_cast<float>(NaN)) == NaN` is `false`, so a `double` column perfectly representable as `float` (including NaN/Inf, which float stores losslessly) is wrongly rejected. `floor(NaN)`/`floor(Inf)` likewise poison `f_all_whole`. `-0.0 == 0.0` is true, so a `-0.0` would map to `bool`/int and lose the sign bit. These are *safe* (false negatives) but miss valid narrowing and mishandle signed zero.

**Action:** Special-case non-finite values (compare bit patterns) and decide the `-0.0` policy explicitly.

---

## 7. "No assertion overhead" is fragile

§3.5 states conversion casts are "safe by construction" with no per-value runtime check. That holds only if scan and convert see identical data and the full-scan invariant is preserved. A silent narrowing cast that overflows produces a corrupt file with no error — bad for a data-integrity tool, and essential to address if `--sample` is ever added.

**Action:** Add a guarded cast (at least in debug) or run a `bcsvCompare --mode value` after `--convert`, rolling back the temp file on mismatch. The test plan already runs that compare externally — consider doing it inside `--convert`.

---

## 8. `ColumnProbeState` (§3.3) contradicts the simpler §3.4 derivation

§3.4 correctly derives the integer optimal type directly from `[min,max]` in O(1). But §3.3 also carries dual `IntLadder` positions (`int_ladder_pos` / `int_ladder_neg` — confusingly named: "pos"=signed, "neg"=unsigned) plus parallel `str_ladder_*` copies. For pure-integer columns the ladders are redundant with min/max; for the float→int path, `min/max` + `f_all_whole` + a single round-trip test suffices. The struct is over-engineered relative to the (correct) §3.4 algorithm, and the numeric/string duplication invites drift.

**Action:** Collapse the ladder fields into the min/max derivation; share one numeric-tracking helper between the numeric and string paths.

---

## 9. CLI naming collisions with existing convention

In csv2bcsv.cpp, `-f, --overwrite` already means "overwrite without confirmation." The plan reuses `-f`/`--overwrite` for overwriting the *input*, then adds a separate `--force` to "skip confirmation" — two different meanings of force. `--in-place` is a third alias for the same action.

**Action:** Pick one model — either `-f` overwrites silently (matching existing tools) or keep a prompt and drop the redundant flag. Trim the alias surface.

---

## 10. Analyze output goes to stderr

The analyze table is the tool's primary output and should go to **stdout** (pipeable/greppable); progress and verbose diagnostics go to stderr. Routing the table to stderr (mirroring `printLayoutSummary`'s default) is non-Unixy for a tool whose purpose is to emit that table.

**Action:** Table → stdout; diagnostics → stderr.

---

## 11. Per-column "Savings %" denominator is undefined

`75.0%` for int64→uint16 appears to be the type-width ratio (8→2), ignoring row header, bitset bits, string length prefixes, and VLE/LZ4. Per-column percentages will mislead.

**Action:** Drop them, or label them "max theoretical (flat codec)."

---

## Smaller items

- **Block size / level preservation (§3.6):** `codecNamesFromFlags` only recovers codec names. Confirm `FileHeader` actually exposes block size and the exact accessors (`getFlags()`, `getCompressionLevel()`) before relying on them.
- **Constant columns:** all-identical values already cost ~nothing under zoh/delta, so narrowing them may show 0 real savings even when "narrowable" — worth a note.
- **Integration test "verify file smaller":** with the default delta codec the output may not shrink (see §1). Don't assert size reduction unless the test forces `--row-codec flat`; otherwise it'll be flaky. The current tests don't assert size — keep it that way and document why.
- **Flag precedence:** define behavior for `--analyze --convert` together (error or convert-wins).
- **Line estimate (~900):** should drop once §8 is simplified and §1's estimation is replaced by measuring a temp file.

---

## Recommended plan adjustments (priority order)

1. Replace width-based estimation with **measured** before/after bytes (convert to tmp, stat both), or scope all savings claims to the flat codec. *(§1, §11)*
2. Reword the early-termination claim and fix the sample output so it doesn't show early exit alongside reported narrowing. The algorithm itself stays. *(§2)*
3. Use `uint64_t` for unsigned tracking; guard `rows_probed == 0`. *(§3, §4)*
4. Make string→numeric opt-in with full-consumption parsing; verify `bcsvCompare` string/number coercion first. *(§5)*
5. Handle NaN/Inf/−0.0 in float checks. *(§6)*
6. Add a post-conversion `bcsvCompare --mode value` (or guarded casts) inside `--convert`. *(§7)*
7. Collapse the `ColumnProbeState` ladders into the §3.4 min/max derivation. *(§8)*
8. Reconcile `-f`/`--force`/`--in-place`; send the analyze table to stdout. *(§9, §10)*

**Unchanged and approved:** the per-column `alive`/`alive_count` early-termination mechanism, the single-`.cpp` tool layout, reuse of cli_common.h helpers, `Reader<Layout>` auto-detect for input, the `withWriter` dispatch for output, and the `bcsvCompare --mode value` round-trip test invariant. 

---

## Assistant (Plan · Qwen3.6-27B Dense · 3442.9s)

_Thinking:_

The review is thorough and raises valid points. Let me go through each item and update the plan accordingly. The user wants me to incorporate all the feedback and present an updated, refined plan.

Let me work through the issues:

1. **Savings estimation vs VLE**: Need to drop width-based estimation, either measure a temp file or qualify flat-codec-only estimates.
2. **Early termination wording**: Algorithm is correct, fix sample output and wording.
3. **uint64 overflow**: Track unsigned values properly.
4. **Empty columns**: Guard `rows_probed == 0`.
5. **String→number**: Make opt-in, full-consumption check, define precedence.
6. **NaN/Inf/-0.0**: Handle special floating-point values.
7. **Conversion safety**: Add post-conversion verification or guarded casts.
8. **ColumnProbeState**: Simplify, collapse redundant ladders.
9. **CLI naming**: Reconcile with existing -f convention.


10. **Output routing**: Send analyze table to stdout for pipeability while keeping diagnostics on stderr, and either drop per-column savings percentages or mark them as theoretical.

Now I'll draft the updated plan incorporating all these refinements.




The review is thorough and correct. Let me address three points where I need your input before finalizing the updated plan:



**Tool: question**

**Input:**
```json
{
  "questions": [
    {
      "header": "Savings reporting",
      "question": "For savings reporting: the review recommends either (a) convert-to-temp then report measured before/after bytes, (b) drop percentage estimates entirely, or (c) qualify them as 'max theoretical (flat codec only)'. Which do you prefer, or a combination?",
      "options": [
        {
          "label": "a) Measured temp file",
          "description": "Always write a temp file during analyze, stat both, report real bytes. Adds I/O but accurate."
        },
        {
          "label": "c) Flat-codec qualifier",
          "description": "Estimate using type widths but label as 'max theoretical (flat codec)'. No temp file."
        },
        {
          "label": "a+c combination",
          "description": "Measured temp file for --convert path; flat-codec estimate for --analyze path. Best of both."
        }
      ],
      "multiple": false
    },
    {
      "header": "Conversion safety",
      "question": "For post-conversion integrity (review §7): should --convert verify the output before finalizing, or only in debug builds?",
      "options": [
        {
          "label": "Always verify (Recommended)",
          "description": "Always read back converted file and compare with bcsvCompare --mode value. Guarantees correctness, adds one more I/O pass."
        },
        {
          "label": "Guarded casts in debug",
          "description": "In debug, assert each cast doesn't overflow. In release, trust the scan. Faster but silent corruption possible if scan/convert mismatch."
        },
        {
          "label": "Verifying + optional skip",
          "description": "Verify by default, but add --no-verify flag to skip the validation pass."
        }
      ],
      "multiple": false
    },
    {
      "header": "String→number default",
      "question": "For string→number conversion (review §5): should the tool consider string columns as convertible by default, or only with an explicit flag?",
      "options": [
        {
          "label": "Opt-in with --strings",
          "description": "String→number conversion only with --strings flag. Conservative, respects user intent."
        },
        {
          "label": "Always on",
          "description": "Always attempt. String→number is lossless if values parse and round-trip. Aggressive."
        }
      ],
      "multiple": false
    }
  ]
}
```

**Output:**
```
User has answered your questions: "For savings reporting: the review recommends either (a) convert-to-temp then report measured before/after bytes, (b) drop percentage estimates entirely, or (c) qualify them as 'max theoretical (flat codec only)'. Which do you prefer, or a combination?"="c) Flat-codec qualifier", "For post-conversion integrity (review §7): should --convert verify the output before finalizing, or only in debug builds?"="Guarded casts in debug", "For string→number conversion (review §5): should the tool consider string columns as convertible by default, or only with an explicit flag?"="Opt-in with --stringsToValue". You can now continue with the user's answers in mind.
```

---

## Assistant (Plan · Qwen3.6-27B Dense · 347.9s)

_Thinking:_

The user has answered all the follow-up questions. Now I need to produce the final, updated plan incorporating all the review feedback and user answers. Let me organize this carefully.

Key changes from the review:
1. Savings estimation: use "max theoretical (flat codec)" qualifier, not measured temp file
2. Early termination: algorithm is correct, just fix the wording/sample output
3. UINT64 tracking: use uint64_t for unsigned, parallel accumulators
4. Empty columns: guard rows_probed == 0
5. String→number: opt-in with --stringsToValue, full-consumption parsing, defined precedence
6. NaN/Inf/-0.0: special-case non-finite values
7. Conversion safety: guarded casts in debug only
8. ColumnProbeState: collapse ladders into min/max derivation, simpler struct
9. CLI naming: -f matches existing tools (overwrite without confirmation), drop --in-place alias
10. Output: analyze table → stdout, diagnostics → stderr
11. Per-column savings %: drop or label as "max theoretical (flat codec)"

Plus smaller items: block size preservation, constant columns note, integration test caveat, flag precedence.

Let me write the final plan.
}




Here is the updated, final plan incorporating all review feedback.

---

# bcsvNarrowType — Final Specification & Implementation Plan

## 1. Intent

`bcsvNarrowType` scans a BCSV file column-by-column to find columns that can be stored in a narrower type without losing information. If the user opts in via `--convert`, the file is rewritten with same column count, same names, same encoding, but reduced per-column types.

**Important caveat on savings:** With the default codec (delta + packet_lz4_batch), integers use zigzag + VLE and floats use XOR + zero-stripping, so a value of `200` in an `int64` column already encodes to ~1–2 bytes before LZ4. Narrowing may yield **little or no on-disk savings** for delta/zoh codecs. The primary beneficiaries are:
- Files encoded with the **flat** codec (fixed-width serialization)
- **Fixed-width consumers** (C API direct-access, Unity P/Invoke, mmap) where in-memory struct sizes depend on declared types

All savings percentages reported by the tool are **max theoretical (flat codec)** — actual on-disk savings depend on the row codec and compression. Constant columns that already encode to ~0 bytes under delta/zoh may show "narrowable" but deliver 0 real savings.

## 2. Specified Tool Behaviour

### 2.1. CLI

```
bcsvNarrowType [OPTIONS] INPUT_FILE

Scan BCSV file for type-narrowing opportunities and optionally convert.

Arguments:
  INPUT_FILE         Input BCSV file

Output:
  -o, --output FILE  Write converted file to new location
  -f, --force        Overwrite INPUT_FILE in place (temp + atomic rename)

Options:
  --analyze          Scan-only, print findings (default, explicit alias)
  --convert          Scan + rewrite file with narrower types
  --stringsToValue   Also attempt string→numeric/bool conversion (opt-in)
  -v, --verbose      Per-column details and progress to stderr
  -h, --help         Show help message

Flag precedence: --analyze + --convert → --convert wins, warning to stderr.

Exit codes:
  0  Success
  1  Error (invalid file, conversion failed)
  2  Argument error
```

- `-f` mirrors existing tool convention (`csv2bcsv -f` = overwrite without confirmation).
- No `--in-place` alias. One flag, one meaning.
- **Analyze output → stdout** (pipeable/greppable). Verbose/progress → stderr.

### 2.2. Analyze Output (stdout)

Successful narrowing — full scan:
```
bcsvNarrowType: analysis of data.bcsv
File codec: packet_lz4_batch | Row codec: delta | Compression: 1
Rows scanned: 100000, Columns: 12

  Idx  Name            Original     Optimal
  ---  ----            --------     -------
    0  timestamp       int64        int64       (unchanged)
    1  sensor_id       int64         uint16
    2  temperature     double        float
    3  humidity        float         float       (unchanged)
    4  active          int64         bool
    5  label           string        string      (unchanged)
  ...

Narrowable columns: 3 / 12
Max theoretical savings (flat codec): 18.0% (estimated type widths only)

Use --convert -o output.bcsv to apply, or --convert -f to overwrite.
```

Early termination — no narrowing possible (all columns stabilized before EOF):
```
bcsvNarrowType: analysis of data.bcsv
File codec: packet_lz4_batch | Row codec: delta | Compression: 1
Rows scanned: 4521 (out of 100000, terminated early)
Columns: 12

All 12 columns are already at their narrowest type.
No conversion possible.
```

**Note:** The sample output above correctly shows early termination only with an "all unchanged" result. When narrowing is reported, `Rows scanned` will equal the total row count (because narrowed columns stay alive until EOF).

### 2.3. Type Narrowing Rules

#### Integer columns (INT8/16/32/64, UINT8/16/32/64)

Track `min`, `max` across all scanned values. For unsigned original types, use `uint64_t` accumulators to avoid wrap of values > INT64_MAX. For signed original types, use `int64_t` accumulators. Derive `optimal_type` in O(1) from `[min, max]`:

```
if all values are 0 or 1:               optimal = bool
else if min >= 0:                       // unsigned ladder
  UINT8 fits (max <= 255)?              → UINT8
  UINT16 fits (max <= 65535)?           → UINT16
  UINT32 fits (max <= 2^32-1)?          → UINT32
  else                                  → UINT64
else:                                   // signed ladder
  INT8 fits ([-128, 127])?              → INT8
  INT16 fits ([-32768, 32767])?         → INT16
  INT32 fits ([-2^31, 2^31-1])?         → INT32
  else                                  → INT64
```

Since `min` only decreases and `max` only increases, `optimal_type` widens monotonically. Once `optimal_type == original_type`, stabilization is guaranteed.

#### Float/Double columns

Track `min`, `max` as `double`. Per-value checks (all start `true`, flip to `false` on counterexample):

| Flag | Meaning | Counterexample |
|------|---------|----------------|
| `f_all_whole` | every value is an integer | `v != floor(v)` |
| `f_all_positive` | every value ≥ 0 | `v < 0` |
| `f_float_roundtrip` | can narrow to float | `d → f → d'` fails |
| `f_ladder_alive` | can narrow to some integer | `f_all_whole` died |
| `f_bool_alive` | all values are 0 or 1 | `v != 0.0 && v != 1.0` |

**NaN/Inf handling:** Non-finite values are excluded from narrowing checks, not treated as counterexamples. For `f_float_roundtrip`, check bit-pattern equality (`memcmp` of float cast vs original double) rather than `==`, because `NaN != NaN`. `std::isnan()` and `std::isinf()` gate the respective checks.

**Signed zero:** `-0.0` is treated as an integer (value `0`). If the row contains only `-0.0` and `0.0`, `f_all_whole` remains `true`, `f_bool_alive` remains `true` (all_zero_or_one). The cast to int discards the sign bit — this is considered lossless for our purposes. If this policy needs to be strict, add a `f_signed_zero_present` flag that disables int narrowing but leaves float narrowing alive.

**Float→int narrowest type:** Once `f_all_whole` is confirmed (not yet killed), compare `[min, max]` against the integer ladder (same as the integer column rules above). The narrowest ladder position that still contains `[min, max]` is tentatively `optimal_type`. On any subsequent non-whole value, `f_ladder_alive = false` and the integer path is dead.

**Overall optimal_type for float/double**, selected by precedence:
1. `bool` (if `f_bool_alive` still true)
2. Narrowest integer in ladder (if `f_ladder_alive` still true)
3. `float` (if original is `double` and `f_float_roundtrip` still true)
4. `original_type`

Per-value round-trip verification happens inline during scan. When `f_ladder_alive` dies and `f_float_roundtrip` dies and `f_bool_alive` dies, the column stabilizes to `original_type` and `alive = false`.

#### Bool columns (BOOL)

Already minimal. Column is `alive = false` from the start. No probing needed.

#### String columns (STRING)

Only attempted with `--stringsToValue` flag. Without the flag, string columns are skipped and reported as `(unchanged)`.

With `--stringsToValue`:
- Each value is attempted as a number using `strtod` with full-consumption check: `endptr` must point to the null terminator. Leading/trailing whitespace is **rejected**. `"007"`, `"+1"`, `"1.0"`, `"1e3"` are all accepted. `"12abc"`, `""`, `"  "` are rejected.
- If all values parse as numbers, track the same `double` statistics as float columns (`str_min`, `str_max`, `str_all_whole`, etc.), then derive the optimal numeric type.
- If any value fails to parse as a number, `str_all_numeric = false` immediately — column is done, stays `string`.
- **Precedence for string→bool:** Only considered if `--stringsToValue` is also enabled. After numeric parsing, check if all parsed values are `0.0` or `1.0`. Boolean-only strings like `"true"`/`"false"`/`"yes"`/`"no"` are **not** handled — the tool only recognizes numeric string representations. This keeps the parser simple (one `strtod` call per value) and the `bcsvCompare --mode value` coercion contract clear (string `"0"` == int `0`).

### 2.4. Encoding Preservation

The output file uses the same file codec, row codec, compression level, block size, and flags as the input. These are recovered from the `FileHeader`:

```cpp
FileFlags flags = reader.fileHeader().getFlags();
uint8_t   comp  = reader.fileHeader().getCompressionLevel();
```

Block size and other per-file parameters are passed through unchanged. The writer dispatch uses `withWriter()` from `cli_common.h` with the source row codec string.

### 2.5. Conversion Safety

- **Debug builds:** Every cast from source type to target type asserts that the value doesn't overflow or lose precision. If an assertion fires, the conversion aborts with an error and the temp file is deleted. This provides a safety net for any scan-phase bug.
- **Release builds:** Trust the scan-phase invariant. No per-value runtime checks.
- No automatic post-conversion `bcsvCompare` — the external test harness runs that. Adding it inside `--convert` would triple the I/O (scan + convert + verify). The debug assertions catch structural bugs; the release trust model matches the library's existing philosophy.

---

## 3. Implementation

### 3.1. Source Layout

```
src/tools/bcsvNarrowType.cpp       — single-file tool (~750 lines)
src/tools/CMakeLists.txt            — add build target
src/tools/CLI_TOOLS.md              — add documentation section
tests/test_narrow_type.cpp          — unit tests
tests/integration/test_narrow.sh    — shell integration tests
```

No changes to the library (`include/bcv/`). Only existing public APIs are used.

### 3.2. ColumnProbeState — Collapsed

The review correctly identified that the ladder fields in the original design were redundant with `[min, max]` derivation. The collapsed struct has a single set of numeric trackers shared by both native-numeric columns and string→numeric columns:

```cpp
struct ColumnProbeState {
    size_t     col_index;
    ColumnType original_type;
    ColumnType optimal_type;          // updated each row, starts at original_type
    bool       alive = true;          // false = stabilized, skip in future rows
    size_t     rows_probed = 0;

    // --- Integer columns ---
    int64_t    min_s  = INT64_MAX;    // signed accumulator (for signed original types)
    int64_t    max_s  = INT64_MIN;
    uint64_t   min_u  = UINT64_MAX;   // unsigned accumulator (for unsigned original types)
    uint64_t   max_u  = 0;

    // --- Float columns (and string→numeric path) ---
    double     min_f = std::numeric_limits<double>::max();
    double     max_f = std::numeric_limits<double>::lowest();
    bool       f_all_whole = true;
    bool       f_all_positive = true;
    bool       f_float_roundtrip = true;       // only relevant for DOUBLE
    bool       f_bool_alive = true;
    bool       f_ladder_alive = true;          // whole-number integer path viable
    int64_t    int_min = INT64_MAX;            // for integer ladder derivation
    int64_t    int_max = INT64_MIN;
    bool       int_all_positive = true;

    // --- String columns ---
    bool       str_all_numeric = true;         // every value parses as number?
    bool       str_done = false;               // killed by non-numeric value

    void visitIntegerSigned(int64_t v)  { update min_s/max_s, derive optimal_type }
    void visitIntegerUnsigned(uint64_t v) { update min_u/max_u, derive optimal_type }
    void visitFloat(double v)            { update min_f/max_f/flags, derive optimal_type }
    void visitString(const std::string& s) { strtod with endptr, delegate to visitFloat or kill }

    void checkStabilization();  // set alive = false if optimal_type == original_type and all flags dead
};
```

The `visitIntegerSigned/Unsigned` methods update min/max then derive `optimal_type` from §2.3 rules in O(1). `checkStabilization` compares `optimal_type` against `original_type` — if equal, and for float columns all narrowing flags are dead, the column is done.

**Empty-column guard:** If `rows_probed == 0`, `optimal_type` is forced to `original_type` regardless of sentinel values. This prevents bogus narrowing of empty files.

### 3.3. Scan Algorithm

```
Phase 1 — SCAN:
  reader = Reader<Layout>(input_file)     // codec auto-detected
  probes = vector<ColumnProbeState>(column_count)
  init each probe: optimal_type = original_type

  // Bool and String (without --stringsToValue) columns are dead from the start
  for each column i:
    if type == BOOL:
      probes[i].alive = false
    else if type == STRING and not stringsToValue:
      probes[i].alive = false

  alive_count = count of alive probes

  while reader.readNext() and alive_count > 0:
    row = reader.row()
    for each alive column i:
      type = probes[i].original_type
      switch (type):
        INT8..INT64:    row.visitConst<intXX_t>(i, probe.visitIntegerSigned, 1)
        UINT8..UINT64:  row.visitConst<uintXX_t>(i, probe.visitIntegerUnsigned, 1)
        FLOAT:          row.visitConst<float>(i, probe.visitFloat*, 1)     // *promote to double
        DOUBLE:         row.visitConst<double>(i, probe.visitFloat, 1)
        STRING:         row.visitConst<string>(i, probe.visitString, 1)

    // Check stabilization
    newly_dead = []
    for each alive column i:
      probes[i].rows_probed++
      probes[i].checkStabilization()
      if not probes[i].alive:
        alive_count--
        newly_dead.push(i)

    if alive_count == 0:
      break   // early termination — no more useful data in remaining rows

  // Post-scan: force optimal_type for columns that are still alive but done
  // (e.g., float columns where all flags are dead but optimal_type hasn't caught up)
  finalize_all_probes()

  // Guard empty file
  for each probe where rows_probed == 0:
    probe.optimal_type = probe.original_type
```

Performance: per-column probing stops as soon as a column reaches its original type. The whole scan exits early only if all columns stabilize before EOF — which by definition means no narrowing is possible. When genuine narrowing exists, at least one column stays alive, and the scan reads to EOF.

### 3.4. Conversion Algorithm

```
Phase 2 — CONVERT (only if --convert and at least one convertible column):

  new_layout = Layout()
  for each column:
    new_layout.addColumn(name, probe.optimal_type)

  reader = Reader<Layout>(input_file)
  withWriter(new_layout, source_row_codec, [&](auto& writer) {
    writer.open(output_path, force, comp_level, block_kb, flags)

    while reader.readNext():
      src_row = reader.row()
      dst_row = writer.row()
      dst_row.clear()

      for each column i:
        src_type = probes[i].original_type
        dst_type = probes[i].optimal_type
        if src_type == dst_type:
          // Same type — copy
          src_row.visitConst(i, [&](size_t, const auto& v) {
            dst_row.set(i, v);
          }, 1);
        else:
          // Type conversion — cast through double for cross-family
          src_row.visitConst(i, [&](size_t, const auto& src) {
            using T = std::decay_t<decltype(src)>;
            dst_row.set(i, coerce<T>(dst_type, src));
          }, 1);

      writer.writeRow();

    writer.close();
  });
```

The `coerce` function dispatches on destination type:

```cpp
template<typename Src>
ValueType coerce(ColumnType dst, Src src) {
    double intermediate = static_cast<double>(src);  // for string→number or float→int
    switch (dst) {
      case BOOL:   return bool(intermediate != 0.0);
      case UINT8:  return uint8_t(intermediate);
      case UINT16: return uint16_t(intermediate);
      // ... etc
      case FLOAT:  return float(intermediate);
      case DOUBLE: return double(intermediate);
      // STRING never a target (only source)
    }
}
```

In debug builds, the intermediate is checked:
```cpp
#ifdef NDEBUG
  // release: trust scan invariant
#else
  if (static_cast<double>(narrowed) != intermediate)
    throw std::runtime_error("NarrowType: cast overflow at col " + ...);
#endif
```

### 3.5. In-Place Overwrite (`-f`)

1. Create temp file in the same directory as the input (`input_path.dirname() / "bcsvNarrowType.tmp.XXXXXX"` via `mkstemp`).
2. Write the converted file to the temp path.
3. On success: `std::filesystem::rename(tmp, input_path)` — atomic on POSIX.
4. On failure (exception during conversion): remove temp file, rethrow.

### 3.6. No JSON output

Dropped per user decision. The tool is interactive-first. If machine-readable output is later needed, it can be added (other tools have it).

---

## 4. Key Technical Approaches

| Decision | Approach | Rationale |
|----------|----------|-----------|
| Row traversal | Typed visitor (`visitConst<T>`) | Compile-time dispatch, no runtime switch per column. Known type from layout. Uses `visitConst<T>(start, visitor, count=1)` for single-column reads. |
| Early termination | Per-column `alive` flag | Columns that reach original_type are skipped. Whole-file exit only when all columns stabilize. |
| Narrowing derivation | Min/max O(1) from `[min, max]` | Monotonic min/max guarantees widening-only `optimal_type`. Once equal to original, done. |
| Float hypotheses | One-way flip-flop flags | `f_all_whole`, `f_float_roundtrip`, etc. only go true→false. A dead hypothesis is never rechecked. O(1) amortized per row. |
| Unsigned overflow safety | `uint64_t` accumulators for UINT columns | `int64_t` would wrap values > 2^63-1. Parallel signed/unsigned accumulators. |
| Empty-column guard | `rows_probed == 0 → optimal = original` | Prevents sentinel-value narrowing on empty files. |
| String→numeric | `strtod` + endptr full consumption, opt-in | Conservative. Avoids silent text loss. Rejected: `"12abc"`, empty strings, whitespace. |
| NaN/Inf | `std::isnan`/`std::isinf` gate | Non-finite values excluded from narrowing, not treated as counterexamples. Bit-pattern comparison for float round-trip. |
| Savings estimation | Type-width ratio, labeled "max theoretical (flat codec)" | Accurate for flat codec. Explicit caveat for delta/zoh. No temp file I/O just for estimation. |
| Conversion safety | Debug assertions on casts | Catch scan bugs during development. Release trusts scan invariant. |
| In-place | Temp file + atomic rename | Standard pattern. Crash-resilient (atomic replace). |
| Single Row reuse | Reader/Writer own their Row | No per-row allocation. Streaming. Constant memory. |

---

## 5. Test Strategy

### Unit tests (`tests/test_narrow_type.cpp`)

| Test | Input | Expected |
|------|-------|----------|
| `Int64ToUint8` | `[0, 100, 200]` as int64 | uint8 |
| `Int64ToBool` | `[0, 1, 0, 1]` as int64 | bool |
| `Int64ToUint16` | `[0, 70000]` as int64 | uint16 |
| `Int64Unchanged` | full int64 range | int64 |
| `Uint64OverflowInt64` | values > INT64_MAX | uint64 (no wrap) |
| `DoubleToFloat` | values that round-trip double→float | float |
| `DoubleToInt32` | whole numbers in int32 range | int32 |
| `DoubleWithNaN` | contains NaN, otherwise float-roundtrips | NaN excluded, float check on finite values |
| `DoubleUnchanged` | non-roundtrippable | double |
| `EmptyFile` | 0 rows | all unchanged |
| `StringToInt32` | `["1", "42", "-5"]` with `--stringsToValue` | int32 |
| `StringToBool` | `["0", "1"]` with `--stringsToValue` | bool |
| `StringMixed` | `["1", "abc"]` with `--stringsToValue` | string (stays) |
| `StringRejected` | `["007"]` → preserved as number | int (007 = 7) |
| `StringPartial` | `["12abc"]` with `--stringsToValue` | string (rejected) |
| `EarlyTermination` | 1 column int64, first row = INT64_MAX | done at row 0, no more scan |
| `MixedFile` | some narrowable, some not | correct per-column types |
| `SignedZero` | `[-0.0, 0.0, 1.0]` | bool (treated as 0/1) |

### Integration tests (`tests/integration/test_narrow.sh`)

| Test | Command | Verification |
|------|---------|-------------|
| Analyze mode | `bcsvNarrowType` on generated file | stdout contains table, exit 0 |
| Convert + compare | `bcsvNarrowType --convert -o out.bcsv`, then `bcsvCompare --mode value in.bcsv out.bcsv` | exit 0 (values match) |
| In-place overwrite | `bcsvNarrowType --convert -f` | file overwritten, compare passes |
| No narrowing | all-optimal file | reports "All columns already narrowest" |
| Empty file | 0-row file | no narrowing, no error |
| Strings opt-in | `--stringsToValue` on string→numeric file | type changed |
| Strings without flag | same file without `--stringsToValue` | string unchanged |
| Flat codec size | `--row-codec flat` generated file | output file is measurably smaller |
| Delta codec size | default delta file | output file may not shrink — test does **not** assert size reduction |

**Important:** Integration tests for the default delta codec do **not** assert that the output file is smaller. They assert value correctness via `bcsvCompare --mode value`. Only tests with `--row-codec flat` assert size reduction.

---

## 6. Implementation Order

1. **Scaffold:** CLI parsing, Config struct, help text, `main()`, argument validation
2. **Probe struct:** `ColumnProbeState` with visit methods and stabilization check
3. **Scan loop:** Reader opening, per-column typed visitor dispatch, early termination via `alive_count`
4. **NaN/Inf handling:** Bit-pattern comparisons, `std::isnan` gates
5. **Analyze output:** stdout table with flat-codec qualifier, early-termination display
6. **Conversion loop:** New layout construction, Writer dispatch, coerce function with debug assertions
7. **In-place:** Temp file + rename
8. **String→numeric:** `strtod` + endptr, `--stringsToValue` flag
9. **CMakeLists.txt, CLI_TOOLS.md**
10. **Tests:** unit + integration

---

