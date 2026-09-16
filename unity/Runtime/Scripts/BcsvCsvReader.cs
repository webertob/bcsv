// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading;

namespace BCSV
{
    /// <summary>
    /// Reads rows from a CSV / CSV-LZ4 text file into typed BCSV rows. Opens
    /// through the unified C API (ADR-0009): the format is sniffed from the file
    /// content, so the same class reads plain ".csv" and ".csv.lz4" alike. A
    /// binary ".bcsv" file also opens (content-sniffed into the native BCSV
    /// member) — rows are then typed by the file's own layout, not this one;
    /// for typed binary access prefer BcsvReader. The Row property is refreshed
    /// on every successful Open: a BcsvRow stashed across a reopen is dangling
    /// afterwards — always re-read Row after opening.
    /// </summary>
    public sealed class BcsvCsvReader : IDisposable, IEnumerable<BcsvRow>
    {
        private nint _handle;
        private int _disposed;   // Interlocked claim flag: 0 = live
        private readonly BcsvLayout _layout;   // schema; must outlive open/reopens
        private readonly byte _delimiter;
        private readonly byte _decimalSep;
        private BcsvRow _row;

        public BcsvCsvReader(BcsvLayout layout, char delimiter = ',', char decimalSep = '.')
        {
            // The unified reader is an inert container until Open(): the schema
            // and parse chars travel on every open via bcsv_open_opts.
            _layout = layout;
            _delimiter = (byte)delimiter;
            _decimalSep = (byte)decimalSep;
            _handle = NativeMethods.bcsv_reader_create_u();
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
            // One-shot claim; see BcsvWriter.Dispose() for why.
            if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
            var handle = _handle;
            _handle = 0;
            if (handle != 0)
                NativeMethods.bcsv_reader_destroy_u(handle);
        }

        /// <summary>Opens a CSV file. Throws BcsvException on failure.</summary>
        public void Open(string filename, bool hasHeader = true)
        {
            ThrowIfDisposed();
            var opts = BuildOpts(hasHeader);
            if (!NativeMethods.bcsv_reader_open_u(_handle, filename,
                    NativeMethods.BcsvFormat.Auto, ref opts))
                NativeMethods.ThrowWithError("Failed to open CSV",
                    NativeMethods.bcsv_reader_error_msg_u(_handle));
            _row = new BcsvRow(NativeMethods.bcsv_reader_row_u(_handle));
        }

        /// <summary>Tries to open a CSV file. Returns false on failure (no exception).</summary>
        public bool TryOpen(string filename, bool hasHeader = true)
        {
            ThrowIfDisposed();
            var opts = BuildOpts(hasHeader);
            if (!NativeMethods.bcsv_reader_open_u(_handle, filename,
                    NativeMethods.BcsvFormat.Auto, ref opts))
                return false;
            _row = new BcsvRow(NativeMethods.bcsv_reader_row_u(_handle));
            return true;
        }

        private void ThrowIfDisposed()
        {
            if (_disposed != 0)
                throw new ObjectDisposedException(nameof(BcsvCsvReader));
        }

        // opts for one open: unified defaults plus the stored schema/parse chars.
        private NativeMethods.BcsvOpenOpts BuildOpts(bool hasHeader)
        {
            var o = NativeMethods.BcsvOpenOpts.NewDefault();
            o.csv_layout = _layout.Handle;
            o.delimiter = _delimiter;
            o.decimal_sep = _decimalSep;
            o.has_header = hasHeader ? 1 : 0;
            return o;
        }

        public void Close() => NativeMethods.bcsv_reader_close_u(_handle);
        public bool IsOpen => NativeMethods.bcsv_reader_is_open_u(_handle);

        /// <summary>
        /// Per-handle error/warning text; empty when the last read ended cleanly.
        /// A decode failure inside a compressed (".csv.lz4") stream surfaces here
        /// ("LZ4 decode failed...") — ReadNext() returning false is itself the
        /// same bool for a clean EOF and for a broken frame.
        /// </summary>
        public string ErrorMessage =>
            NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_reader_error_msg_u(_handle));

        public bool ReadNext() => NativeMethods.bcsv_reader_next_u(_handle);
        public BcsvRow Row => _row;
        public long CurrentIndex => (long)NativeMethods.bcsv_reader_index_u(_handle);

        public IEnumerator<BcsvRow> GetEnumerator()
        {
            while (ReadNext())
                yield return _row;
        }

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
