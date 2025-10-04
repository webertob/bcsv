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
    /// Layout defines the schema of BCSV files - column names and types
    /// </summary>
    public class BcsvLayout : IDisposable
    {
        private IntPtr handle;

        /// <summary>
        /// Create a new empty layout
        /// </summary>
        public BcsvLayout()
        {
            handle = NativeMethods.bcsv_layout_create();
            if (handle == IntPtr.Zero)
                throw new InvalidOperationException("Failed to create BCSV layout");
        }

        /// <summary>
        /// Create a layout by cloning an existing one
        /// </summary>
        /// <param name="other">Layout to clone</param>
        public BcsvLayout(BcsvLayout other)
        {
            if (other == null || other.handle == IntPtr.Zero)
                throw new ArgumentException("Source layout is null or disposed");
            
            handle = NativeMethods.bcsv_layout_clone(other.handle);
            if (handle == IntPtr.Zero)
                throw new InvalidOperationException("Failed to clone BCSV layout");
        }

        internal BcsvLayout(IntPtr nativeHandle)
        {
            handle = nativeHandle;
        }

        /// <summary>
        /// Internal handle for native calls
        /// </summary>
        internal IntPtr Handle
        {
            get
            {
                if (handle == IntPtr.Zero)
                    throw new ObjectDisposedException("BcsvLayout");
                return handle;
            }
        }

        /// <summary>
        /// Check if a column with the given name exists
        /// </summary>
        /// <param name="name">Column name</param>
        /// <returns>True if column exists</returns>
        public bool HasColumn(string name)
        {
            return NativeMethods.bcsv_layout_has_column(Handle, name);
        }

        /// <summary>
        /// Get the number of columns in this layout
        /// </summary>
        public int ColumnCount
        {
            get { return (int)NativeMethods.bcsv_layout_column_count(Handle); }
        }

        /// <summary>
        /// Get the index of a column by name
        /// </summary>
        /// <param name="name">Column name</param>
        /// <returns>Column index or -1 if not found</returns>
        public int GetColumnIndex(string name)
        {
            var result = NativeMethods.bcsv_layout_column_index(Handle, name);
            return result == UIntPtr.Zero ? -1 : (int)result;
        }

        /// <summary>
        /// Get the name of a column by index
        /// </summary>
        /// <param name="index">Column index</param>
        /// <returns>Column name</returns>
        public string GetColumnName(int index)
        {
            if (index < 0 || index >= ColumnCount)
                throw new ArgumentOutOfRangeException(nameof(index));

            var ptr = NativeMethods.bcsv_layout_column_name(Handle, (UIntPtr)index);
            return Marshal.PtrToStringAnsi(ptr);
        }

        /// <summary>
        /// Get the type of a column by index
        /// </summary>
        /// <param name="index">Column index</param>
        /// <returns>Column type</returns>
        public ColumnType GetColumnType(int index)
        {
            if (index < 0 || index >= ColumnCount)
                throw new ArgumentOutOfRangeException(nameof(index));

            return NativeMethods.bcsv_layout_column_type(Handle, (UIntPtr)index);
        }

        /// <summary>
        /// Set the name of a column
        /// </summary>
        /// <param name="index">Column index</param>
        /// <param name="name">New column name</param>
        /// <returns>True if successful</returns>
        public bool SetColumnName(int index, string name)
        {
            if (index < 0 || index >= ColumnCount)
                throw new ArgumentOutOfRangeException(nameof(index));

            return NativeMethods.bcsv_layout_set_column_name(Handle, (UIntPtr)index, name);
        }

        /// <summary>
        /// Set the type of a column
        /// </summary>
        /// <param name="index">Column index</param>
        /// <param name="type">New column type</param>
        public void SetColumnType(int index, ColumnType type)
        {
            if (index < 0 || index >= ColumnCount)
                throw new ArgumentOutOfRangeException(nameof(index));

            NativeMethods.bcsv_layout_set_column_type(Handle, (UIntPtr)index, type);
        }

        /// <summary>
        /// Add a new column at the specified index
        /// </summary>
        /// <param name="index">Index to insert at</param>
        /// <param name="name">Column name</param>
        /// <param name="type">Column type</param>
        /// <returns>True if successful</returns>
        public bool AddColumn(int index, string name, ColumnType type)
        {
            if (index < 0 || index > ColumnCount)
                throw new ArgumentOutOfRangeException(nameof(index));

            return NativeMethods.bcsv_layout_add_column(Handle, (UIntPtr)index, name, type);
        }

        /// <summary>
        /// Add a new column at the end
        /// </summary>
        /// <param name="name">Column name</param>
        /// <param name="type">Column type</param>
        /// <returns>True if successful</returns>
        public bool AddColumn(string name, ColumnType type)
        {
            return AddColumn(ColumnCount, name, type);
        }

        /// <summary>
        /// Remove a column by index
        /// </summary>
        /// <param name="index">Column index</param>
        public void RemoveColumn(int index)
        {
            if (index < 0 || index >= ColumnCount)
                throw new ArgumentOutOfRangeException(nameof(index));

            NativeMethods.bcsv_layout_remove_column(Handle, (UIntPtr)index);
        }

        /// <summary>
        /// Clear all columns from this layout
        /// </summary>
        public void Clear()
        {
            NativeMethods.bcsv_layout_clear(Handle);
        }

        /// <summary>
        /// Check if this layout is compatible with another layout
        /// </summary>
        /// <param name="other">Other layout to compare</param>
        /// <returns>True if layouts are compatible</returns>
        public bool IsCompatible(BcsvLayout other)
        {
            if (other == null || other.Handle == IntPtr.Zero)
                return false;

            return NativeMethods.bcsv_layout_isCompatible(Handle, other.Handle);
        }

        /// <summary>
        /// Copy the layout from another layout
        /// </summary>
        /// <param name="source">Source layout</param>
        public void AssignFrom(BcsvLayout source)
        {
            if (source == null || source.Handle == IntPtr.Zero)
                throw new ArgumentException("Source layout is null or disposed");

            NativeMethods.bcsv_layout_assign(Handle, source.Handle);
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
                // Always clean up native resources regardless of disposing flag
                NativeMethods.bcsv_layout_destroy(handle);
                handle = IntPtr.Zero;
            }
        }

        ~BcsvLayout()
        {
            // Finalizer safety net for forgotten Dispose() calls
            Dispose(false);
        }
    }
}