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

namespace BCSV
{
    // Enum for column types - must match bcsv_type_t in C API
    public enum ColumnType
    {
        Bool   = 0,
        UInt8  = 1,
        UInt16 = 2,
        UInt32 = 3,
        UInt64 = 4,
        Int8   = 5,
        Int16  = 6,
        Int32  = 7,
        Int64  = 8,
        Float  = 9,
        Double = 10,
        String = 11
    }

    public enum ReadMode
    {
        Strict    = 0,
        Resilient = 1
    }

    public enum FileFlags     {
        None = 0,
        ZOH  = 1 << 0,
    }
    // Native P/Invoke declarations
    internal static class NativeMethods
    {
        const string DllName = "bcsv_c_api"; // Adjust this to match your actual DLL name

        // Layout API
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_layout_create();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_layout_clone(IntPtr layout);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_destroy(IntPtr layout);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_layout_has_column(IntPtr layout, string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern UIntPtr bcsv_layout_column_count(IntPtr layout);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern UIntPtr bcsv_layout_column_index(IntPtr layout, string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_layout_column_name(IntPtr layout, UIntPtr index);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ColumnType bcsv_layout_column_type(IntPtr layout, UIntPtr index);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_layout_set_column_name(IntPtr layout, UIntPtr index, string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_set_column_type(IntPtr layout, UIntPtr index, ColumnType type);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_layout_add_column(IntPtr layout, UIntPtr index, string name, ColumnType type);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_remove_column(IntPtr layout, UIntPtr index);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_clear(IntPtr layout);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_layout_isCompatible(IntPtr layout1, IntPtr layout2);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_assign(IntPtr dest, IntPtr src);

        // Reader API
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_create(ReadMode mode);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_reader_destroy(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_reader_close(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong bcsv_reader_count_rows(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_reader_open(IntPtr reader, string filename);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_reader_is_open(IntPtr reader);

        // Platform-specific filename functions
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_filename(IntPtr reader); // Returns wchar_t* on Windows
#else
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_filename(IntPtr reader); // Returns char* on POSIX
#endif

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_layout(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_reader_next(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_row(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern UIntPtr bcsv_reader_index(IntPtr reader);

        // Writer API
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_create(IntPtr layout);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_writer_destroy(IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_writer_close(IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_writer_flush(IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_writer_open(IntPtr writer, string filename, bool overwrite, int compression, int block_size_kb, FileFlags flag);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_writer_is_open(IntPtr writer);

        // Platform-specific filename functions
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_filename(IntPtr writer); // Returns wchar_t* on Windows
#else
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_filename(IntPtr writer); // Returns char* on POSIX
#endif

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_layout(IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_writer_next(IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_row(IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern UIntPtr bcsv_writer_index(IntPtr writer);

        // Row API - Lifecycle
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_row_create(IntPtr layout);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_row_clone(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_destroy(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_clear(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_assign(IntPtr dest, IntPtr src);

        // Row API - Change tracking
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_row_has_any_changes(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_row_tracks_changes(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_changes(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_reset_changes(IntPtr row);

        // Row API - Single value access
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_row_layout(IntPtr row);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern bool bcsv_row_get_bool(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern byte bcsv_row_get_uint8(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ushort bcsv_row_get_uint16(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint bcsv_row_get_uint32(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong bcsv_row_get_uint64(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern sbyte bcsv_row_get_int8(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern short bcsv_row_get_int16(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int bcsv_row_get_int32(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern long bcsv_row_get_int64(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float bcsv_row_get_float(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double bcsv_row_get_double(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_row_get_string(IntPtr row, int col);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_bool(IntPtr row, int col, bool value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint8(IntPtr row, int col, byte value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint16(IntPtr row, int col, ushort value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint32(IntPtr row, int col, uint value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint64(IntPtr row, int col, ulong value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int8(IntPtr row, int col, sbyte value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int16(IntPtr row, int col, short value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int32(IntPtr row, int col, int value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int64(IntPtr row, int col, long value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_float(IntPtr row, int col, float value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_double(IntPtr row, int col, double value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_string(IntPtr row, int col, string value);

        // Row API - Array access
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_bool_array(IntPtr row, int start_col, bool[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint8_array(IntPtr row, int start_col, byte[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint16_array(IntPtr row, int start_col, ushort[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint32_array(IntPtr row, int start_col, uint[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint64_array(IntPtr row, int start_col, ulong[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int8_array(IntPtr row, int start_col, sbyte[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int16_array(IntPtr row, int start_col, short[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int32_array(IntPtr row, int start_col, int[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int64_array(IntPtr row, int start_col, long[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_float_array(IntPtr row, int start_col, float[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_double_array(IntPtr row, int start_col, double[] dst, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_bool_array(IntPtr row, int start_col, bool[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint8_array(IntPtr row, int start_col, byte[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint16_array(IntPtr row, int start_col, ushort[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint32_array(IntPtr row, int start_col, uint[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint64_array(IntPtr row, int start_col, ulong[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int8_array(IntPtr row, int start_col, sbyte[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int16_array(IntPtr row, int start_col, short[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int32_array(IntPtr row, int start_col, int[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int64_array(IntPtr row, int start_col, long[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_float_array(IntPtr row, int start_col, float[] src, UIntPtr count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_double_array(IntPtr row, int start_col, double[] src, UIntPtr count);

        // Error handling
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_last_error();
    }

    /// <summary>
    /// Helper methods for cross-platform filename handling
    /// Provides unified string interface while using platform-specific native calls
    /// </summary>
    internal static class FilenameHelper
    {
        /// <summary>
        /// Get reader filename as string, handling platform differences
        /// </summary>
        internal static string GetReaderFilename(IntPtr reader)
        {
            IntPtr filenamePtr = NativeMethods.bcsv_reader_filename(reader);
            if (filenamePtr == IntPtr.Zero)
                return null;

#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            // On Windows, convert from wchar_t* to string
            return Marshal.PtrToStringUni(filenamePtr);
#else
            // On POSIX, convert from char* to string
            return Marshal.PtrToStringAnsi(filenamePtr);
#endif
        }

        /// <summary>
        /// Get writer filename as string, handling platform differences
        /// </summary>
        internal static string GetWriterFilename(IntPtr writer)
        {
            IntPtr filenamePtr = NativeMethods.bcsv_writer_filename(writer);
            if (filenamePtr == IntPtr.Zero)
                return null;

#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            // On Windows, convert from wchar_t* to string
            return Marshal.PtrToStringUni(filenamePtr);
#else
            // On POSIX, convert from char* to string
            return Marshal.PtrToStringAnsi(filenamePtr);
#endif
        }
    }
}