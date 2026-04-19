// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
// Full P/Invoke coverage matching csharp/src/Bcsv/NativeMethods.cs.

using System;
using System.Runtime.InteropServices;
using UnityEngine.Scripting;

namespace BCSV
{
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

    [System.Flags]
    public enum FileFlags
    {
        None           = 0,
        ZeroOrderHold  = 1 << 0,
        NoFileIndex    = 1 << 1,
        StreamMode     = 1 << 2,
        BatchCompress  = 1 << 3,
        DeltaEncoding  = 1 << 4,
    }

    public enum SamplerMode
    {
        Truncate = 0,
        Expand   = 1,
    }

    [Preserve]
    internal static class NativeMethods
    {
        const string Lib = "bcsv_c_api";

        // ── Version ────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_version();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int bcsv_version_major();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int bcsv_version_minor();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int bcsv_version_patch();

        // ── Error ──────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_last_error();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_clear_last_error();

        // ── Layout ─────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_layout_create();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_layout_clone(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_destroy(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_layout_has_column(nint layout,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_layout_column_count(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern nuint bcsv_layout_column_index(nint layout,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_layout_column_name(nint layout, nuint index);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ColumnType bcsv_layout_column_type(nint layout, nuint index);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_layout_set_column_name(nint layout, nuint index,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_set_column_type(nint layout, nuint index, ColumnType type);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_layout_add_column(nint layout, nuint index,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name, ColumnType type);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_remove_column(nint layout, nuint index);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_clear(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_layout_is_compatible(nint layout1, nint layout2);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_layout_assign(nint dest, nint src);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_layout_column_count_by_type(nint layout, ColumnType type);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_layout_to_string(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_layout_row_data_size(nint layout);

        // ── Reader ─────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_reader_create();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_reader_destroy(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_reader_open(nint reader,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string filename);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_reader_open_ex(nint reader,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
            [MarshalAs(UnmanagedType.U1)] bool rebuildFooter);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_reader_close(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_reader_is_open(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_filename(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_reader_layout(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_reader_next(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_reader_read(nint reader, nuint index);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_reader_row(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_reader_index(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_reader_count_rows(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_error_msg(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern byte bcsv_reader_compression_level(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int bcsv_reader_file_flags(nint reader);

        // ── Writer ─────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_writer_create(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_writer_create_zoh(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_writer_create_delta(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_writer_destroy(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_writer_close(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_writer_flush(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_writer_open(nint writer,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
            [MarshalAs(UnmanagedType.U1)] bool overwrite,
            int compress, int blockSizeKb, int flags);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_writer_is_open(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_filename(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_writer_layout(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_writer_next(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_writer_write(nint writer, nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_writer_row(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_writer_index(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_writer_error_msg(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern byte bcsv_writer_compression_level(nint writer);

        // ── Row ────────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_row_create(nint layout);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_row_clone(nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_destroy(nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_clear(nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_assign(nint dest, nint src);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_row_layout(nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_row_column_count(nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_row_to_string(nint row);

        // Scalar getters
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_row_get_bool(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern byte bcsv_row_get_uint8(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ushort bcsv_row_get_uint16(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint bcsv_row_get_uint32(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong bcsv_row_get_uint64(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern sbyte bcsv_row_get_int8(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern short bcsv_row_get_int16(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int bcsv_row_get_int32(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern long bcsv_row_get_int64(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float bcsv_row_get_float(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double bcsv_row_get_double(nint row, int col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_row_get_string(nint row, int col);

        // Scalar setters
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_bool(nint row, int col, [MarshalAs(UnmanagedType.U1)] bool value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint8(nint row, int col, byte value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint16(nint row, int col, ushort value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint32(nint row, int col, uint value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint64(nint row, int col, ulong value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int8(nint row, int col, sbyte value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int16(nint row, int col, short value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int32(nint row, int col, int value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int64(nint row, int col, long value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_float(nint row, int col, float value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_double(nint row, int col, double value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern void bcsv_row_set_string(nint row, int col,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        // Array getters
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_bool_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint8_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint16_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint32_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_uint64_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int8_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int16_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int32_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_int64_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_float_array(nint row, int startCol, IntPtr dst, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_get_double_array(nint row, int startCol, IntPtr dst, nuint count);

        // Array setters
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_bool_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint8_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint16_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint32_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_uint64_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int8_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int16_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int32_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_int64_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_float_array(nint row, int startCol, IntPtr src, nuint count);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_row_set_double_array(nint row, int startCol, IntPtr src, nuint count);

        // ── CSV Reader ─────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_csv_reader_create(nint layout, byte delimiter, byte decimalSep);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_csv_reader_destroy(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_reader_open(nint reader,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
            [MarshalAs(UnmanagedType.U1)] bool hasHeader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_csv_reader_close(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_reader_is_open(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_csv_reader_layout(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_reader_next(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_csv_reader_row(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_csv_reader_index(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_csv_reader_error_msg(nint reader);

        // ── CSV Writer ─────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_csv_writer_create(nint layout, byte delimiter, byte decimalSep);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_csv_writer_destroy(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_writer_open(nint writer,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
            [MarshalAs(UnmanagedType.U1)] bool overwrite,
            [MarshalAs(UnmanagedType.U1)] bool includeHeader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_csv_writer_close(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_writer_is_open(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_csv_writer_layout(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_writer_next(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_csv_writer_write(nint writer, nint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_csv_writer_row(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_csv_writer_index(nint writer);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_csv_writer_error_msg(nint writer);

        // ── Sampler ────────────────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_sampler_create(nint reader);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_sampler_destroy(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_sampler_set_conditional(nint sampler,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string expr);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_sampler_set_selection(nint sampler,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string expr);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_sampler_get_conditional(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_sampler_get_selection(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void bcsv_sampler_set_mode(nint sampler, SamplerMode mode);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern SamplerMode bcsv_sampler_get_mode(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_sampler_next(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_sampler_row(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint bcsv_sampler_output_layout(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_sampler_source_row_pos(nint sampler);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_sampler_error_msg(nint sampler);

        // ── Columnar Bulk I/O ──────────────────────────────────────────
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_reader_read_columns(nint reader, IntPtr[] bufs,
            nuint numCols, nuint maxRows);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr bcsv_reader_column_string(nint reader, nuint col, nuint row);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_reader_column_string_count(nint reader, nuint col);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nuint bcsv_reader_column_strings_packed(nint reader, nuint col,
            IntPtr outBuf, nuint bufSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool bcsv_writer_write_columns(nint writer, IntPtr[] bufs,
            nuint numCols, nuint numRows);

        // ── Helpers ────────────────────────────────────────────────────
        internal static string PtrToStringUtf8(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) return string.Empty;
            return Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
        }

        internal static void ThrowIfError(string context)
        {
            var errPtr = bcsv_last_error();
            var err = PtrToStringUtf8(errPtr);
            if (!string.IsNullOrEmpty(err))
            {
                bcsv_clear_last_error();
                throw new BcsvException($"{context}: {err}");
            }
        }

        internal static void ThrowWithError(string message, IntPtr errPtr)
        {
            var err = PtrToStringUtf8(errPtr);
            throw new BcsvException($"{message}: {err}");
        }
    }

    /// <summary>
    /// Cross-platform filename handling. Windows returns wchar_t*, POSIX returns char*.
    /// </summary>
    internal static class FilenameHelper
    {
        internal static string GetReaderFilename(nint reader)
        {
            IntPtr ptr = NativeMethods.bcsv_reader_filename(reader);
            if (ptr == IntPtr.Zero) return null;
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            return Marshal.PtrToStringUni(ptr);
#else
            return NativeMethods.PtrToStringUtf8(ptr);
#endif
        }

        internal static string GetWriterFilename(nint writer)
        {
            IntPtr ptr = NativeMethods.bcsv_writer_filename(writer);
            if (ptr == IntPtr.Zero) return null;
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            return Marshal.PtrToStringUni(ptr);
#else
            return NativeMethods.PtrToStringUtf8(ptr);
#endif
        }
    }
}