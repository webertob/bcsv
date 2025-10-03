using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace BCSV
{
    /// <summary>
    /// Represents a single row of data in a BCSV file
    /// </summary>
    public class BcsvRow
    {
        private IntPtr handle;
        private bool isOwned; // True if this row owns the native handle

        internal BcsvRow(IntPtr nativeHandle, bool owned = false)
        {
            handle = nativeHandle;
            isOwned = owned;
        }

        /// <summary>
        /// Internal handle for native calls
        /// </summary>
        internal IntPtr Handle
        {
            get
            {
                if (handle == IntPtr.Zero)
                    throw new ObjectDisposedException("BcsvRow");
                return handle;
            }
        }

        /// <summary>
        /// Get the layout for this row
        /// </summary>
        public BcsvLayout Layout
        {
            get
            {
                var layoutHandle = NativeMethods.bcsv_row_layout(Handle);
                return layoutHandle == IntPtr.Zero ? null : new BcsvLayout(layoutHandle);
            }
        }

        #region Single Value Getters

        /// <summary>
        /// Get a boolean value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Boolean value</returns>
        public bool GetBool(int column)
        {
            return NativeMethods.bcsv_row_get_bool(Handle, column);
        }

        /// <summary>
        /// Get a byte value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Byte value</returns>
        public byte GetUInt8(int column)
        {
            return NativeMethods.bcsv_row_get_uint8(Handle, column);
        }

        /// <summary>
        /// Get a ushort value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>UShort value</returns>
        public ushort GetUInt16(int column)
        {
            return NativeMethods.bcsv_row_get_uint16(Handle, column);
        }

        /// <summary>
        /// Get a uint value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>UInt value</returns>
        public uint GetUInt32(int column)
        {
            return NativeMethods.bcsv_row_get_uint32(Handle, column);
        }

        /// <summary>
        /// Get a ulong value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>ULong value</returns>
        public ulong GetUInt64(int column)
        {
            return NativeMethods.bcsv_row_get_uint64(Handle, column);
        }

        /// <summary>
        /// Get a sbyte value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>SByte value</returns>
        public sbyte GetInt8(int column)
        {
            return NativeMethods.bcsv_row_get_int8(Handle, column);
        }

        /// <summary>
        /// Get a short value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Short value</returns>
        public short GetInt16(int column)
        {
            return NativeMethods.bcsv_row_get_int16(Handle, column);
        }

        /// <summary>
        /// Get an int value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Int value</returns>
        public int GetInt32(int column)
        {
            return NativeMethods.bcsv_row_get_int32(Handle, column);
        }

        /// <summary>
        /// Get a long value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Long value</returns>
        public long GetInt64(int column)
        {
            return NativeMethods.bcsv_row_get_int64(Handle, column);
        }

        /// <summary>
        /// Get a float value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Float value</returns>
        public float GetFloat(int column)
        {
            return NativeMethods.bcsv_row_get_float(Handle, column);
        }

        /// <summary>
        /// Get a double value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>Double value</returns>
        public double GetDouble(int column)
        {
            return NativeMethods.bcsv_row_get_double(Handle, column);
        }

        /// <summary>
        /// Get a string value from the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <returns>String value</returns>
        public string GetString(int column)
        {
            var ptr = NativeMethods.bcsv_row_get_string(Handle, column);
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        #endregion

        #region Single Value Setters

        /// <summary>
        /// Set a boolean value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetBool(int column, bool value)
        {
            NativeMethods.bcsv_row_set_bool(Handle, column, value);
        }

        /// <summary>
        /// Set a byte value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetUInt8(int column, byte value)
        {
            NativeMethods.bcsv_row_set_uint8(Handle, column, value);
        }

        /// <summary>
        /// Set a ushort value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetUInt16(int column, ushort value)
        {
            NativeMethods.bcsv_row_set_uint16(Handle, column, value);
        }

        /// <summary>
        /// Set a uint value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetUInt32(int column, uint value)
        {
            NativeMethods.bcsv_row_set_uint32(Handle, column, value);
        }

        /// <summary>
        /// Set a ulong value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetUInt64(int column, ulong value)
        {
            NativeMethods.bcsv_row_set_uint64(Handle, column, value);
        }

        /// <summary>
        /// Set a sbyte value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetInt8(int column, sbyte value)
        {
            NativeMethods.bcsv_row_set_int8(Handle, column, value);
        }

        /// <summary>
        /// Set a short value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetInt16(int column, short value)
        {
            NativeMethods.bcsv_row_set_int16(Handle, column, value);
        }

        /// <summary>
        /// Set an int value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetInt32(int column, int value)
        {
            NativeMethods.bcsv_row_set_int32(Handle, column, value);
        }

        /// <summary>
        /// Set a long value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetInt64(int column, long value)
        {
            NativeMethods.bcsv_row_set_int64(Handle, column, value);
        }

        /// <summary>
        /// Set a float value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetFloat(int column, float value)
        {
            NativeMethods.bcsv_row_set_float(Handle, column, value);
        }

        /// <summary>
        /// Set a double value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetDouble(int column, double value)
        {
            NativeMethods.bcsv_row_set_double(Handle, column, value);
        }

        /// <summary>
        /// Set a string value in the specified column
        /// </summary>
        /// <param name="column">Column index</param>
        /// <param name="value">Value to set</param>
        public void SetString(int column, string value)
        {
            NativeMethods.bcsv_row_set_string(Handle, column, value);
        }

        #endregion

        #region Array Getters

        /// <summary>
        /// Get multiple boolean values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array to fill with values</param>
        /// <param name="count">Number of values to read</param>
        public void GetBoolArray(int startColumn, bool[] values, int count)
        {
            NativeMethods.bcsv_row_get_bool_array(Handle, startColumn, values, (UIntPtr)count);
        }

        /// <summary>
        /// Get multiple byte values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array to fill with values</param>
        /// <param name="count">Number of values to read</param>
        public void GetUInt8Array(int startColumn, byte[] values, int count)
        {
            NativeMethods.bcsv_row_get_uint8_array(Handle, startColumn, values, (UIntPtr)count);
        }

        /// <summary>
        /// Get multiple float values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array to fill with values</param>
        /// <param name="count">Number of values to read</param>
        public void GetFloatArray(int startColumn, float[] values, int count)
        {
            NativeMethods.bcsv_row_get_float_array(Handle, startColumn, values, (UIntPtr)count);
        }

        /// <summary>
        /// Get multiple int values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array to fill with values</param>
        /// <param name="count">Number of values to read</param>
        public void GetInt32Array(int startColumn, int[] values, int count)
        {
            NativeMethods.bcsv_row_get_int32_array(Handle, startColumn, values, (UIntPtr)count);
        }

        #endregion

        #region Array Setters

        /// <summary>
        /// Set multiple boolean values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array of values to set</param>
        /// <param name="count">Number of values to write</param>
        public void SetBoolArray(int startColumn, bool[] values, int count)
        {
            NativeMethods.bcsv_row_set_bool_array(Handle, startColumn, values, (UIntPtr)count);
        }

        /// <summary>
        /// Set multiple byte values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array of values to set</param>
        /// <param name="count">Number of values to write</param>
        public void SetUInt8Array(int startColumn, byte[] values, int count)
        {
            NativeMethods.bcsv_row_set_uint8_array(Handle, startColumn, values, (UIntPtr)count);
        }

        /// <summary>
        /// Set multiple float values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array of values to set</param>
        /// <param name="count">Number of values to write</param>
        public void SetFloatArray(int startColumn, float[] values, int count)
        {
            NativeMethods.bcsv_row_set_float_array(Handle, startColumn, values, (UIntPtr)count);
        }

        /// <summary>
        /// Set multiple int values starting from the specified column
        /// </summary>
        /// <param name="startColumn">Starting column index</param>
        /// <param name="values">Array of values to set</param>
        /// <param name="count">Number of values to write</param>
        public void SetInt32Array(int startColumn, int[] values, int count)
        {
            NativeMethods.bcsv_row_set_int32_array(Handle, startColumn, values, (UIntPtr)count);
        }

        #endregion
    }
}