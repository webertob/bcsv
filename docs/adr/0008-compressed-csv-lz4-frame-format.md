# ADR-0008: Compressed CSV uses the standard LZ4 frame format

**Status:** Accepted
**Date:** 2026-09-15

## Context

CSV interop (`CsvReader`/`CsvWriter`, C API, NuGet and Unity bindings) only
handled plain text. Users exchanging measurement CSVs want compressed
on-the-fly files — `data.csv.lz4` — that stream (row-by-row, no full-file
loaders, per the project's core philosophy) yet remain usable outside bcsv:
someone must be able to `lz4 -d data.csv.lz4 data.csv` with the official CLI
and feed any spec-conformant LZ4 producer's output into bcsv.

bcsv already vendors LZ4 and has `LZ4CompressionStream` (`lz4_stream.hpp`),
but that codec is **headerless**: no magic, no sizes, no boundaries — one
`compress()` corresponds to exactly one `decompress()` and only makes sense
inside a `.bcsv` packet where the container stores the lengths. Written to a
loose file it is undecodable by anything else, including a future bcsv.

Two shape decisions follow from that: which container the file uses, and how
the reader picks a codec. And one behaviour decision: what happens when a
`.lz4` file turns out broken halfway through.

## Decision

1. **The `lz4_frame_stream` codec implements the standard LZ4 Frame
   format** (magic `0x184D2204`, the format `lz4` 1.10 writes, IETF
   draft-darwin-lz4-02), on top of the vendored `lz4frame.h`. Written files
   are valid LZ4 frame files: the CLI decodes them; CLI-produced frames
   (fast *and* `-12` HC, with `--content-size`) decode back through bcsv —
   both directions covered by tests in `lz4_csv_test.cpp`. Written frames
   use the CLI's 64 KB block default with linked blocks and **block +
   content checksums on** — deliberately stricter than the CLI's default
   flags: a checksumless frame cannot distinguish corruption from valid
   data, and the loud-error rule below only holds with them (the flags cost
   ~8 bytes per block; every spec reader validates them). Block max size
   (4=64KB/5=256KB/6=1MB/7=4MB) and level (0/1=fast, 2..12=LZ4HC) are user
   options via `bcsv_csv_writer_set_lz4()` / `BcsvCsvWriter.SetLz4()`.
   *(The C entry point was removed with the legacy `bcsv_csv_*` family —
   ADR-0009 revision, 2.0.0: the options travel in `bcsv_open_opts`
   (`lz4_block_id` / `lz4_level`) on `bcsv_writer_open_u`.)*
2. **The reader decides by content, not filename**: `open()` sniffs the first
   four bytes; frame magic → decode, anything else → plain CSV. A `.csv`
   file containing a frame reads as compressed; a `.lz4` file containing text
   reads as text. The writer picks by extension (`.lz4`): it cannot sniff
   its own future output; the reader's sniff makes the pairing lossless.
3. **Decode errors are loud**: a truncated, corrupted, or
   trailing-garbage `LZ4 frame` stream is an error (C++: exception →
   `getErrorMsg()` containing `LZ4 decode failed`; row iteration stops at the
   last good row but `readNext()` never masquerades the break as EOF).
   Silently returning a short row set is exactly the failure mode crash
   resilience was built to avoid.

## Options considered

1. **Reuse `LZ4CompressionStream` (the packet codec)** — zero new code,
   smallest files. Rejected: headerless streams are not self-describing,
   not CLI- or third-party-decodable, and a torn file is unrecoverable even
   for bcsv. Fails the "produce valid lz4 files" requirement.
2. **Other standard containers (gzip, zstd)** — widely supported, but no
   standard-library-grade in-tree option: gzip adds zlib, zstd is another
   large vendor drop. LZ4 is already vendored, already the library's
   compression axis, and the frame format is a ~100-line spec. (Extension
   point: `lz4_frame_stream.hpp` is the seam a later container would slot
   behind — same sniff pattern, same streambuf adapters.)
3. **Extension-only detection on read** — one `if` per open. Rejected: every
   misnamed or piped file (`cat frames | reader`, files renamed by tools)
   silently mis-parses, and sniffing costs 4 bytes once per file.
4. **Lenient decode (CLI's partial-output-on-corrupt policy)** — `lz4 -d`
   happily emits the recoverable prefix and warns on the console
   (exit 1). Rejected for a library: a console warning is invisible through
   the C API, and the alternative was silent truncation. Strict-by-default;
   a tolerance knob can be added if a user ever needs salvage mode.
5. **CRLF inside `.csv.lz4` like text-mode CSV** — Windows hosts see `\r\n`.
   Rejected: inside a frame the bytes *are* the data; newline translation
   would corrupt the container itself (the file stream opens `binary` for
   this reason). LF-inside is what `lz4 -d` output and Linux/macOS CSV
   tooling produce anyway; the reader strips `\r` for the plain path.

## Consequences

- `.csv.lz4` files are portable LZ4 artifacts: any language's lz4 binding,
  the CLI, or bcsv can process them; bcsv files are debuggable with `lz4 -t`
  / `lz4 -vv`.
- The reader keeps streaming semantics: blocks decode as pulled (≤64 KB
  scratch), never a whole-file buffer. A crashed writer leaves a valid frame
  prefix — strict reading names the truncation, salvage with `lz4 -d` if wanted.
- `Lz4FrameCompressor` is one-frame-per-instance by contract; do not write
  `header() + push() + finish()` in one expression (operator+ operands are
  indeterminately sequenced and the one-shot contract would throw). Prefer
  `Lz4FrameOStream`, used `std::ostream`-style.
- No new dependencies, no CMake change (`lz4frame.c` was already compiled for
  the batch codec). The `bcsv_csv_writer_set_lz4` C entry point was removed
  with the legacy `bcsv_csv_*` family (2.0.0, ADR-0009); LZ4 knobs now travel
  in `bcsv_open_opts`. ADR-0007 export guard passes.
- Skippable frames decode transparently; frame *streaming through them with
  unknown sizes* is handled via the spec's size field. Multi-frame
  concatenation (`cat a.lz4 b.lz4`) decodes as one logical stream.
