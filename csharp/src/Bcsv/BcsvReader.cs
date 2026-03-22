// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Collections;
using System.Runtime.InteropServices;

namespace Bcsv;

/// <summary>
/// Reads rows sequentially from a BCSV binary file. Supports IEnumerable for
/// foreach iteration. Also supports random access via Read(index).
/// </summary>
public sealed class BcsvReader : IDisposable, IEnumerable<BcsvRow>
{
    private nint _handle;
    private BcsvLayout? _layout;
    private BcsvRow _row;

    public BcsvReader()
    {
        _handle = NativeMethods.bcsv_reader_create();
        if (_handle == 0)
            throw new BcsvException("Failed to create reader");
    }

    ~BcsvReader() => Dispose(false);

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (_handle != 0)
        {
            NativeMethods.bcsv_reader_destroy(_handle);
            _handle = 0;
        }
    }

    public void Open(string filename)
    {
        if (!NativeMethods.bcsv_reader_open(_handle, filename))
            NativeMethods.ThrowWithError("Failed to open", NativeMethods.bcsv_reader_error_msg(_handle));
        _row = new BcsvRow(NativeMethods.bcsv_reader_row(_handle));
    }

    public void Open(string filename, bool rebuildFooter)
    {
        if (!NativeMethods.bcsv_reader_open_ex(_handle, filename, rebuildFooter))
            NativeMethods.ThrowWithError("Failed to open", NativeMethods.bcsv_reader_error_msg(_handle));
        _row = new BcsvRow(NativeMethods.bcsv_reader_row(_handle));
    }

    /// <summary>Tries to open a file. Returns false on failure (no exception).</summary>
    public bool TryOpen(string filename)
    {
        if (!NativeMethods.bcsv_reader_open(_handle, filename))
            return false;
        _row = new BcsvRow(NativeMethods.bcsv_reader_row(_handle));
        return true;
    }

    /// <summary>Tries to open with optional footer rebuild. Returns false on failure.</summary>
    public bool TryOpen(string filename, bool rebuildFooter)
    {
        if (!NativeMethods.bcsv_reader_open_ex(_handle, filename, rebuildFooter))
            return false;
        _row = new BcsvRow(NativeMethods.bcsv_reader_row(_handle));
        return true;
    }

    public void Close() => NativeMethods.bcsv_reader_close(_handle);
    public bool IsOpen => NativeMethods.bcsv_reader_is_open(_handle);

    public string? Filename
    {
        get
        {
            var ptr = NativeMethods.bcsv_reader_filename(_handle);
            return ptr == IntPtr.Zero ? null : NativeMethods.PtrToStringAuto(ptr);
        }
    }

    public string ErrorMessage =>
        NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_reader_error_msg(_handle));

    /// <summary>Advance to next row. Returns false at EOF.</summary>
    public bool ReadNext() => NativeMethods.bcsv_reader_next(_handle);

    /// <summary>Random access: read row at given index.</summary>
    public bool Read(long index) => NativeMethods.bcsv_reader_read(_handle, (nuint)index);

    /// <summary>Current row (reference — data changes on ReadNext/Read).</summary>
    public BcsvRow Row => _row;

    public BcsvLayout Layout
    {
        get
        {
            _layout ??= new BcsvLayout(NativeMethods.bcsv_reader_layout(_handle), ownsHandle: false);
            return _layout;
        }
    }

    public long RowCount => (long)NativeMethods.bcsv_reader_count_rows(_handle);
    public long CurrentIndex => (long)NativeMethods.bcsv_reader_index(_handle);
    public byte CompressionLevel => NativeMethods.bcsv_reader_compression_level(_handle);
    public FileFlags FileFlags => (FileFlags)NativeMethods.bcsv_reader_file_flags(_handle);

    internal nint Handle => _handle;

    /// <summary>
    /// Read up to maxRows rows from current position into column-oriented buffers.
    /// Returns a ColumnData with the actual number of rows read.
    /// Returns null at EOF (0 rows available).
    /// </summary>
    public ColumnData? ReadBatch(int maxRows)
    {
        if (maxRows <= 0)
            throw new ArgumentOutOfRangeException(nameof(maxRows), maxRows, "Must be positive");

        var result = BcsvColumns.ReadColumnsFromHandle(_handle, Layout, maxRows);
        if (result.RowCount == 0) { result.Dispose(); return null; }
        return result;
    }

    // ── IEnumerable<BcsvRow> — enables foreach ─────────────────────────
    public IEnumerator<BcsvRow> GetEnumerator()
    {
        while (ReadNext())
            yield return _row;
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
