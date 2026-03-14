// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>
/// Writes rows to a BCSV binary file. Wraps the native bcsv_writer_t handle.
/// Use the Row property to fill data, then call WriteRow() to commit.
/// </summary>
public sealed class BcsvWriter : IDisposable
{
    private nint _handle;
    private BcsvLayout? _layout;  // borrowed, not owned
    private BcsvRow _row;

    /// <param name="layout">Layout defining columns. Must remain alive while writer is in use.</param>
    /// <param name="rowCodec">"flat", "zoh", or "delta" (default).</param>
    public BcsvWriter(BcsvLayout layout, string rowCodec = "delta")
    {
        _handle = rowCodec switch
        {
            "flat" => NativeMethods.bcsv_writer_create(layout.Handle),
            "zoh"  => NativeMethods.bcsv_writer_create_zoh(layout.Handle),
            _      => NativeMethods.bcsv_writer_create_delta(layout.Handle),
        };
        if (_handle == 0)
            throw new BcsvException("Failed to create writer");
        _row = new BcsvRow(NativeMethods.bcsv_writer_row(_handle));
    }

    ~BcsvWriter() => Dispose(false);

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (_handle != 0)
        {
            NativeMethods.bcsv_writer_destroy(_handle);
            _handle = 0;
        }
    }

    public void Open(string filename, bool overwrite = true,
                     int compression = 1, int blockSizeKb = 64,
                     FileFlags flags = FileFlags.BatchCompress)
    {
        if (!NativeMethods.bcsv_writer_open(_handle, filename, overwrite,
                compression, blockSizeKb, (int)flags))
            NativeMethods.ThrowWithError("Failed to open writer", NativeMethods.bcsv_writer_error_msg(_handle));
    }

    public void Close() => NativeMethods.bcsv_writer_close(_handle);
    public void Flush() => NativeMethods.bcsv_writer_flush(_handle);
    public bool IsOpen => NativeMethods.bcsv_writer_is_open(_handle);

    /// <summary>Mutable reference to the internal row. Fill with Set*, then call WriteRow().</summary>
    public BcsvRow Row => _row;

    /// <summary>Writes the current internal row to file.</summary>
    public void WriteRow()
    {
        if (!NativeMethods.bcsv_writer_next(_handle))
            NativeMethods.ThrowWithError("WriteRow failed", NativeMethods.bcsv_writer_error_msg(_handle));
    }

    /// <summary>Writes an external row to file.</summary>
    public void Write(BcsvRow row)
    {
        if (!NativeMethods.bcsv_writer_write(_handle, row.Handle))
            NativeMethods.ThrowWithError("Write failed", NativeMethods.bcsv_writer_error_msg(_handle));
    }

    public BcsvLayout Layout
    {
        get
        {
            _layout ??= new BcsvLayout(NativeMethods.bcsv_writer_layout(_handle), ownsHandle: false);
            return _layout;
        }
    }

    public long RowCount => (long)NativeMethods.bcsv_writer_index(_handle);

    internal nint Handle => _handle;
}
