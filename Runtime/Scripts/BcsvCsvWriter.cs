// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

using System;

namespace BCSV
{
    /// <summary>Writes rows to a CSV text file from typed BCSV rows.</summary>
    public sealed class BcsvCsvWriter : IDisposable
    {
        private nint _handle;
        private BcsvRow _row;

        public BcsvCsvWriter(BcsvLayout layout, char delimiter = ',', char decimalSep = '.')
        {
            _handle = NativeMethods.bcsv_csv_writer_create(layout.Handle,
                (byte)delimiter, (byte)decimalSep);
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
            if (_handle != 0)
            {
                NativeMethods.bcsv_csv_writer_destroy(_handle);
                _handle = 0;
            }
        }

        /// <summary>Opens a CSV file for writing. Throws BcsvException on failure.</summary>
        public void Open(string filename, bool overwrite = false, bool includeHeader = true)
        {
            if (!NativeMethods.bcsv_csv_writer_open(_handle, filename, overwrite, includeHeader))
                NativeMethods.ThrowWithError("Failed to open CSV writer",
                    NativeMethods.bcsv_csv_writer_error_msg(_handle));
            _row = new BcsvRow(NativeMethods.bcsv_csv_writer_row(_handle));
        }

        /// <summary>Tries to open a CSV file for writing. Returns false on failure.</summary>
        public bool TryOpen(string filename, bool overwrite = false, bool includeHeader = true)
        {
            if (!NativeMethods.bcsv_csv_writer_open(_handle, filename, overwrite, includeHeader))
                return false;
            _row = new BcsvRow(NativeMethods.bcsv_csv_writer_row(_handle));
            return true;
        }

        public void Close() => NativeMethods.bcsv_csv_writer_close(_handle);
        public bool IsOpen => NativeMethods.bcsv_csv_writer_is_open(_handle);

        public BcsvRow Row => _row;

        /// <summary>Writes the current internal row to file.</summary>
        public void WriteRow()
        {
            if (!NativeMethods.bcsv_csv_writer_next(_handle))
                NativeMethods.ThrowWithError("CSV WriteRow failed",
                    NativeMethods.bcsv_csv_writer_error_msg(_handle));
        }

        /// <summary>Writes an external row to file.</summary>
        public void Write(BcsvRow row)
        {
            if (!NativeMethods.bcsv_csv_writer_write(_handle, row.Handle))
                NativeMethods.ThrowWithError("CSV Write failed",
                    NativeMethods.bcsv_csv_writer_error_msg(_handle));
        }

        public long RowCount => (long)NativeMethods.bcsv_csv_writer_index(_handle);
    }
}
