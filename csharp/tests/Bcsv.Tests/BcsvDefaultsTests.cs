// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using Xunit;

namespace Bcsv.Tests;

/// <summary>
/// Every managed entry point that opens a writer must default to the same
/// compression level.
///
/// These assertions read the level back out of the written file's header
/// (<see cref="BcsvReader.CompressionLevel"/>) rather than checking source
/// literals, because the defect they guard against was exactly a source literal
/// that had drifted: <c>BcsvWriter.Open</c> moved to level 6 while
/// <c>BcsvColumns.WriteColumns</c> kept writing level 1, so identical data
/// compressed differently depending on which API you called. Only the file
/// tells the truth.
/// </summary>
public class BcsvDefaultsTests : IDisposable
{
    private readonly string _tmpDir;

    public BcsvDefaultsTests()
    {
        _tmpDir = Path.Combine(Path.GetTempPath(),
                               "bcsv_defaults_tests_" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(_tmpDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tmpDir))
            Directory.Delete(_tmpDir, recursive: true);
    }

    private string TmpFile(string name) => Path.Combine(_tmpDir, name);

    private static byte LevelOf(string path)
    {
        using var reader = new BcsvReader();
        reader.Open(path);
        return reader.CompressionLevel;
    }

    private static BcsvLayout MakeLayout()
    {
        var layout = new BcsvLayout();
        layout.AddColumn("x", ColumnType.Double)
              .AddColumn("label", ColumnType.String);
        return layout;
    }

    [Fact]
    public void Default_Is_Six()
    {
        Assert.Equal(6, BcsvDefaults.CompressionLevel);
    }

    [Fact]
    public void Writer_Open_Uses_The_Default()
    {
        var path = TmpFile("writer.bcsv");
        using (var layout = MakeLayout())
        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path, overwrite: true);
            for (int i = 0; i < 200; i++)
            {
                writer.Row.SetDouble(0, i * 0.1);
                writer.Row.SetString(1, $"label_{i}");
                writer.WriteRow();
            }
        }

        Assert.Equal(BcsvDefaults.CompressionLevel, LevelOf(path));
    }

    [Fact]
    public void WriteColumns_Uses_The_Same_Default_As_Writer_Open()
    {
        var path = TmpFile("columns.bcsv");
        using var layout = MakeLayout();

        var xData = new double[200];
        var labels = new string[200];
        for (int i = 0; i < 200; i++)
        {
            xData[i] = i * 0.1;
            labels[i] = $"label_{i}";
        }

        BcsvColumns.WriteColumns(path, layout,
            new Dictionary<int, Array> { [0] = xData, [1] = labels }, 200,
            overwrite: true);

        Assert.Equal(BcsvDefaults.CompressionLevel, LevelOf(path));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(9)]
    public void Explicit_Level_Still_Wins(int level)
    {
        var path = TmpFile($"explicit_{level}.bcsv");
        using (var layout = MakeLayout())
        using (var writer = new BcsvWriter(layout))
        {
            writer.Open(path, overwrite: true, compression: level);
            writer.Row.SetDouble(0, 1.0);
            writer.Row.SetString(1, "x");
            writer.WriteRow();
        }

        Assert.Equal((byte)level, LevelOf(path));
    }
}
