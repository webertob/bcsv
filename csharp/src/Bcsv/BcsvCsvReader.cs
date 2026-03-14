// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Collections;

namespace Bcsv;

/// <summary>Reads rows from a CSV text file into typed BCSV rows.</summary>
public sealed class BcsvCsvReader : IDisposable, IEnumerable<BcsvRow>
{
    private nint _handle;
    private BcsvRow _row;

    public BcsvCsvReader(BcsvLayout layout, char delimiter = ',', char decimalSep = '.')
    {
        _handle = NativeMethods.bcsv_csv_reader_create(layout.Handle,
            (byte)delimiter, (byte)decimalSep);
        if (_handle == 0)
            throw new BcsvException("Failed to create CSV reader");
    }

    ~BcsvCsvReader() => Dispose(false);

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (_handle != 0)
        {
            NativeMethods.bcsv_csv_reader_destroy(_handle);
            _handle = 0;
        }
    }

    public void Open(string filename, bool hasHeader = true)
    {
        if (!NativeMethods.bcsv_csv_reader_open(_handle, filename, hasHeader))
            NativeMethods.ThrowWithError("Failed to open CSV", NativeMethods.bcsv_csv_reader_error_msg(_handle));
        _row = new BcsvRow(NativeMethods.bcsv_csv_reader_row(_handle));
    }

    public void Close() => NativeMethods.bcsv_csv_reader_close(_handle);
    public bool IsOpen => NativeMethods.bcsv_csv_reader_is_open(_handle);

    public bool ReadNext() => NativeMethods.bcsv_csv_reader_next(_handle);
    public BcsvRow Row => _row;
    public long CurrentIndex => (long)NativeMethods.bcsv_csv_reader_index(_handle);

    public IEnumerator<BcsvRow> GetEnumerator()
    {
        while (ReadNext())
            yield return _row;
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
