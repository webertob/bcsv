// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Runtime.CompilerServices;
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

    // ── Error ──────────────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_last_error();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_clear_last_error();

    // ── Lifecycle ──────────────────────────────────────────────────────
    // Bulk teardown of every live handle (close writers, free everything).
    // Call only once per process, when the host is done with BCSV for good —
    // typically from an atexit/AppDomain hook. Idempotent; destroys of
    // handles taken before shutdown become logged no-ops afterwards.
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_shutdown();

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
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_is_poisoned(nint writer);

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

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int bcsv_writer_file_flags(nint writer);

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

    // Checked scalar getters: strict type match (see bcsv_c_api.h, ADR-0006).
    // false → *out untouched, error channel set. Use ThrowIfError to surface.
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_bool(nint row, int col,
        [MarshalAs(UnmanagedType.U1)] out bool value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_uint8(nint row, int col, out byte value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_uint16(nint row, int col, out ushort value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_uint32(nint row, int col, out uint value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_uint64(nint row, int col, out ulong value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_int8(nint row, int col, out sbyte value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_int16(nint row, int col, out short value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_int32(nint row, int col, out int value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_int64(nint row, int col, out long value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_float(nint row, int col, out float value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_double(nint row, int col, out double value);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_row_try_get_string(nint row, int col, out IntPtr value);

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

    // ── Unified File API (ADR-0009) ────────────────────────────────────
    // One open path for .bcsv / .csv / .csv.lz4. The managed CSV wrappers
    // (BcsvCsvReader/BcsvCsvWriter) bind these entry points; the legacy CSV
    // family was retired in the 2.0.0 trim (see bcsv_c_api.h).

    /// <summary>Classifies an existing file by magic; false + bcsv_last_error for unreadable paths.</summary>
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_probe_format(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename, out BcsvFormat outFormat);

    /// <summary>Row codec for bcsv_writer_create_u (the .bcsv member codec). Mirrors bcsv_row_codec_t.</summary>
    internal enum BcsvRowCodec
    {
        Flat = 0,
        ZoH = 1,
        Delta = 2,
    }

    /// <summary>File format for the unified open. Mirrors bcsv_format_t.</summary>
    internal enum BcsvFormat
    {
        Auto = 0,
        Bcsv = 1,
        Csv = 2,
        CsvLz4 = 3,
    }

    /// <summary>
    /// Mirrors bcsv_open_opts (bcsv_c_api.h) field-for-field — sequential
    /// layout, no Pack. size_t→nint, char→byte, handle→nint; tri-state ints
    /// keep their C -1 semantics, so they stay int (never bool).
    /// struct_size must be set via NewDefault() at every construction site.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct BcsvOpenOpts
    {
        public nint struct_size;
        public int rebuild_footer;
        public nint csv_layout;
        public byte delimiter;
        public byte decimal_sep;
        public int has_header;
        public int include_header;
        public int compression;
        public int block_size_kb;
        public int file_flags;
        public int lz4_block_id;
        public int lz4_level;

        /// <summary>All-tri-state-unset opts (== C BCSV_OPEN_OPTS_INIT).</summary>
        public static BcsvOpenOpts NewDefault()
        {
            var o = new BcsvOpenOpts
            {
                rebuild_footer = -1,
                has_header = -1,
                include_header = -1,
                compression = -1,
                block_size_kb = -1,
                file_flags = -1,
                lz4_block_id = -1,
                lz4_level = -1,
            };
            o.struct_size = Unsafe.SizeOf<BcsvOpenOpts>();
            return o;
        }
    }

    // ── Unified Reader ─────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_reader_create_u();

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_reader_destroy_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_reader_close_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_is_open_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_next_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_reader_row_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_index_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_reader_file_line_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_reader_layout_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern BcsvFormat bcsv_reader_format_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_reader_filename_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_reader_error_msg_u(nint reader);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_reader_open_u(nint reader,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        BcsvFormat format, ref BcsvOpenOpts opts);

    // ── Unified Writer ─────────────────────────────────────────────────
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_create_u(nint layout, BcsvRowCodec rowCodec);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_writer_destroy_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void bcsv_writer_close_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_is_open_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_next_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_write_u(nint writer, nint row);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_row_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint bcsv_writer_index_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint bcsv_writer_layout_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern BcsvFormat bcsv_writer_format_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_writer_filename_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr bcsv_writer_error_msg_u(nint writer);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool bcsv_writer_open_u(nint writer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        [MarshalAs(UnmanagedType.U1)] bool overwrite,
        BcsvFormat format, ref BcsvOpenOpts opts);

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

    /// <summary>Filename helper: C API returns wchar_t* on Windows, char* elsewhere.</summary>
    internal static string PtrToStringAuto(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero) return string.Empty;
        return OperatingSystem.IsWindows()
            ? Marshal.PtrToStringUni(ptr) ?? string.Empty
            : Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
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
