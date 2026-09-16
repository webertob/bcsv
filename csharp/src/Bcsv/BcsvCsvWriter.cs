// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>
/// Writes rows to a CSV / CSV-LZ4 text file from typed BCSV rows. Opens
/// through the unified C API (ADR-0009): the destination format resolved
/// from the file extension (".csv" text, ".csv.lz4" LZ4 frame). The Row
/// property is refreshed on every successful Open: a BcsvRow stashed across
/// a reopen is dangling afterwards — always re-read Row after opening.
/// </summary>
public sealed class BcsvCsvWriter : IDisposable
{
    private nint _handle;
    private int _disposed;   // Interlocked claim flag: 0 = live
    private readonly byte _delimiter;
    private readonly byte _decimalSep;
    private int _lz4BlockId = -1;     // SetLz4 storage: applied via opts at Open()
    private int _lz4Level = -1;       // -1 = never set → native default
    private BcsvRow _row;

    public BcsvCsvWriter(BcsvLayout layout, char delimiter = ',', char decimalSep = '.')
    {
        // create_u snapshots the layout, so the .bcsv member stays valid even
        // if the managed layout dies first; CSV parse chars ride the opts.
        _delimiter = (byte)delimiter;
        _decimalSep = (byte)decimalSep;
        _handle = NativeMethods.bcsv_writer_create_u(layout.Handle,
            NativeMethods.BcsvRowCodec.Flat);
        if (_handle == 0)
            throw new BcsvException("Failed to create CSV writer");
    }

    ~BcsvCsvWriter() => Dispose(false);

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        // One-shot claim; see BcsvWriter.Dispose() for why.
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        var handle = _handle;
        _handle = 0;
        if (handle != 0)
        {
            // Close before destroy: a CSV writer torn down by the finalizer
            // still flushes and closes its file deterministically.
            NativeMethods.bcsv_writer_close_u(handle);
            NativeMethods.bcsv_writer_destroy_u(handle);
        }
    }

    public void Open(string filename, bool overwrite = false, bool includeHeader = true)
    {
        ThrowIfDisposed();
        var opts = BuildOpts(includeHeader);
        if (!NativeMethods.bcsv_writer_open_u(_handle, filename, overwrite,
                NativeMethods.BcsvFormat.Auto, ref opts))
            NativeMethods.ThrowWithError("Failed to open CSV writer", NativeMethods.bcsv_writer_error_msg_u(_handle));
        _row = new BcsvRow(NativeMethods.bcsv_writer_row_u(_handle));
    }

    /// <summary>Tries to open a CSV file for writing. Returns false on failure.</summary>
    public bool TryOpen(string filename, bool overwrite = false, bool includeHeader = true)
    {
        ThrowIfDisposed();
        var opts = BuildOpts(includeHeader);
        if (!NativeMethods.bcsv_writer_open_u(_handle, filename, overwrite,
                NativeMethods.BcsvFormat.Auto, ref opts))
            return false;
        _row = new BcsvRow(NativeMethods.bcsv_writer_row_u(_handle));
        return true;
    }

    private void ThrowIfDisposed()
    {
        if (_disposed != 0)
            throw new ObjectDisposedException(nameof(BcsvCsvWriter));
    }

    /// <summary>
    /// Configures LZ4 frame output for a ".csv.lz4" destination. Must be
    /// called before Open(): the frame header is written at open time.
    /// Inert for plain ".csv" destinations. Readers auto-detect LZ4 by the
    /// frame magic, so no matching call is needed on the read side.
    /// Parameters are validated immediately and travel to the native open
    /// via bcsv_open_opts.lz4_block_id / lz4_level (ADR-0009).
    /// </summary>
    /// <param name="blockMaxSizeId">Frame block max size: 4=64KB, 5=256KB, 6=1MB, 7=4MB.</param>
    /// <param name="level">Compression level: 0 or 1 = fast (default 0),
    /// 2..12 = LZ4HC.</param>
    public void SetLz4(int blockMaxSizeId = 4, int level = 0)
    {
        // The open-time native range check still guards a reopen race; the
        // wrapper validates eagerly so SetLz4 keeps its pre-open fail-fast
        // behaviour (the same reasons the retired native export reported).
        if (IsOpen)
            throw new BcsvException("SetLz4 failed: Error: setLz4: writer is" +
                " already open \u2014 set LZ4 options before open()");
        if (blockMaxSizeId < 4 || blockMaxSizeId > 7)
            throw new BcsvException($"SetLz4 failed: Error: setLz4: invalid block size id {blockMaxSizeId} (4=64KB, 5=256KB, 6=1MB, 7=4MB)");
        if (level < 0 || level > 12)
            throw new BcsvException($"SetLz4 failed: Error: setLz4: invalid compression level {level} (0=fast, 2..12=LZ4HC)");
        _lz4BlockId = blockMaxSizeId;
        _lz4Level = level;
    }

    // opts for one open: unified defaults plus the stored parse chars, header
    // flag and LZ4 tuning (inert for plain .csv — native only reads them for
    // a CSV_LZ4 open).
    private NativeMethods.BcsvOpenOpts BuildOpts(bool includeHeader)
    {
        var o = NativeMethods.BcsvOpenOpts.NewDefault();
        o.delimiter = _delimiter;
        o.decimal_sep = _decimalSep;
        o.include_header = includeHeader ? 1 : 0;
        o.lz4_block_id = _lz4BlockId;
        o.lz4_level = _lz4Level;
        return o;
    }

    public void Close() => NativeMethods.bcsv_writer_close_u(_handle);
    public bool IsOpen => NativeMethods.bcsv_writer_is_open_u(_handle);

    public BcsvRow Row => _row;

    public void WriteRow()
    {
        if (!NativeMethods.bcsv_writer_next_u(_handle))
            NativeMethods.ThrowWithError("CSV WriteRow failed", NativeMethods.bcsv_writer_error_msg_u(_handle));
    }

    public void Write(BcsvRow row)
    {
        if (!NativeMethods.bcsv_writer_write_u(_handle, row.Handle))
            NativeMethods.ThrowWithError("CSV Write failed", NativeMethods.bcsv_writer_error_msg_u(_handle));
    }

    public long RowCount => (long)NativeMethods.bcsv_writer_index_u(_handle);
}
