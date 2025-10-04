/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace BCSV
{
    /// <summary>
    /// Reader for BCSV files - reads files row by row
    /// </summary>
    public class BcsvReader : IDisposable
    {
        private IntPtr handle;

        /// <summary>
        /// Create a new BCSV reader
        /// </summary>
        /// <param name="mode">Read mode (strict or resilient)</param>
        public BcsvReader(ReadMode mode = ReadMode.Strict)
        {
            handle = NativeMethods.bcsv_reader_create(mode);
            if (handle == IntPtr.Zero)
                throw new InvalidOperationException("Failed to create BCSV reader");
        }

        /// <summary>
        /// Internal handle for native calls
        /// </summary>
        internal IntPtr Handle
        {
            get
            {
                if (handle == IntPtr.Zero)
                    throw new ObjectDisposedException("BcsvReader");
                return handle;
            }
        }

        /// <summary>
        /// Check if the reader has a file open
        /// </summary>
        public bool IsOpen
        {
            get { return NativeMethods.bcsv_reader_is_open(Handle); }
        }

        /// <summary>
        /// Get the filename of the currently open file
        /// </summary>
        public string Filename
        {
            get
            {
                return FilenameHelper.GetReaderFilename(Handle);
            }
        }

        /// <summary>
        /// Get the layout of the currently open file
        /// </summary>
        public BcsvLayout Layout
        {
            get
            {
                var layoutHandle = NativeMethods.bcsv_reader_layout(Handle);
                return layoutHandle == IntPtr.Zero ? null : new BcsvLayout(layoutHandle);
            }
        }

        /// <summary>
        /// Get the current row (valid after calling Next() successfully)
        /// Returns an immutable reference to prevent accidental modification
        /// </summary>
        public BcsvRowRefConst Row
        {
            get
            {
                var rowHandle = NativeMethods.bcsv_reader_row(Handle);
                return rowHandle == IntPtr.Zero ? null : new BcsvRowRefConst(rowHandle);
            }
        }

        /// <summary>
        /// Get the current row index (0-based, number of rows read so far)
        /// </summary>
        public int Index
        {
            get { return (int)NativeMethods.bcsv_reader_index(Handle); }
        }

        /// <summary>
        /// Open a BCSV file for reading
        /// </summary>
        /// <param name="filename">Path to the BCSV file</param>
        /// <returns>True if successful</returns>
        public bool Open(string filename)
        {
            if (string.IsNullOrEmpty(filename))
                throw new ArgumentNullException(nameof(filename));

            return NativeMethods.bcsv_reader_open(Handle, filename);
        }

        /// <summary>
        /// Close the currently open file
        /// </summary>
        public void Close()
        {
            NativeMethods.bcsv_reader_close(Handle);
        }

        /// <summary>
        /// Read the next row from the file
        /// </summary>
        /// <returns>True if a row was read, false if end of file</returns>
        public bool Next()
        {
            return NativeMethods.bcsv_reader_next(Handle);
        }

        /// <summary>
        /// Read all rows from the file using a callback function
        /// </summary>
        /// <param name="rowCallback">Function to call for each row</param>
        /// <returns>Number of rows processed</returns>
        public int ReadAll(Action<BcsvRowRefConst, int> rowCallback)
        {
            if (rowCallback == null)
                throw new ArgumentNullException(nameof(rowCallback));

            int rowCount = 0;
            while (Next())
            {
                rowCallback(Row, Index);
                rowCount++;
            }
            return rowCount;
        }

        /// <summary>
        /// Read all rows from the file and return them as an array
        /// </summary>
        /// <returns>Array of row data as object arrays</returns>
        public object[][] ReadAllRows()
        {
            if (!IsOpen || Layout == null)
                throw new InvalidOperationException("No file is open");

            var rows = new System.Collections.Generic.List<object[]>();
            var layout = Layout;
            int columnCount = layout.ColumnCount;

            while (Next())
            {
                var row = Row;
                var rowData = new object[columnCount];

                for (int i = 0; i < columnCount; i++)
                {
                    var columnType = layout.GetColumnType(i);
                    switch (columnType)
                    {
                        case ColumnType.Bool:
                            rowData[i] = row.GetBool(i);
                            break;
                        case ColumnType.UInt8:
                            rowData[i] = row.GetUInt8(i);
                            break;
                        case ColumnType.UInt16:
                            rowData[i] = row.GetUInt16(i);
                            break;
                        case ColumnType.UInt32:
                            rowData[i] = row.GetUInt32(i);
                            break;
                        case ColumnType.UInt64:
                            rowData[i] = row.GetUInt64(i);
                            break;
                        case ColumnType.Int8:
                            rowData[i] = row.GetInt8(i);
                            break;
                        case ColumnType.Int16:
                            rowData[i] = row.GetInt16(i);
                            break;
                        case ColumnType.Int32:
                            rowData[i] = row.GetInt32(i);
                            break;
                        case ColumnType.Int64:
                            rowData[i] = row.GetInt64(i);
                            break;
                        case ColumnType.Float:
                            rowData[i] = row.GetFloat(i);
                            break;
                        case ColumnType.Double:
                            rowData[i] = row.GetDouble(i);
                            break;
                        case ColumnType.String:
                            rowData[i] = row.GetString(i);
                            break;
                        default:
                            rowData[i] = null;
                            break;
                    }
                }
                rows.Add(rowData);
            }

            return rows.ToArray();
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
            GC.SuppressFinalize(this); // Prevent finalizer from running
        }

        protected virtual void Dispose(bool disposing)
        {
            if (handle != IntPtr.Zero)
            {
                if (disposing)
                {
                    // Disposing from user code - safe to call other methods
                    Close(); // Close file if open
                }
                // Always clean up native resources
                NativeMethods.bcsv_reader_destroy(handle);
                handle = IntPtr.Zero;
            }
        }

        ~BcsvReader()
        {
            // Finalizer safety net for forgotten Dispose() calls
            Dispose(false);
        }
    }
}