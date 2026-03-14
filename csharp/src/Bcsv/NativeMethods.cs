// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Runtime.InteropServices;

namespace Bcsv;

/// <summary>Raw P/Invoke declarations for the BCSV C API (libbcsv_c_api).</summary>
internal static partial class NativeMethods
{
    private const string LibName = "bcsv_c_api";

    // ── Version ────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_version();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int bcsv_version_major();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int bcsv_version_minor();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int bcsv_version_patch();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_format_version();

    // ── Error ──────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_last_error();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_clear_last_error();

    // ── Layout ─────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_layout_create();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_layout_clone(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_layout_destroy(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_layout_has_column(nint layout,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_layout_column_count(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern nuint bcsv_layout_column_index(nint layout,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_layout_column_name(nint layout, nuint index);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ColumnType bcsv_layout_column_type(nint layout, nuint index);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_layout_set_column_name(nint layout, nuint index,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_layout_set_column_type(nint layout, nuint index, ColumnType type);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_layout_add_column(nint layout, nuint index,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name, ColumnType type);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_layout_remove_column(nint layout, nuint index);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_layout_clear(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_layout_is_compatible(nint layout1, nint layout2);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_layout_assign(nint dest, nint src);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_layout_column_count_by_type(nint layout, ColumnType type);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_layout_to_string(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_layout_row_data_size(nint layout);

    // ── Reader ─────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_reader_create();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_reader_destroy(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_reader_close(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_open(nint reader,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_open_ex(nint reader,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        [MarshalAs(UnmanagedType.U1)] bool rebuildFooter);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_is_open(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_reader_filename(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_reader_layout(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_next(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_read(nint reader, nuint index);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_reader_row(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_index(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_count_rows(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_reader_error_msg(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte bcsv_reader_compression_level(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int bcsv_reader_file_flags(nint reader);

    // ── Writer ─────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_create(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_create_zoh(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_create_delta(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_writer_destroy(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_writer_close(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_writer_flush(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_open(nint writer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        [MarshalAs(UnmanagedType.U1)] bool overwrite,
        int compress, int blockSizeKb, int flags);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_is_open(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_writer_filename(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_layout(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_next(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_write(nint writer, nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_row(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_writer_index(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_writer_error_msg(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte bcsv_writer_compression_level(nint writer);

    // ── Row ────────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_row_create(nint layout);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_row_clone(nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_destroy(nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_clear(nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_assign(nint dest, nint src);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_row_layout(nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_row_column_count(nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_row_to_string(nint row);

    // Scalar getters
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_get_bool(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte bcsv_row_get_uint8(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ushort bcsv_row_get_uint16(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint bcsv_row_get_uint32(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong bcsv_row_get_uint64(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern sbyte bcsv_row_get_int8(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern short bcsv_row_get_int16(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int bcsv_row_get_int32(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern long bcsv_row_get_int64(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern float bcsv_row_get_float(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern double bcsv_row_get_double(nint row, int col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_row_get_string(nint row, int col);

    // Scalar setters
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_bool(nint row, int col, [MarshalAs(UnmanagedType.U1)] bool value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint8(nint row, int col, byte value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint16(nint row, int col, ushort value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint32(nint row, int col, uint value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint64(nint row, int col, ulong value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int8(nint row, int col, sbyte value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int16(nint row, int col, short value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int32(nint row, int col, int value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int64(nint row, int col, long value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_float(nint row, int col, float value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_double(nint row, int col, double value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern void bcsv_row_set_string(nint row, int col,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

    // Array getters
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_bool_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_uint8_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_uint16_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_int8_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_int16_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_double_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_float_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_int32_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_int64_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_uint32_array(nint row, int startCol, IntPtr dst, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_get_uint64_array(nint row, int startCol, IntPtr dst, nuint count);

    // Array setters
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_bool_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint8_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint16_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint32_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_uint64_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int8_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int16_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_double_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_float_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int32_array(nint row, int startCol, IntPtr src, nuint count);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_set_int64_array(nint row, int startCol, IntPtr src, nuint count);

    // Visitor
    internal delegate void VisitCallback(nuint colIndex, ColumnType colType, IntPtr value, IntPtr userData);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_row_visit_const(nint row, nuint startCol, nuint count,
        VisitCallback cb, IntPtr userData);

    // ── CSV Reader ─────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_csv_reader_create(nint layout, byte delimiter, byte decimalSep);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_csv_reader_destroy(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_reader_open(nint reader,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        [MarshalAs(UnmanagedType.U1)] bool hasHeader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_csv_reader_close(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_reader_is_open(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_csv_reader_layout(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_reader_next(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_csv_reader_row(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_csv_reader_index(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_csv_reader_error_msg(nint reader);

    // ── CSV Writer ─────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_csv_writer_create(nint layout, byte delimiter, byte decimalSep);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_csv_writer_destroy(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_writer_open(nint writer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        [MarshalAs(UnmanagedType.U1)] bool overwrite,
        [MarshalAs(UnmanagedType.U1)] bool includeHeader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_csv_writer_close(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_writer_is_open(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_csv_writer_layout(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_writer_next(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_csv_writer_write(nint writer, nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_csv_writer_row(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_csv_writer_index(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_csv_writer_error_msg(nint writer);

    // ── Sampler ────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_sampler_create(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_sampler_destroy(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_sampler_set_conditional(nint sampler,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string expr);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_sampler_set_selection(nint sampler,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string expr);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_sampler_get_conditional(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_sampler_get_selection(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_sampler_set_mode(nint sampler, SamplerMode mode);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern SamplerMode bcsv_sampler_get_mode(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_sampler_next(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_sampler_row(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_sampler_output_layout(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_sampler_source_row_pos(nint sampler);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_sampler_error_msg(nint sampler);

    // ── Columnar Bulk I/O ──────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_read_columns(nint reader, IntPtr[] bufs,
        nuint numCols, nuint maxRows);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_reader_column_string(nint reader, nuint col, nuint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_column_string_count(nint reader, nuint col);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_column_strings_packed(nint reader, nuint col,
        IntPtr outBuf, nuint bufSize);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_write_columns(nint writer, IntPtr[] bufs,
        nuint numCols, nuint numRows);

    // ── Helpers ────────────────────────────────────────────────────────
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
