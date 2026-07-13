// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

using System;
using System.Runtime.InteropServices;

namespace BCSV
{
    /// <summary>
    /// Provides typed get/set access to a single BCSV row.
    /// Non-owning wrapper — always references a Reader's or Writer's internal row.
    /// </summary>
    public readonly struct BcsvRow
    {
        internal readonly nint Handle;

        internal BcsvRow(nint handle) => Handle = handle;

        public int ColumnCount => (int)NativeMethods.bcsv_row_column_count(Handle);

        // ── Typed getters ──────────────────────────────────────────────
        public bool   GetBool(int col)   => NativeMethods.bcsv_row_get_bool(Handle, col);
        public byte   GetUInt8(int col)  => NativeMethods.bcsv_row_get_uint8(Handle, col);
        public ushort GetUInt16(int col) => NativeMethods.bcsv_row_get_uint16(Handle, col);
        public uint   GetUInt32(int col) => NativeMethods.bcsv_row_get_uint32(Handle, col);
        public ulong  GetUInt64(int col) => NativeMethods.bcsv_row_get_uint64(Handle, col);
        public sbyte  GetInt8(int col)   => NativeMethods.bcsv_row_get_int8(Handle, col);
        public short  GetInt16(int col)  => NativeMethods.bcsv_row_get_int16(Handle, col);
        public int    GetInt32(int col)  => NativeMethods.bcsv_row_get_int32(Handle, col);
        public long   GetInt64(int col)  => NativeMethods.bcsv_row_get_int64(Handle, col);
        public float  GetFloat(int col)  => NativeMethods.bcsv_row_get_float(Handle, col);
        public double GetDouble(int col) => NativeMethods.bcsv_row_get_double(Handle, col);
        public string GetString(int col) =>
            NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_row_get_string(Handle, col));

        // ── Typed setters ──────────────────────────────────────────────
        public void SetBool(int col, bool value)     => NativeMethods.bcsv_row_set_bool(Handle, col, value);
        public void SetUInt8(int col, byte value)    => NativeMethods.bcsv_row_set_uint8(Handle, col, value);
        public void SetUInt16(int col, ushort value) => NativeMethods.bcsv_row_set_uint16(Handle, col, value);
        public void SetUInt32(int col, uint value)   => NativeMethods.bcsv_row_set_uint32(Handle, col, value);
        public void SetUInt64(int col, ulong value)  => NativeMethods.bcsv_row_set_uint64(Handle, col, value);
        public void SetInt8(int col, sbyte value)    => NativeMethods.bcsv_row_set_int8(Handle, col, value);
        public void SetInt16(int col, short value)   => NativeMethods.bcsv_row_set_int16(Handle, col, value);
        public void SetInt32(int col, int value)     => NativeMethods.bcsv_row_set_int32(Handle, col, value);
        public void SetInt64(int col, long value)    => NativeMethods.bcsv_row_set_int64(Handle, col, value);
        public void SetFloat(int col, float value)   => NativeMethods.bcsv_row_set_float(Handle, col, value);
        public void SetDouble(int col, double value) => NativeMethods.bcsv_row_set_double(Handle, col, value);
        public void SetString(int col, string value) => NativeMethods.bcsv_row_set_string(Handle, col, value);

        // ── Generic get/set ────────────────────────────────────────────
        public T Get<T>(int col)
        {
            if (typeof(T) == typeof(bool))   return (T)(object)GetBool(col);
            if (typeof(T) == typeof(byte))   return (T)(object)GetUInt8(col);
            if (typeof(T) == typeof(ushort)) return (T)(object)GetUInt16(col);
            if (typeof(T) == typeof(uint))   return (T)(object)GetUInt32(col);
            if (typeof(T) == typeof(ulong))  return (T)(object)GetUInt64(col);
            if (typeof(T) == typeof(sbyte))  return (T)(object)GetInt8(col);
            if (typeof(T) == typeof(short))  return (T)(object)GetInt16(col);
            if (typeof(T) == typeof(int))    return (T)(object)GetInt32(col);
            if (typeof(T) == typeof(long))   return (T)(object)GetInt64(col);
            if (typeof(T) == typeof(float))  return (T)(object)GetFloat(col);
            if (typeof(T) == typeof(double)) return (T)(object)GetDouble(col);
            if (typeof(T) == typeof(string)) return (T)(object)GetString(col);
            throw new BcsvException($"Unsupported type: {typeof(T)}");
        }

        public void Set<T>(int col, T value)
        {
            switch (value)
            {
                case bool v:   SetBool(col, v); break;
                case byte v:   SetUInt8(col, v); break;
                case ushort v: SetUInt16(col, v); break;
                case uint v:   SetUInt32(col, v); break;
                case ulong v:  SetUInt64(col, v); break;
                case sbyte v:  SetInt8(col, v); break;
                case short v:  SetInt16(col, v); break;
                case int v:    SetInt32(col, v); break;
                case long v:   SetInt64(col, v); break;
                case float v:  SetFloat(col, v); break;
                case double v: SetDouble(col, v); break;
                case string v: SetString(col, v); break;
                default: throw new BcsvException($"Unsupported type: {typeof(T)}");
            }
        }

        // ── Array access (Span-based, zero-copy via IL2CPP) ────────────
        public unsafe void GetBools(int startCol, Span<bool> dst)
        {
            fixed (bool* p = dst)
                NativeMethods.bcsv_row_get_bool_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetBools(int startCol, ReadOnlySpan<bool> src)
        {
            fixed (bool* p = src)
                NativeMethods.bcsv_row_set_bool_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetUInt8s(int startCol, Span<byte> dst)
        {
            fixed (byte* p = dst)
                NativeMethods.bcsv_row_get_uint8_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetUInt8s(int startCol, ReadOnlySpan<byte> src)
        {
            fixed (byte* p = src)
                NativeMethods.bcsv_row_set_uint8_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetUInt16s(int startCol, Span<ushort> dst)
        {
            fixed (ushort* p = dst)
                NativeMethods.bcsv_row_get_uint16_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetUInt16s(int startCol, ReadOnlySpan<ushort> src)
        {
            fixed (ushort* p = src)
                NativeMethods.bcsv_row_set_uint16_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetUInt32s(int startCol, Span<uint> dst)
        {
            fixed (uint* p = dst)
                NativeMethods.bcsv_row_get_uint32_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetUInt32s(int startCol, ReadOnlySpan<uint> src)
        {
            fixed (uint* p = src)
                NativeMethods.bcsv_row_set_uint32_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetUInt64s(int startCol, Span<ulong> dst)
        {
            fixed (ulong* p = dst)
                NativeMethods.bcsv_row_get_uint64_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetUInt64s(int startCol, ReadOnlySpan<ulong> src)
        {
            fixed (ulong* p = src)
                NativeMethods.bcsv_row_set_uint64_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetInt8s(int startCol, Span<sbyte> dst)
        {
            fixed (sbyte* p = dst)
                NativeMethods.bcsv_row_get_int8_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetInt8s(int startCol, ReadOnlySpan<sbyte> src)
        {
            fixed (sbyte* p = src)
                NativeMethods.bcsv_row_set_int8_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetInt16s(int startCol, Span<short> dst)
        {
            fixed (short* p = dst)
                NativeMethods.bcsv_row_get_int16_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetInt16s(int startCol, ReadOnlySpan<short> src)
        {
            fixed (short* p = src)
                NativeMethods.bcsv_row_set_int16_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetInt32s(int startCol, Span<int> dst)
        {
            fixed (int* p = dst)
                NativeMethods.bcsv_row_get_int32_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetInt32s(int startCol, ReadOnlySpan<int> src)
        {
            fixed (int* p = src)
                NativeMethods.bcsv_row_set_int32_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetInt64s(int startCol, Span<long> dst)
        {
            fixed (long* p = dst)
                NativeMethods.bcsv_row_get_int64_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetInt64s(int startCol, ReadOnlySpan<long> src)
        {
            fixed (long* p = src)
                NativeMethods.bcsv_row_set_int64_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetFloats(int startCol, Span<float> dst)
        {
            fixed (float* p = dst)
                NativeMethods.bcsv_row_get_float_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetFloats(int startCol, ReadOnlySpan<float> src)
        {
            fixed (float* p = src)
                NativeMethods.bcsv_row_set_float_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public unsafe void GetDoubles(int startCol, Span<double> dst)
        {
            fixed (double* p = dst)
                NativeMethods.bcsv_row_get_double_array(Handle, startCol, (IntPtr)p, (nuint)dst.Length);
        }

        public unsafe void SetDoubles(int startCol, ReadOnlySpan<double> src)
        {
            fixed (double* p = src)
                NativeMethods.bcsv_row_set_double_array(Handle, startCol, (IntPtr)p, (nuint)src.Length);
        }

        public void Clear() => NativeMethods.bcsv_row_clear(Handle);

        public override string ToString()
            => NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_row_to_string(Handle));
    }
}