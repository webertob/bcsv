using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace BCSV
{
    /// <summary>
    /// Writer for BCSV files - writes files row by row
    /// </summary>
    public class BcsvWriter : IDisposable
    {
        private IntPtr handle;
        private bool disposed = false;
        private BcsvLayout layout;

        /// <summary>
        /// Create a new BCSV writer with the specified layout
        /// </summary>
        /// <param name="layout">Layout defining the schema</param>
        public BcsvWriter(BcsvLayout layout)
        {
            if (layout == null)
                throw new ArgumentNullException(nameof(layout));

            this.layout = layout;
            handle = NativeMethods.bcsv_writer_create(layout.Handle);
            if (handle == IntPtr.Zero)
                throw new InvalidOperationException("Failed to create BCSV writer");
        }

        /// <summary>
        /// Internal handle for native calls
        /// </summary>
        internal IntPtr Handle
        {
            get
            {
                if (disposed)
                    throw new ObjectDisposedException("BcsvWriter");
                return handle;
            }
        }

        /// <summary>
        /// Check if the writer has a file open
        /// </summary>
        public bool IsOpen
        {
            get { return NativeMethods.bcsv_writer_is_open(Handle); }
        }

        /// <summary>
        /// Get the filename of the currently open file
        /// </summary>
        public string Filename
        {
            get
            {
                var ptr = NativeMethods.bcsv_writer_filename(Handle);
                return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
            }
        }

        /// <summary>
        /// Get the layout of this writer
        /// </summary>
        public BcsvLayout Layout
        {
            get
            {
                var layoutHandle = NativeMethods.bcsv_writer_layout(Handle);
                return layoutHandle == IntPtr.Zero ? null : new BcsvLayout(layoutHandle);
            }
        }

        /// <summary>
        /// Get the current row for writing data
        /// </summary>
        public BcsvRow Row
        {
            get
            {
                var rowHandle = NativeMethods.bcsv_writer_row(Handle);
                return rowHandle == IntPtr.Zero ? null : new BcsvRow(rowHandle);
            }
        }

        /// <summary>
        /// Get the current row index (0-based, number of rows written so far)
        /// </summary>
        public int Index
        {
            get { return (int)NativeMethods.bcsv_writer_index(Handle); }
        }

        /// <summary>
        /// Open a BCSV file for writing
        /// </summary>
        /// <param name="filename">Path to the BCSV file to create</param>
        /// <returns>True if successful</returns>
        public bool Open(string filename, bool overwrite = false, int compression = 1, int blockSizeKB = 64, FileFlags flag = FileFlags.None)
        {
            if (string.IsNullOrEmpty(filename))
                throw new ArgumentNullException(nameof(filename));

            return NativeMethods.bcsv_writer_open(Handle, filename, overwrite, compression, blockSizeKB, flag);
        }

        /// <summary>
        /// Close the currently open file
        /// </summary>
        public void Close()
        {
            NativeMethods.bcsv_writer_close(Handle);
        }

        /// <summary>
        /// Flush any pending data to disk
        /// </summary>
        public void Flush()
        {
            NativeMethods.bcsv_writer_flush(Handle);
        }

        /// <summary>
        /// Write the current row to the file and advance to the next row
        /// </summary>
        /// <returns>True if successful</returns>
        public bool Next()
        {
            return NativeMethods.bcsv_writer_next(Handle);
        }

        /// <summary>
        /// Write a row of data to the file
        /// </summary>
        /// <param name="rowData">Array of values to write</param>
        /// <returns>True if successful</returns>
        public bool WriteRow(params object[] rowData)
        {
            if (rowData == null)
                throw new ArgumentNullException(nameof(rowData));

            if (!IsOpen || Layout == null)
                throw new InvalidOperationException("No file is open");

            var row = Row;
            if (row == null)
                return false;

            var layout = Layout;
            int columnCount = Math.Min(rowData.Length, layout.ColumnCount);

            try
            {
                for (int i = 0; i < columnCount; i++)
                {
                    if (rowData[i] == null)
                        continue;

                    var columnType = layout.GetColumnType(i);
                    switch (columnType)
                    {
                        case ColumnType.Bool:
                            row.SetBool(i, Convert.ToBoolean(rowData[i]));
                            break;
                        case ColumnType.UInt8:
                            row.SetUInt8(i, Convert.ToByte(rowData[i]));
                            break;
                        case ColumnType.UInt16:
                            row.SetUInt16(i, Convert.ToUInt16(rowData[i]));
                            break;
                        case ColumnType.UInt32:
                            row.SetUInt32(i, Convert.ToUInt32(rowData[i]));
                            break;
                        case ColumnType.UInt64:
                            row.SetUInt64(i, Convert.ToUInt64(rowData[i]));
                            break;
                        case ColumnType.Int8:
                            row.SetInt8(i, Convert.ToSByte(rowData[i]));
                            break;
                        case ColumnType.Int16:
                            row.SetInt16(i, Convert.ToInt16(rowData[i]));
                            break;
                        case ColumnType.Int32:
                            row.SetInt32(i, Convert.ToInt32(rowData[i]));
                            break;
                        case ColumnType.Int64:
                            row.SetInt64(i, Convert.ToInt64(rowData[i]));
                            break;
                        case ColumnType.Float:
                            row.SetFloat(i, Convert.ToSingle(rowData[i]));
                            break;
                        case ColumnType.Double:
                            row.SetDouble(i, Convert.ToDouble(rowData[i]));
                            break;
                        case ColumnType.String:
                            row.SetString(i, rowData[i]?.ToString());
                            break;
                    }
                }

                return Next();
            }
            catch (Exception ex)
            {
                Debug.LogError($"Error writing row: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Write multiple rows of data to the file
        /// </summary>
        /// <param name="rows">Array of row data arrays</param>
        /// <returns>Number of rows successfully written</returns>
        public int WriteRows(object[][] rows)
        {
            if (rows == null)
                throw new ArgumentNullException(nameof(rows));

            int writtenCount = 0;
            foreach (var rowData in rows)
            {
                if (WriteRow(rowData))
                    writtenCount++;
                else
                    break;
            }
            return writtenCount;
        }

        /// <summary>
        /// Write data using a callback function to populate each row
        /// </summary>
        /// <param name="rowCount">Number of rows to write</param>
        /// <param name="rowPopulator">Function to populate each row</param>
        /// <returns>Number of rows successfully written</returns>
        public int WriteRows(int rowCount, Action<BcsvRow, int> rowPopulator)
        {
            if (rowPopulator == null)
                throw new ArgumentNullException(nameof(rowPopulator));

            int writtenCount = 0;
            for (int i = 0; i < rowCount; i++)
            {
                var row = Row;
                if (row == null)
                    break;

                try
                {
                    rowPopulator(row, i);
                    if (Next())
                        writtenCount++;
                    else
                        break;
                }
                catch (Exception ex)
                {
                    Debug.LogError($"Error writing row {i}: {ex.Message}");
                    break;
                }
            }
            return writtenCount;
        }

        /// <summary>
        /// Get the last error message from the native library
        /// </summary>
        /// <returns>Error message or null</returns>
        public static string GetLastError()
        {
            var ptr = NativeMethods.bcsv_last_error();
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!disposed)
            {
                if (handle != IntPtr.Zero)
                {
                    Close(); // Close file if open
                    NativeMethods.bcsv_writer_destroy(handle);
                    handle = IntPtr.Zero;
                }
                disposed = true;
            }
        }

        ~BcsvWriter()
        {
            Dispose(false);
        }
    }
}