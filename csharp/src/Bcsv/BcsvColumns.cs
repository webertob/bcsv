// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Runtime.InteropServices;

namespace Bcsv;

/// <summary>
/// Columnar (column-oriented) bulk read/write for BCSV files.
/// Numeric columns use zero-copy pinned arrays. Entire I/O loop runs in C.
/// </summary>
public static class BcsvColumns
{
    /// <summary>Read an entire BCSV file into column arrays.</summary>
    public static ColumnData ReadColumns(string filename)
    {
        using var reader = new BcsvReader();
        reader.Open(filename, rebuildFooter: true);
        long totalRows = reader.RowCount;
        if (totalRows < 0) totalRows = 1_000_000; // fallback if footer missing
        return ReadColumnsFromHandle(reader.Handle, reader.Layout, (int)totalRows);
    }

    /// <summary>Shared columnar-read: pin numeric arrays, single P/Invoke read, packed string retrieval.</summary>
    internal static ColumnData ReadColumnsFromHandle(nint handle, BcsvLayout layout, int maxRows)
    {
        int numCols = layout.ColumnCount;

        var types = new ColumnType[numCols];
        var names = new string[numCols];
        var isString = new bool[numCols];
        for (int c = 0; c < numCols; c++)
        {
            types[c] = layout.ColumnType(c);
            names[c] = layout.ColumnName(c);
            isString[c] = types[c] == ColumnType.String;
        }

        var arrays = new Array[numCols];
        var gcHandles = new GCHandle[numCols];
        var bufPtrs = new IntPtr[numCols];

        try
        {
            for (int c = 0; c < numCols; c++)
            {
                if (isString[c])
                {
                    bufPtrs[c] = IntPtr.Zero;
                    continue;
                }
                arrays[c] = AllocateTypedArray(types[c], maxRows);
                gcHandles[c] = GCHandle.Alloc(arrays[c], GCHandleType.Pinned);
                bufPtrs[c] = gcHandles[c].AddrOfPinnedObject();
            }

            nuint rowsRead = NativeMethods.bcsv_reader_read_columns(
                handle, bufPtrs, (nuint)numCols, (nuint)maxRows);

            var stringColumns = new string[numCols][];
            for (int c = 0; c < numCols; c++)
            {
                if (!isString[c]) continue;
                int count = (int)NativeMethods.bcsv_reader_column_string_count(handle, (nuint)c);
                nuint totalSize = NativeMethods.bcsv_reader_column_strings_packed(
                    handle, (nuint)c, IntPtr.Zero, 0);
                if (totalSize == 0 || count == 0)
                {
                    stringColumns[c] = Array.Empty<string>();
                    continue;
                }
                var buf = new byte[(int)totalSize];
                unsafe
                {
                    fixed (byte* pBuf = buf)
                    {
                        NativeMethods.bcsv_reader_column_strings_packed(
                            handle, (nuint)c, (IntPtr)pBuf, totalSize);
                    }
                }
                stringColumns[c] = SplitPackedStrings(buf, count);
            }

            return new ColumnData(layout.Clone(), types, names, arrays,
                stringColumns, (int)rowsRead);
        }
        finally
        {
            for (int c = 0; c < numCols; c++)
                if (gcHandles[c].IsAllocated) gcHandles[c].Free();
        }
    }

    /// <summary>Write column arrays to a BCSV file.</summary>
    public static void WriteColumns(string filename, BcsvLayout layout,
        IDictionary<int, Array> columns, int rowCount,
        string rowCodec = "delta", int compression = 1,
        FileFlags flags = FileFlags.BatchCompress)
    {
        int numCols = layout.ColumnCount;
        var types = new ColumnType[numCols];
        var isString = new bool[numCols];
        for (int c = 0; c < numCols; c++)
        {
            types[c] = layout.ColumnType(c);
            isString[c] = types[c] == ColumnType.String;
        }

        using var writer = new BcsvWriter(layout, rowCodec);
        writer.Open(filename, overwrite: true, compression: compression, flags: flags);

        var gcHandles = new GCHandle[numCols];
        var bufPtrs = new IntPtr[numCols];
        var stringBufHandles = new List<GCHandle>(); // pinned contiguous UTF-8 buffers

        try
        {
            for (int c = 0; c < numCols; c++)
            {
                if (!columns.TryGetValue(c, out var arr))
                    throw new BcsvException($"Missing column data for index {c}");

                if (isString[c])
                {
                    // Single contiguous buffer for all strings (null-terminated)
                    var strings = (string[])arr;
                    var ptrs = new IntPtr[rowCount];
                    int totalBytes = 0;
                    var offsets = new int[rowCount];
                    for (int r = 0; r < rowCount; r++)
                    {
                        offsets[r] = totalBytes;
                        totalBytes += System.Text.Encoding.UTF8.GetByteCount(strings[r]) + 1;
                    }

                    var allBytes = new byte[totalBytes > 0 ? totalBytes : 1];
                    for (int r = 0; r < rowCount; r++)
                    {
                        int written = System.Text.Encoding.UTF8.GetBytes(
                            strings[r], 0, strings[r].Length, allBytes, offsets[r]);
                        allBytes[offsets[r] + written] = 0; // null terminator
                    }

                    var bufHandle = GCHandle.Alloc(allBytes, GCHandleType.Pinned);
                    stringBufHandles.Add(bufHandle);
                    IntPtr basePtr = bufHandle.AddrOfPinnedObject();

                    for (int r = 0; r < rowCount; r++)
                        ptrs[r] = basePtr + offsets[r];

                    gcHandles[c] = GCHandle.Alloc(ptrs, GCHandleType.Pinned);
                    bufPtrs[c] = gcHandles[c].AddrOfPinnedObject();
                }
                else
                {
                    gcHandles[c] = GCHandle.Alloc(arr, GCHandleType.Pinned);
                    bufPtrs[c] = gcHandles[c].AddrOfPinnedObject();
                }
            }

            if (!NativeMethods.bcsv_writer_write_columns(
                    writer.Handle, bufPtrs, (nuint)numCols, (nuint)rowCount))
            {
                NativeMethods.ThrowIfError("WriteColumns");
            }
        }
        finally
        {
            for (int c = 0; c < numCols; c++)
                if (gcHandles[c].IsAllocated) gcHandles[c].Free();
            foreach (var h in stringBufHandles)
                if (h.IsAllocated) h.Free();
        }
    }

    internal static Array AllocateTypedArray(ColumnType type, int length) => type switch
    {
        ColumnType.Bool   => new bool[length],
        ColumnType.UInt8  => new byte[length],
        ColumnType.UInt16 => new ushort[length],
        ColumnType.UInt32 => new uint[length],
        ColumnType.UInt64 => new ulong[length],
        ColumnType.Int8   => new sbyte[length],
        ColumnType.Int16  => new short[length],
        ColumnType.Int32  => new int[length],
        ColumnType.Int64  => new long[length],
        ColumnType.Float  => new float[length],
        ColumnType.Double => new double[length],
        _ => throw new BcsvException($"Cannot allocate array for type {type}")
    };

    /// <summary>Split a packed null-terminated string buffer into individual strings.</summary>
    internal static string[] SplitPackedStrings(byte[] buf, int count)
    {
        var result = new string[count];
        int offset = 0;
        for (int i = 0; i < count && offset < buf.Length; i++)
        {
            int end = Array.IndexOf(buf, (byte)0, offset);
            if (end < 0) end = buf.Length;
            result[i] = System.Text.Encoding.UTF8.GetString(buf, offset, end - offset);
            offset = end + 1;
        }
        return result;
    }
}

/// <summary>Holds column-oriented data read from a BCSV file.</summary>
public sealed class ColumnData : IDisposable
{
    private readonly BcsvLayout _layout;
    private readonly ColumnType[] _types;
    private readonly string[] _names;
    private readonly Array[] _numericArrays;
    private readonly string[][] _stringArrays;

    public int RowCount { get; }

    internal ColumnData(BcsvLayout layout, ColumnType[] types, string[] names,
        Array[] numericArrays, string[][] stringArrays, int rowCount)
    {
        _layout = layout;
        _types = types;
        _names = names;
        _numericArrays = numericArrays;
        _stringArrays = stringArrays;
        RowCount = rowCount;
    }

    public void Dispose() => _layout.Dispose();

    public BcsvLayout Layout => _layout;
    public int ColumnCount => _types.Length;

    public ColumnType GetColumnType(int index) => _types[index];
    public string GetColumnName(int index) => _names[index];

    /// <summary>Get a numeric column as a typed array.</summary>
    public T[] GetColumn<T>(int index) where T : unmanaged
    {
        if (_types[index] == ColumnType.String)
            throw new BcsvException("Use GetStringColumn for string columns");

        var arr = _numericArrays[index];
        if (arr is T[] typed)
        {
            // Return a correctly-sized slice if fewer rows were read
            if (typed.Length == RowCount) return typed;
            return typed[..RowCount];
        }
        throw new BcsvException(
            $"Type mismatch: column {index} is {_types[index]}, requested {typeof(T).Name}");
    }

    /// <summary>Get a string column.</summary>
    public string[] GetStringColumn(int index)
    {
        if (_types[index] != ColumnType.String)
            throw new BcsvException("Use GetColumn<T> for numeric columns");
        return _stringArrays[index];
    }

    /// <summary>Get a numeric column as a ReadOnlySpan.</summary>
    public ReadOnlySpan<T> AsSpan<T>(int index) where T : unmanaged
        => GetColumn<T>(index).AsSpan();
}
