// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Runtime.InteropServices;
using Xunit;

namespace Bcsv.Tests;

/// <summary>xUnit test suite for the BCSV C# bindings.</summary>
public class BcsvTests : IDisposable
{
    private readonly string _tmpDir;

    public BcsvTests()
    {
        _tmpDir = Path.Combine(Path.GetTempPath(), "bcsv_csharp_tests_" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(_tmpDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tmpDir))
            Directory.Delete(_tmpDir, recursive: true);
    }

    private string TmpFile(string name) => Path.Combine(_tmpDir, name);

    // ════════════════════════════════════════════════════════════════════
    // Version API
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Version_ReturnsNonEmpty()
    {
        var v = BcsvVersion.Version;
        Assert.False(string.IsNullOrEmpty(v));
        Assert.Contains('.', v);
    }

    [Fact]
    public void Version_ComponentsAreNonNegative()
    {
        Assert.True(BcsvVersion.Major >= 0);
        Assert.True(BcsvVersion.Minor >= 0);
        Assert.True(BcsvVersion.Patch >= 0);
    }

    [Fact]
    public void FormatVersion_ReturnsNonEmpty()
    {
        Assert.False(string.IsNullOrEmpty(BcsvVersion.FormatVersion));
    }

    // ════════════════════════════════════════════════════════════════════
    // Layout
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Layout_CreateEmpty()
    {
        using var layout = new BcsvLayout();
        Assert.Equal(0, layout.ColumnCount);
    }

    [Fact]
    public void Layout_AddColumns()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("temp", ColumnType.Float)
              .AddColumn("id", ColumnType.Int32)
              .AddColumn("name", ColumnType.String);

        Assert.Equal(3, layout.ColumnCount);
        Assert.Equal("temp", layout.ColumnName(0));
        Assert.Equal(ColumnType.Float, layout.ColumnType(0));
        Assert.Equal("id", layout.ColumnName(1));
        Assert.Equal(ColumnType.Int32, layout.ColumnType(1));
        Assert.Equal("name", layout.ColumnName(2));
        Assert.Equal(ColumnType.String, layout.ColumnType(2));
    }

    [Fact]
    public void Layout_HasColumn()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("alpha", ColumnType.Double);
        Assert.True(layout.HasColumn("alpha"));
        Assert.False(layout.HasColumn("beta"));
    }

    [Fact]
    public void Layout_ColumnIndex()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double)
              .AddColumn("y", ColumnType.Double)
              .AddColumn("z", ColumnType.Double);

        Assert.Equal(0, layout.ColumnIndex("x"));
        Assert.Equal(1, layout.ColumnIndex("y"));
        Assert.Equal(2, layout.ColumnIndex("z"));
    }

    [Fact]
    public void Layout_RemoveColumn()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("a", ColumnType.Int32)
              .AddColumn("b", ColumnType.Int32)
              .AddColumn("c", ColumnType.Int32);

        layout.RemoveColumn(1);
        Assert.Equal(2, layout.ColumnCount);
        Assert.Equal("a", layout.ColumnName(0));
        Assert.Equal("c", layout.ColumnName(1));
    }

    [Fact]
    public void Layout_Clone()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double);

        using var clone = layout.Clone();
        Assert.Equal(1, clone.ColumnCount);
        Assert.Equal("x", clone.ColumnName(0));
        Assert.True(layout.IsCompatible(clone));
    }

    [Fact]
    public void Layout_IReadOnlyList()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("a", ColumnType.Bool)
              .AddColumn("b", ColumnType.String);

        IReadOnlyList<ColumnDefinition> cols = layout;
        Assert.Equal(2, cols.Count);
        Assert.Equal("a", cols[0].Name);
        Assert.Equal(ColumnType.Bool, cols[0].Type);
        Assert.Equal(0, cols[0].Index);
    }

    [Fact]
    public void Layout_Enumerable()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Float)
              .AddColumn("y", ColumnType.Float);

        var names = layout.Select(c => c.Name).ToList();
        Assert.Equal(new[] { "x", "y" }, names);
    }

    [Fact]
    public void Layout_ToString()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("val", ColumnType.Int32);
        var s = layout.ToString();
        Assert.False(string.IsNullOrEmpty(s));
    }

    // ════════════════════════════════════════════════════════════════════
    // Writer + Reader Round-Trip
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void WriteRead_AllTypes_RoundTrip()
    {
        var path = TmpFile("all_types.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("b", ColumnType.Bool)
              .AddColumn("u8", ColumnType.UInt8)
              .AddColumn("u16", ColumnType.UInt16)
              .AddColumn("u32", ColumnType.UInt32)
              .AddColumn("u64", ColumnType.UInt64)
              .AddColumn("i8", ColumnType.Int8)
              .AddColumn("i16", ColumnType.Int16)
              .AddColumn("i32", ColumnType.Int32)
              .AddColumn("i64", ColumnType.Int64)
              .AddColumn("f", ColumnType.Float)
              .AddColumn("d", ColumnType.Double)
              .AddColumn("s", ColumnType.String);

        // Write
        using (var writer = new BcsvWriter(layout, "delta"))
        {
            writer.Open(path);
            for (int i = 0; i < 100; i++)
            {
                var row = writer.Row;
                row.SetBool(0, i % 2 == 0);
                row.SetUInt8(1, (byte)(i & 0xFF));
                row.SetUInt16(2, (ushort)i);
                row.SetUInt32(3, (uint)i * 10);
                row.SetUInt64(4, (ulong)i * 100);
                row.SetInt8(5, (sbyte)(i - 50));
                row.SetInt16(6, (short)(i - 500));
                row.SetInt32(7, i * 1000);
                row.SetInt64(8, (long)i * 10000);
                row.SetFloat(9, i * 1.5f);
                row.SetDouble(10, i * 2.5);
                row.SetString(11, $"row_{i}");
                writer.WriteRow();
            }
        }

        // Read + verify
        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(100, reader.RowCount);
        Assert.Equal(12, reader.Layout.ColumnCount);

        int rowIdx = 0;
        foreach (var row in reader)
        {
            int i = rowIdx;
            Assert.Equal(i % 2 == 0, row.GetBool(0));
            Assert.Equal((byte)(i & 0xFF), row.GetUInt8(1));
            Assert.Equal((ushort)i, row.GetUInt16(2));
            Assert.Equal((uint)i * 10, row.GetUInt32(3));
            Assert.Equal((ulong)i * 100, row.GetUInt64(4));
            Assert.Equal((sbyte)(i - 50), row.GetInt8(5));
            Assert.Equal((short)(i - 500), row.GetInt16(6));
            Assert.Equal(i * 1000, row.GetInt32(7));
            Assert.Equal((long)i * 10000, row.GetInt64(8));
            Assert.Equal(i * 1.5f, row.GetFloat(9));
            Assert.Equal(i * 2.5, row.GetDouble(10));
            Assert.Equal($"row_{i}", row.GetString(11));
            rowIdx++;
        }
        Assert.Equal(100, rowIdx);
    }

    [Theory]
    [InlineData("flat")]
    [InlineData("zoh")]
    [InlineData("delta")]
    public void WriteRead_AllCodecs(string codec)
    {
        var path = TmpFile($"codec_{codec}.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double)
              .AddColumn("y", ColumnType.Int32);

        using (var writer = new BcsvWriter(layout, codec))
        {
            writer.Open(path, flags: codec == "flat" ? FileFlags.None : FileFlags.BatchCompress);
            for (int i = 0; i < 50; i++)
            {
                writer.Row.SetDouble(0, i * 0.1);
                writer.Row.SetInt32(1, i);
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(50, reader.RowCount);

        int count = 0;
        while (reader.ReadNext())
        {
            Assert.Equal(count * 0.1, reader.Row.GetDouble(0), 5);
            Assert.Equal(count, reader.Row.GetInt32(1));
            count++;
        }
        Assert.Equal(50, count);
    }

    [Fact]
    public void WriteRead_EmptyFile()
    {
        var path = TmpFile("empty.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            // Write zero rows
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(0, reader.RowCount);
        Assert.False(reader.ReadNext());
    }

    [Fact]
    public void WriteRead_SingleRow()
    {
        var path = TmpFile("single.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("v", ColumnType.Double);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            writer.Row.SetDouble(0, 42.0);
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(1, reader.RowCount);
        Assert.True(reader.ReadNext());
        Assert.Equal(42.0, reader.Row.GetDouble(0));
        Assert.False(reader.ReadNext());
    }

    [Fact]
    public void Writer_IsOpenCloseState()
    {
        var path = TmpFile("state.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);

        using var writer = new BcsvWriter(layout);
        Assert.False(writer.IsOpen);
        writer.Open(path);
        Assert.True(writer.IsOpen);
        writer.Close();
        Assert.False(writer.IsOpen);
    }

    [Fact]
    public void Writer_RowCount()
    {
        var path = TmpFile("rowcount.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);

        using var writer = new BcsvWriter(layout);
        writer.Open(path);
        Assert.Equal(0, writer.RowCount);
        writer.Row.SetInt32(0, 1);
        writer.WriteRow();
        Assert.Equal(1, writer.RowCount);
        writer.Row.SetInt32(0, 2);
        writer.WriteRow();
        Assert.Equal(2, writer.RowCount);
    }

    // ════════════════════════════════════════════════════════════════════
    // Row — generic Get/Set
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Row_GenericGetSet()
    {
        var path = TmpFile("generic.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("i", ColumnType.Int32)
              .AddColumn("d", ColumnType.Double)
              .AddColumn("s", ColumnType.String);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            writer.Row.Set(0, 42);
            writer.Row.Set(1, 3.14);
            writer.Row.Set(2, "hello");
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Assert.Equal(42, reader.Row.Get<int>(0));
        Assert.Equal(3.14, reader.Row.Get<double>(1));
        Assert.Equal("hello", reader.Row.Get<string>(2));
    }

    // ════════════════════════════════════════════════════════════════════
    // Row — Array (vectorized) access
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Row_ArrayAccess_Doubles()
    {
        var path = TmpFile("array_doubles.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double)
              .AddColumn("y", ColumnType.Double)
              .AddColumn("z", ColumnType.Double);

        var srcVec = new double[] { 1.0, 2.0, 3.0 };

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            writer.Row.SetDoubles(0, srcVec);
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());

        var dstVec = new double[3];
        reader.Row.GetDoubles(0, dstVec);
        Assert.Equal(srcVec, dstVec);
    }

    [Fact]
    public void Row_ArrayAccess_Floats()
    {
        var path = TmpFile("array_floats.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("a", ColumnType.Float)
              .AddColumn("b", ColumnType.Float);

        var srcVec = new float[] { 1.5f, 2.5f };

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            writer.Row.SetFloats(0, srcVec);
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());

        var dstVec = new float[2];
        reader.Row.GetFloats(0, dstVec);
        Assert.Equal(srcVec, dstVec);
    }

    // ════════════════════════════════════════════════════════════════════
    // Random Access (ReaderDirectAccess)
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Reader_RandomAccess()
    {
        var path = TmpFile("random.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("idx", ColumnType.Int32);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            for (int i = 0; i < 100; i++)
            {
                writer.Row.SetInt32(0, i);
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(100, reader.RowCount);

        // Read in reverse order
        for (int i = 99; i >= 0; i--)
        {
            Assert.True(reader.Read(i));
            Assert.Equal(i, reader.Row.GetInt32(0));
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // Sampler (filter + projection)
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Sampler_FilterAndProject()
    {
        var path = TmpFile("sampler.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("id", ColumnType.Int32)
              .AddColumn("value", ColumnType.Double)
              .AddColumn("label", ColumnType.String);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            for (int i = 0; i < 100; i++)
            {
                writer.Row.SetInt32(0, i);
                writer.Row.SetDouble(1, i * 0.5);
                writer.Row.SetString(2, $"item_{i}");
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path);

        using var sampler = new BcsvSampler(reader);
        sampler.SetConditional("X[0][0] >= 50");     // id >= 50
        sampler.SetSelection("X[0][0], X[0][1]");    // id, value

        int count = 0;
        foreach (var row in sampler)
        {
            int id = row.GetInt32(0);
            Assert.True(id >= 50);
            count++;
        }
        Assert.Equal(50, count);
    }

    // ════════════════════════════════════════════════════════════════════
    // CSV Reader + Writer
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void CsvRoundTrip()
    {
        var csvPath = TmpFile("test.csv");
        var bcsvPath = TmpFile("test.bcsv");
        var csvOutPath = TmpFile("test_out.csv");

        using var layout = new BcsvLayout();
        layout.AddColumn("id", ColumnType.Int32)
              .AddColumn("value", ColumnType.Double)
              .AddColumn("name", ColumnType.String);

        // Write CSV
        using (var csvWriter = new BcsvCsvWriter(layout))
        {
            csvWriter.Open(csvPath);
            for (int i = 0; i < 10; i++)
            {
                csvWriter.Row.SetInt32(0, i);
                csvWriter.Row.SetDouble(1, i * 1.1);
                csvWriter.Row.SetString(2, $"item{i}");
                csvWriter.WriteRow();
            }
        }

        // Read CSV → write BCSV
        using (var csvReader = new BcsvCsvReader(layout))
        {
            csvReader.Open(csvPath);
            using var writer = new BcsvWriter(layout);
            writer.Open(bcsvPath);
            foreach (var row in csvReader)
            {
                writer.Write(row);
            }
        }

        // Verify BCSV
        using var reader = new BcsvReader();
        reader.Open(bcsvPath);
        Assert.Equal(10, reader.RowCount);

        int idx = 0;
        while (reader.ReadNext())
        {
            Assert.Equal(idx, reader.Row.GetInt32(0));
            Assert.Equal(idx * 1.1, reader.Row.GetDouble(1), 5);
            Assert.Equal($"item{idx}", reader.Row.GetString(2));
            idx++;
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // Columnar Bulk I/O
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void Columnar_WriteRead_RoundTrip()
    {
        var path = TmpFile("columnar.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("id", ColumnType.Int32)
              .AddColumn("value", ColumnType.Double)
              .AddColumn("label", ColumnType.String);

        int numRows = 1000;
        var ids = new int[numRows];
        var values = new double[numRows];
        var labels = new string[numRows];
        for (int i = 0; i < numRows; i++)
        {
            ids[i] = i;
            values[i] = i * 0.01;
            labels[i] = $"row_{i}";
        }

        // Write columnar
        var columns = new Dictionary<int, Array>
        {
            { 0, ids },
            { 1, values },
            { 2, labels }
        };
        BcsvColumns.WriteColumns(path, layout, columns, numRows);

        // Read columnar
        using var data = BcsvColumns.ReadColumns(path);
        Assert.Equal(numRows, data.RowCount);
        Assert.Equal(3, data.ColumnCount);

        var readIds = data.GetColumn<int>(0);
        var readValues = data.GetColumn<double>(1);
        var readLabels = data.GetStringColumn(2);

        Assert.Equal(numRows, readIds.Length);
        for (int i = 0; i < numRows; i++)
        {
            Assert.Equal(ids[i], readIds[i]);
            Assert.Equal(values[i], readValues[i], 10);
            Assert.Equal(labels[i], readLabels[i]);
        }
    }

    [Fact]
    public void Columnar_NumericOnly()
    {
        var path = TmpFile("columnar_numeric.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Float)
              .AddColumn("y", ColumnType.Float)
              .AddColumn("z", ColumnType.Float);

        int numRows = 500;
        var x = new float[numRows];
        var y = new float[numRows];
        var z = new float[numRows];
        for (int i = 0; i < numRows; i++)
        {
            x[i] = i * 0.1f; y[i] = i * 0.2f; z[i] = i * 0.3f;
        }

        BcsvColumns.WriteColumns(path, layout,
            new Dictionary<int, Array> { { 0, x }, { 1, y }, { 2, z } }, numRows);

        using var data = BcsvColumns.ReadColumns(path);
        Assert.Equal(numRows, data.RowCount);
        var rx = data.GetColumn<float>(0);
        var ry = data.GetColumn<float>(1);
        var rz = data.GetColumn<float>(2);
        for (int i = 0; i < numRows; i++)
        {
            Assert.Equal(x[i], rx[i]);
            Assert.Equal(y[i], ry[i]);
            Assert.Equal(z[i], rz[i]);
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // Edge Cases
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void EmptyString_RoundTrip()
    {
        var path = TmpFile("empty_strings.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("s", ColumnType.String);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            writer.Row.SetString(0, "");
            writer.WriteRow();
            writer.Row.SetString(0, "notempty");
            writer.WriteRow();
            writer.Row.SetString(0, "");
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Assert.Equal("", reader.Row.GetString(0));
        Assert.True(reader.ReadNext());
        Assert.Equal("notempty", reader.Row.GetString(0));
        Assert.True(reader.ReadNext());
        Assert.Equal("", reader.Row.GetString(0));
    }

    [Fact]
    public void UnicodeString_RoundTrip()
    {
        var path = TmpFile("unicode.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("s", ColumnType.String);

        var testStrings = new[] { "Hello", "Ωmega", "日本語", "🎉🚀", "café" };

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            foreach (var s in testStrings)
            {
                writer.Row.SetString(0, s);
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        int i = 0;
        while (reader.ReadNext())
        {
            Assert.Equal(testStrings[i], reader.Row.GetString(0));
            i++;
        }
        Assert.Equal(testStrings.Length, i);
    }

    [Fact]
    public void LargeFile_MultiRow()
    {
        var path = TmpFile("large.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double);

        int numRows = 10_000;
        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            for (int i = 0; i < numRows; i++)
            {
                writer.Row.SetDouble(0, i * 0.001);
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(numRows, reader.RowCount);

        int count = 0;
        foreach (var row in reader)
        {
            Assert.Equal(count * 0.001, row.GetDouble(0), 8);
            count++;
        }
        Assert.Equal(numRows, count);
    }

    [Fact]
    public void Reader_FileNotFound_Throws()
    {
        using var reader = new BcsvReader();
        Assert.Throws<BcsvException>(() => reader.Open("/nonexistent/path.bcsv"));
    }

    [Fact]
    public void Reader_FileFlags()
    {
        var path = TmpFile("flags.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);

        using (var writer = new BcsvWriter(layout, "delta"))
        {
            writer.Open(path, flags: FileFlags.BatchCompress);
            writer.Row.SetInt32(0, 1);
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        var flags = reader.FileFlags;
        Assert.True(flags.HasFlag(FileFlags.BatchCompress));
        Assert.True(flags.HasFlag(FileFlags.DeltaEncoding));
    }

    // ════════════════════════════════════════════════════════════════════
    // Cross-language interop — write with C# CLI, read with C# API
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void WriteRead_MixedTypes_1000Rows()
    {
        var path = TmpFile("mixed_1k.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("timestamp", ColumnType.Int64)
              .AddColumn("temperature", ColumnType.Float)
              .AddColumn("humidity", ColumnType.Double)
              .AddColumn("sensor_id", ColumnType.UInt16)
              .AddColumn("active", ColumnType.Bool)
              .AddColumn("label", ColumnType.String);

        int numRows = 1000;
        using (var writer = new BcsvWriter(layout, "delta"))
        {
            writer.Open(path);
            for (int i = 0; i < numRows; i++)
            {
                writer.Row.SetInt64(0, 1000000 + i);
                writer.Row.SetFloat(1, 20.0f + i * 0.1f);
                writer.Row.SetDouble(2, 50.0 + i * 0.05);
                writer.Row.SetUInt16(3, (ushort)(i % 100));
                writer.Row.SetBool(4, i % 3 == 0);
                writer.Row.SetString(5, $"sensor_{i % 10}");
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.Equal(numRows, reader.RowCount);

        int count = 0;
        while (reader.ReadNext())
        {
            int i = count;
            Assert.Equal(1000000 + i, reader.Row.GetInt64(0));
            Assert.Equal(20.0f + i * 0.1f, reader.Row.GetFloat(1), 3);
            Assert.Equal(50.0 + i * 0.05, reader.Row.GetDouble(2), 6);
            Assert.Equal((ushort)(i % 100), reader.Row.GetUInt16(3));
            Assert.Equal(i % 3 == 0, reader.Row.GetBool(4));
            Assert.Equal($"sensor_{i % 10}", reader.Row.GetString(5));
            count++;
        }
        Assert.Equal(numRows, count);
    }

    [Fact]
    public void Row_ToString()
    {
        var path = TmpFile("tostring.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);

        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            writer.Row.SetInt32(0, 42);
            writer.WriteRow();
        }

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        var s = reader.Row.ToString();
        Assert.False(string.IsNullOrEmpty(s));
    }

    [Fact]
    public void Row_Clear()
    {
        var path = TmpFile("clear.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32)
              .AddColumn("s", ColumnType.String);

        using var writer = new BcsvWriter(layout);
        writer.Open(path);

        writer.Row.SetInt32(0, 42);
        writer.Row.SetString(1, "hello");
        writer.Row.Clear();

        // After clear, values should be defaults
        Assert.Equal(0, writer.Row.GetInt32(0));
        Assert.Equal("", writer.Row.GetString(1));
    }

    // ════════════════════════════════════════════════════════════════════
    // ReadBatch — Bulk Row Read
    // ════════════════════════════════════════════════════════════════════
    [Fact]
    public void ReadBatch_MixedTypes()
    {
        var path = TmpFile("readbatch.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("id", ColumnType.Int32)
              .AddColumn("value", ColumnType.Double)
              .AddColumn("name", ColumnType.String);

        int total = 100;
        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path);
            for (int i = 0; i < total; i++)
            {
                writer.Row.SetInt32(0, i);
                writer.Row.SetDouble(1, i * 1.5);
                writer.Row.SetString(2, $"item_{i:D3}");
                writer.WriteRow();
            }
        }

        using var reader = new BcsvReader();
        reader.Open(path, rebuildFooter: true);

        // First batch
        using var b1 = reader.ReadBatch(30);
        Assert.NotNull(b1);
        Assert.Equal(30, b1!.RowCount);
        var ids = b1.GetColumn<int>(0);
        Assert.Equal(0, ids[0]);
        Assert.Equal(29, ids[29]);
        var names = b1.GetStringColumn(2);
        Assert.Equal("item_000", names[0]);
        Assert.Equal("item_029", names[29]);

        // Second batch
        using var b2 = reader.ReadBatch(50);
        Assert.NotNull(b2);
        Assert.Equal(50, b2!.RowCount);
        Assert.Equal(30, b2.GetColumn<int>(0)[0]);

        // Third batch (partial — 20 remain)
        using var b3 = reader.ReadBatch(100);
        Assert.NotNull(b3);
        Assert.Equal(20, b3!.RowCount);
        Assert.Equal(80, b3.GetColumn<int>(0)[0]);

        // EOF
        var b4 = reader.ReadBatch(10);
        Assert.Null(b4);
    }

    [Fact]
    public void Columnar_StringWrite_NoLeak()
    {
        // Verifies that the GCHandle leak fix works correctly
        // by writing strings through the columnar API multiple times
        var path = TmpFile("string_leak.bcsv");
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double)
              .AddColumn("label", ColumnType.String);

        var xData = new double[200];
        var labels = new string[200];
        for (int i = 0; i < 200; i++)
        {
            xData[i] = i * 0.1;
            labels[i] = $"label_{i}_with_some_longer_text";
        }

        // Write and read back multiple times to stress test
        for (int round = 0; round < 3; round++)
        {
            BcsvColumns.WriteColumns(path, layout,
                new Dictionary<int, Array> { [0] = xData, [1] = labels }, 200,
                compression: 1);

            using var data = BcsvColumns.ReadColumns(path);
            Assert.Equal(200, data.RowCount);
            var readLabels = data.GetStringColumn(1);
            for (int i = 0; i < 200; i++)
                Assert.Equal(labels[i], readLabels[i]);
        }
    }

    // ── Cycle 3: Finalizer & Dispose safety ─────────────────────────────

    [Fact]
    public void Layout_Finalizer_Prevents_Leak()
    {
        WeakReference CreateAndAbandon()
        {
            var layout = new BcsvLayout();
            layout.AddColumn("test", ColumnType.Int32);
            return new WeakReference(layout);
        }
        var weakRef = CreateAndAbandon();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Assert.False(weakRef.IsAlive);
    }

    [Fact]
    public void Reader_DoubleDispose_NoThrow()
    {
        var reader = new BcsvReader();
        reader.Dispose();
        reader.Dispose(); // must not throw
    }

    [Fact]
    public void Writer_DoubleDispose_NoThrow()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);
        var writer = new BcsvWriter(layout);
        writer.Dispose();
        writer.Dispose();
    }

    [Fact]
    public void Layout_DoubleDispose_NoThrow()
    {
        var layout = new BcsvLayout();
        layout.Dispose();
        layout.Dispose();
    }

    [Fact]
    public void Sampler_DoubleDispose_NoThrow()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);
        var path = TmpFile("sampler_dd.bcsv");
        using var writer = new BcsvWriter(layout);
        writer.Open(path, true);
        writer.Row.SetInt32(0, 1);
        writer.WriteRow();
        writer.Close();
        using var reader = new BcsvReader();
        reader.Open(path);
        var sampler = new BcsvSampler(reader);
        sampler.Dispose();
        sampler.Dispose();
    }

    [Fact]
    public void CsvReader_DoubleDispose_NoThrow()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);
        var csvReader = new BcsvCsvReader(layout);
        csvReader.Dispose();
        csvReader.Dispose();
    }

    [Fact]
    public void CsvWriter_DoubleDispose_NoThrow()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);
        var csvWriter = new BcsvCsvWriter(layout);
        csvWriter.Dispose();
        csvWriter.Dispose();
    }

    [Fact]
    public void Layout_NonOwning_DoubleDispose_NoThrow()
    {
        using var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Int32);
        var path = TmpFile("nonowning.bcsv");
        using var writer = new BcsvWriter(layout);
        writer.Open(path, true);
        writer.Row.SetInt32(0, 42);
        writer.WriteRow();
        writer.Close();
        using var reader = new BcsvReader();
        reader.Open(path);
        // Reader.Layout returns a non-owning layout
        var borrowed = reader.Layout;
        Assert.True(borrowed.ColumnCount > 0);
        borrowed.Dispose(); // non-owning — should be a no-op
        borrowed.Dispose(); // double dispose of non-owning — also safe
    }

    // ── Cycle 3: Array round-trip tests for new P/Invoke types ──────────

    [Fact]
    public void Row_ArrayAccess_Bool()
    {
        var path = TmpFile("bool_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 4; i++)
            layout.AddColumn($"b{i}", ColumnType.Bool);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<bool> src = stackalloc bool[] { true, false, true, false };
        writer.Row.SetBools(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<bool> dst = stackalloc bool[4];
        reader.Row.GetBools(0, dst);
        for (int i = 0; i < 4; i++)
            Assert.Equal(src[i], dst[i]);
    }

    [Fact]
    public void Row_ArrayAccess_UInt8()
    {
        var path = TmpFile("uint8_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 4; i++)
            layout.AddColumn($"u8_{i}", ColumnType.UInt8);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<byte> src = stackalloc byte[] { 0, 127, 255, 42 };
        writer.Row.SetUInt8s(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<byte> dst = stackalloc byte[4];
        reader.Row.GetUInt8s(0, dst);
        Assert.Equal(src.ToArray(), dst.ToArray());
    }

    [Fact]
    public void Row_ArrayAccess_UInt16()
    {
        var path = TmpFile("uint16_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 4; i++)
            layout.AddColumn($"u16_{i}", ColumnType.UInt16);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<ushort> src = stackalloc ushort[] { 0, 1000, 65535, 42 };
        writer.Row.SetUInt16s(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<ushort> dst = stackalloc ushort[4];
        reader.Row.GetUInt16s(0, dst);
        Assert.Equal(src.ToArray(), dst.ToArray());
    }

    [Fact]
    public void Row_ArrayAccess_UInt32()
    {
        var path = TmpFile("uint32_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 3; i++)
            layout.AddColumn($"u32_{i}", ColumnType.UInt32);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<uint> src = stackalloc uint[] { 0, 2_000_000_000, 4_294_967_295 };
        writer.Row.SetUInt32s(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<uint> dst = stackalloc uint[3];
        reader.Row.GetUInt32s(0, dst);
        Assert.Equal(src.ToArray(), dst.ToArray());
    }

    [Fact]
    public void Row_ArrayAccess_UInt64()
    {
        var path = TmpFile("uint64_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 3; i++)
            layout.AddColumn($"u64_{i}", ColumnType.UInt64);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<ulong> src = stackalloc ulong[] { 0, 9_999_999_999, ulong.MaxValue };
        writer.Row.SetUInt64s(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<ulong> dst = stackalloc ulong[3];
        reader.Row.GetUInt64s(0, dst);
        Assert.Equal(src.ToArray(), dst.ToArray());
    }

    [Fact]
    public void Row_ArrayAccess_Int8()
    {
        var path = TmpFile("int8_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 4; i++)
            layout.AddColumn($"i8_{i}", ColumnType.Int8);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<sbyte> src = stackalloc sbyte[] { -128, -1, 0, 127 };
        writer.Row.SetInt8s(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<sbyte> dst = stackalloc sbyte[4];
        reader.Row.GetInt8s(0, dst);
        Assert.Equal(src.ToArray(), dst.ToArray());
    }

    [Fact]
    public void Row_ArrayAccess_Int16()
    {
        var path = TmpFile("int16_arr.bcsv");
        using var layout = new BcsvLayout();
        for (int i = 0; i < 4; i++)
            layout.AddColumn($"i16_{i}", ColumnType.Int16);

        using var writer = new BcsvWriter(layout, "flat");
        writer.Open(path, true);
        Span<short> src = stackalloc short[] { short.MinValue, -1, 0, short.MaxValue };
        writer.Row.SetInt16s(0, src);
        writer.WriteRow();
        writer.Close();

        using var reader = new BcsvReader();
        reader.Open(path);
        Assert.True(reader.ReadNext());
        Span<short> dst = stackalloc short[4];
        reader.Row.GetInt16s(0, dst);
        Assert.Equal(src.ToArray(), dst.ToArray());
    }
}
