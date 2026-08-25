// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

using System;

namespace BCSV
{
    /// <summary>
    /// Writes rows to a BCSV binary file. Wraps the native bcsv_writer_t handle.
    /// Use the Row property to fill data, then call WriteRow() to commit.
    /// </summary>
    public sealed class BcsvWriter : IDisposable
    {
        private nint _handle;
        private BcsvLayout _layout;  // borrowed, not owned
        private BcsvRow _row;

        /// <param name="layout">Layout defining columns. Must remain alive while writer is in use.</param>
        /// <param name="rowCodec">"flat", "zoh", or "delta" (default).</param>
        public BcsvWriter(BcsvLayout layout, string rowCodec = "delta")
        {
            if (layout == null)
                throw new ArgumentNullException(nameof(layout));

            _handle = rowCodec switch
            {
                "flat" => NativeMethods.bcsv_writer_create(layout.Handle),
                "zoh"  => NativeMethods.bcsv_writer_create_zoh(layout.Handle),
                "delta" => NativeMethods.bcsv_writer_create_delta(layout.Handle),

                // Anything else used to land here as delta, so a typo recorded
                // happily in a codec nobody asked for and the file gave no sign
                // of it. pybcsv has always rejected an unknown name; this makes
                // the two agree.
                _ => throw new ArgumentException(
                        $"Unknown row codec: '{rowCodec}'. Use \"delta\", \"zoh\", or \"flat\".",
                        nameof(rowCodec)),
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

        /// <summary>Opens a file. Throws BcsvException on failure.</summary>
        /// <summary>Opens <paramref name="filename"/> for writing.</summary>
        /// <param name="flags">
        /// File-structure flags. <b>The row-codec bits are ignored here</b> — see the
        /// remarks.
        /// </param>
        /// <remarks>
        /// <para>
        /// <see cref="FileFlags.ZeroOrderHold"/> and <see cref="FileFlags.DeltaEncoding"/>
        /// are <b>output-only</b>. Whatever is passed for them is discarded and replaced
        /// from the row codec chosen in the constructor, because a header claiming one
        /// codec while the rows were produced by another is a file no reader can trust.
        /// So <c>Open(path, flags: FileFlags.ZeroOrderHold)</c> compiles, looks right and
        /// does nothing: to record zero-order-hold, pass <c>"zoh"</c> to
        /// <see cref="BcsvWriter(BcsvLayout, string)"/> instead.
        /// </para>
        /// <para>
        /// The bits this argument does honour are <see cref="FileFlags.NoFileIndex"/>,
        /// <see cref="FileFlags.StreamMode"/> and <see cref="FileFlags.BatchCompress"/>.
        /// <see cref="FileFlags"/> reports what actually reached the header.
        /// </para>
        /// </remarks>
        public void Open(string filename, bool overwrite = false,
                         int compression = BcsvDefaults.CompressionLevel,
                         int blockSizeKb = BcsvDefaults.BlockSizeKb,
                         FileFlags flags = FileFlags.BatchCompress)
        {
            if (!NativeMethods.bcsv_writer_open(_handle, filename, overwrite,
                    compression, blockSizeKb, (int)flags))
                NativeMethods.ThrowWithError("Failed to open writer",
                    NativeMethods.bcsv_writer_error_msg(_handle));
        }

        /// <summary>Tries to open a file. Returns false on failure (no exception).</summary>
        /// <remarks>
        /// The row-codec bits of <paramref name="flags"/> are ignored exactly as they
        /// are by <see cref="Open"/> — see its remarks.
        /// </remarks>
        public bool TryOpen(string filename, bool overwrite = false,
                            int compression = BcsvDefaults.CompressionLevel,
                            int blockSizeKb = BcsvDefaults.BlockSizeKb,
                            FileFlags flags = FileFlags.BatchCompress)
        {
            return NativeMethods.bcsv_writer_open(_handle, filename, overwrite,
                compression, blockSizeKb, (int)flags);
        }

        /// <summary>The flags actually written to the file header.</summary>
        /// <remarks>
        /// Not necessarily the flags passed to <see cref="Open"/>: the row-codec bits
        /// are replaced from the codec this writer was constructed with. Read this to
        /// find out what a file really carries rather than assuming the request was
        /// honoured. Meaningful only while the writer is open.
        /// </remarks>
        public FileFlags FileFlags => (FileFlags)NativeMethods.bcsv_writer_file_flags(_handle);

        public void Close() => NativeMethods.bcsv_writer_close(_handle);
        public void Flush() => NativeMethods.bcsv_writer_flush(_handle);
        public bool IsOpen => NativeMethods.bcsv_writer_is_open(_handle);

        public string Filename => FilenameHelper.GetWriterFilename(_handle);

        /// <summary>Mutable reference to the internal row. Fill with Set*, then call WriteRow().</summary>
        public BcsvRow Row => _row;

        /// <summary>Writes the current internal row to file.</summary>
        public void WriteRow()
        {
            if (!NativeMethods.bcsv_writer_next(_handle))
                NativeMethods.ThrowWithError("WriteRow failed",
                    NativeMethods.bcsv_writer_error_msg(_handle));
        }

        /// <summary>Writes an external row to file.</summary>
        public void Write(BcsvRow row)
        {
            if (!NativeMethods.bcsv_writer_write(_handle, row.Handle))
                NativeMethods.ThrowWithError("Write failed",
                    NativeMethods.bcsv_writer_error_msg(_handle));
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

        public byte CompressionLevel => NativeMethods.bcsv_writer_compression_level(_handle);

        public string ErrorMessage =>
            NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_writer_error_msg(_handle));

        internal nint Handle => _handle;
    }
}