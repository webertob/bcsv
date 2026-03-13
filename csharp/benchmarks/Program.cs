// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
// C# BCSV Benchmark — covers sequential row, direct access, columnar I/O,
// multiple codecs, all 3 reference workloads. Output matches Python benchmark JSON.

using System.Diagnostics;
using System.Text.Json;
using Bcsv;

static class BcsvBenchmark
{
    static readonly string[] WeatherStations = ["SEA", "HAM", "MUC", "SFO", "NYC"];
    static readonly string[] IotFirmware = ["1.2.0", "1.2.1", "1.3.0", "1.3.1"];
    static readonly string[] IotRegions = ["eu-west", "us-east", "ap-south"];
    static readonly string[] FinSymbols = ["AAPL", "MSFT", "NVDA", "AMZN", "GOOG"];
    static readonly string[] FinVenues = ["XNAS", "XNYS", "BATS"];
    static readonly string[] FinSides = ["BUY", "SELL"];
    static readonly string[] FinTypes = ["LMT", "MKT", "IOC"];
    static readonly string[] FinTraders = ["T1", "T2", "T3", "T4"];

    static readonly Dictionary<string, int> SizeRows = new()
    {
        ["S"] = 10_000,
        ["M"] = 100_000,
        ["L"] = 500_000,
    };

    record ColumnDef(string Name, ColumnType Type);

    static readonly Dictionary<string, ColumnDef[]> WorkloadSpecs = new()
    {
        ["weather_timeseries"] =
        [
            new("timestamp", ColumnType.Int64),
            new("station", ColumnType.String),
            new("temperature", ColumnType.Float),
            new("humidity", ColumnType.Float),
            new("pressure", ColumnType.Double),
            new("wind_speed", ColumnType.Float),
            new("raining", ColumnType.Bool),
            new("quality", ColumnType.UInt8),
        ],
        ["iot_fleet"] =
        [
            new("timestamp", ColumnType.Int64),
            new("device_id", ColumnType.UInt32),
            new("firmware", ColumnType.String),
            new("region", ColumnType.String),
            new("battery", ColumnType.Float),
            new("temperature", ColumnType.Float),
            new("vibration", ColumnType.Double),
            new("online", ColumnType.Bool),
        ],
        ["financial_orders"] =
        [
            new("timestamp", ColumnType.Int64),
            new("symbol", ColumnType.String),
            new("venue", ColumnType.String),
            new("side", ColumnType.String),
            new("qty", ColumnType.Int32),
            new("price", ColumnType.Double),
            new("order_type", ColumnType.String),
            new("trader", ColumnType.String),
            new("is_cancel", ColumnType.Bool),
        ],
    };

    // ── Data generation (matches Python/C++ deterministic patterns) ────
    static void GenerateData(string workload, int i, Random rng,
        BcsvRow? row = null, Dictionary<int, Array>? cols = null)
    {
        long ts = 1_700_000_000L + i;
        switch (workload)
        {
            case "weather_timeseries":
            {
                var station = WeatherStations[i % WeatherStations.Length];
                var temp = (float)(12.0 + 8.0 * (i % 97) / 97.0 + (rng.NextDouble() - 0.5));
                var hum = (float)(45.0 + 40.0 * (i % 53) / 53.0 + 2.0 * (rng.NextDouble() - 0.5));
                var pres = 1000.0 + 15.0 * (i % 41) / 41.0 + 0.4 * (rng.NextDouble() - 0.5);
                var wind = (float)(3.0 + 10.0 * (i % 31) / 31.0);
                var rain = i % 17 == 0;
                var qual = (byte)(i % 4);
                if (row != null)
                {
                    var r = row.Value;
                    r.SetInt64(0, ts); r.SetString(1, station); r.SetFloat(2, temp);
                    r.SetFloat(3, hum); r.SetDouble(4, pres); r.SetFloat(5, wind);
                    r.SetBool(6, rain); r.SetUInt8(7, qual);
                }
                if (cols != null)
                {
                    ((long[])cols[0])[i] = ts; ((string[])cols[1])[i] = station;
                    ((float[])cols[2])[i] = temp; ((float[])cols[3])[i] = hum;
                    ((double[])cols[4])[i] = pres; ((float[])cols[5])[i] = wind;
                    ((bool[])cols[6])[i] = rain; ((byte[])cols[7])[i] = qual;
                }
                break;
            }
            case "iot_fleet":
            {
                var devId = (uint)(1000 + i % 5000);
                var fw = IotFirmware[i % IotFirmware.Length];
                var reg = IotRegions[i % IotRegions.Length];
                var batt = (float)(100.0 - (i % 100) * 0.3);
                var temp = (float)(20.0 + (i % 120) * 0.1);
                var vib = (i % 40) * 0.02 + rng.NextDouble() * 0.01;
                var online = i % 29 != 0;
                if (row != null)
                {
                    var r = row.Value;
                    r.SetInt64(0, ts); r.SetUInt32(1, devId);
                    r.SetString(2, fw); r.SetString(3, reg);
                    r.SetFloat(4, batt); r.SetFloat(5, temp);
                    r.SetDouble(6, vib); r.SetBool(7, online);
                }
                if (cols != null)
                {
                    ((long[])cols[0])[i] = ts; ((uint[])cols[1])[i] = devId;
                    ((string[])cols[2])[i] = fw; ((string[])cols[3])[i] = reg;
                    ((float[])cols[4])[i] = batt; ((float[])cols[5])[i] = temp;
                    ((double[])cols[6])[i] = vib; ((bool[])cols[7])[i] = online;
                }
                break;
            }
            case "financial_orders":
            {
                var sym = FinSymbols[i % FinSymbols.Length];
                var ven = FinVenues[i % FinVenues.Length];
                var side = FinSides[i % FinSides.Length];
                var qty = 1 + i % 5000;
                var price = 90.0 + (i % 200) * 0.05 + 0.04 * (rng.NextDouble() - 0.5);
                var otype = FinTypes[i % FinTypes.Length];
                var trader = FinTraders[i % FinTraders.Length];
                var cancel = i % 23 == 0;
                if (row != null)
                {
                    var r = row.Value;
                    r.SetInt64(0, ts); r.SetString(1, sym);
                    r.SetString(2, ven); r.SetString(3, side);
                    r.SetInt32(4, qty); r.SetDouble(5, price);
                    r.SetString(6, otype); r.SetString(7, trader);
                    r.SetBool(8, cancel);
                }
                if (cols != null)
                {
                    ((long[])cols[0])[i] = ts; ((string[])cols[1])[i] = sym;
                    ((string[])cols[2])[i] = ven; ((string[])cols[3])[i] = side;
                    ((int[])cols[4])[i] = qty; ((double[])cols[5])[i] = price;
                    ((string[])cols[6])[i] = otype; ((string[])cols[7])[i] = trader;
                    ((bool[])cols[8])[i] = cancel;
                }
                break;
            }
        }
    }

    static BcsvLayout BuildLayout(ColumnDef[] cols)
    {
        var layout = new BcsvLayout();
        foreach (var c in cols)
            layout.AddColumn(c.Name, c.Type);
        return layout;
    }

    // ── Benchmark: Sequential Row Write ────────────────────────────────
    static (double writeMs, long fileSize) BenchSequentialWrite(
        string workload, ColumnDef[] cols, int numRows, string filePath,
        string rowCodec, FileFlags flags, int seed)
    {
        using var layout = BuildLayout(cols);
        using var writer = new BcsvWriter(layout, rowCodec);
        writer.Open(filePath, overwrite: true, compression: 1, blockSizeKb: 64, flags: flags);

        var rng = new Random(seed);
        var row = writer.Row;

        var sw = Stopwatch.StartNew();
        for (int i = 0; i < numRows; i++)
        {
            GenerateData(workload, i, rng, row: row);
            writer.WriteRow();
        }
        writer.Close();
        sw.Stop();

        long size = new FileInfo(filePath).Length;
        return (sw.Elapsed.TotalMilliseconds, size);
    }

    // ── Benchmark: Sequential Row Read (iterate only) ──────────────────
    static (double readMs, int rowCount) BenchSequentialRead(string filePath)
    {
        using var reader = new BcsvReader();
        reader.Open(filePath);

        int count = 0;
        var sw = Stopwatch.StartNew();
        while (reader.ReadNext())
            count++;
        sw.Stop();
        reader.Close();

        return (sw.Elapsed.TotalMilliseconds, count);
    }

    // ── Benchmark: Sequential Row Read with field access ───────────────
    static (double readMs, int rowCount) BenchSequentialReadFields(
        string filePath, ColumnDef[] cols)
    {
        using var reader = new BcsvReader();
        reader.Open(filePath);

        int count = 0;
        var sw = Stopwatch.StartNew();
        while (reader.ReadNext())
        {
            var row = reader.Row;
            for (int c = 0; c < cols.Length; c++)
            {
                switch (cols[c].Type)
                {
                    case ColumnType.Bool:   _ = row.GetBool(c); break;
                    case ColumnType.UInt8:  _ = row.GetUInt8(c); break;
                    case ColumnType.UInt16: _ = row.GetUInt16(c); break;
                    case ColumnType.UInt32: _ = row.GetUInt32(c); break;
                    case ColumnType.UInt64: _ = row.GetUInt64(c); break;
                    case ColumnType.Int8:   _ = row.GetInt8(c); break;
                    case ColumnType.Int16:  _ = row.GetInt16(c); break;
                    case ColumnType.Int32:  _ = row.GetInt32(c); break;
                    case ColumnType.Int64:  _ = row.GetInt64(c); break;
                    case ColumnType.Float:  _ = row.GetFloat(c); break;
                    case ColumnType.Double: _ = row.GetDouble(c); break;
                    case ColumnType.String: _ = row.GetString(c); break;
                }
            }
            count++;
        }
        sw.Stop();
        reader.Close();

        return (sw.Elapsed.TotalMilliseconds, count);
    }

    // ── Benchmark: Direct (random) Access ──────────────────────────────
    static (double readMs, int rowCount) BenchDirectAccess(
        string filePath, ColumnDef[] cols, int numRows)
    {
        using var reader = new BcsvReader();
        reader.Open(filePath, rebuildFooter: true);

        int count = 0;
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < numRows; i++)
        {
            if (!reader.Read(i)) break;
            var row = reader.Row;
            for (int c = 0; c < cols.Length; c++)
            {
                switch (cols[c].Type)
                {
                    case ColumnType.Bool:   _ = row.GetBool(c); break;
                    case ColumnType.UInt8:  _ = row.GetUInt8(c); break;
                    case ColumnType.UInt16: _ = row.GetUInt16(c); break;
                    case ColumnType.UInt32: _ = row.GetUInt32(c); break;
                    case ColumnType.UInt64: _ = row.GetUInt64(c); break;
                    case ColumnType.Int8:   _ = row.GetInt8(c); break;
                    case ColumnType.Int16:  _ = row.GetInt16(c); break;
                    case ColumnType.Int32:  _ = row.GetInt32(c); break;
                    case ColumnType.Int64:  _ = row.GetInt64(c); break;
                    case ColumnType.Float:  _ = row.GetFloat(c); break;
                    case ColumnType.Double: _ = row.GetDouble(c); break;
                    case ColumnType.String: _ = row.GetString(c); break;
                }
            }
            count++;
        }
        sw.Stop();
        reader.Close();

        return (sw.Elapsed.TotalMilliseconds, count);
    }

    // ── Benchmark: Columnar Write ──────────────────────────────────────
    static (double writeMs, long fileSize) BenchColumnarWrite(
        string workload, ColumnDef[] cols, int numRows, string filePath,
        string rowCodec, FileFlags flags, int seed)
    {
        var rng = new Random(seed);
        var columnData = new Dictionary<int, Array>();

        for (int c = 0; c < cols.Length; c++)
        {
            columnData[c] = cols[c].Type switch
            {
                ColumnType.Bool   => new bool[numRows],
                ColumnType.UInt8  => new byte[numRows],
                ColumnType.UInt16 => new ushort[numRows],
                ColumnType.UInt32 => new uint[numRows],
                ColumnType.UInt64 => new ulong[numRows],
                ColumnType.Int8   => new sbyte[numRows],
                ColumnType.Int16  => new short[numRows],
                ColumnType.Int32  => new int[numRows],
                ColumnType.Int64  => new long[numRows],
                ColumnType.Float  => new float[numRows],
                ColumnType.Double => new double[numRows],
                ColumnType.String => new string[numRows],
                _ => throw new NotSupportedException()
            };
        }

        for (int i = 0; i < numRows; i++)
            GenerateData(workload, i, rng, cols: columnData);

        using var layout = BuildLayout(cols);
        var sw = Stopwatch.StartNew();
        BcsvColumns.WriteColumns(filePath, layout, columnData, numRows, rowCodec,
            compression: 1, flags: flags);
        sw.Stop();

        long size = new FileInfo(filePath).Length;
        return (sw.Elapsed.TotalMilliseconds, size);
    }

    // ── Benchmark: Columnar Read ───────────────────────────────────────
    static (double readMs, int rowCount) BenchColumnarRead(string filePath)
    {
        var sw = Stopwatch.StartNew();
        using var data = BcsvColumns.ReadColumns(filePath);
        sw.Stop();
        return (sw.Elapsed.TotalMilliseconds, data.RowCount);
    }

    // ── Codec configurations ───────────────────────────────────────────
    static (string rowCodec, FileFlags flags, string label)[] GetCodecConfigs()
    {
        return
        [
            ("flat",  FileFlags.None, "flat"),
            ("delta", FileFlags.None, "delta"),
            ("delta", FileFlags.BatchCompress, "delta+batch"),
        ];
    }

    // ── Mode labels ────────────────────────────────────────────────────
    static string ModeLabel(string mode) => mode switch
    {
        "row_write"       => "CSharp Row Write",
        "row_read"        => "CSharp Row Read (iterate)",
        "row_read_fields" => "CSharp Row Read (fields)",
        "direct_access"   => "CSharp Direct Access",
        "columnar_write"  => "CSharp Columnar Write",
        "columnar_read"   => "CSharp Columnar Read",
        _ => mode,
    };

    static int Main(string[] args)
    {
        string size = "M";
        string workloadsArg = "weather_timeseries,iot_fleet,financial_orders";
        string modesArg = "row_write,row_read,row_read_fields,direct_access,columnar_write,columnar_read";
        string codecsArg = "flat,delta,delta+batch";
        string outputArg = "";
        int seed = 42;
        bool verify = false;
        string verifyDir = "";

        foreach (string arg in args)
        {
            if (arg.StartsWith("--size=")) size = arg[7..].ToUpperInvariant();
            else if (arg.StartsWith("--workloads=")) workloadsArg = arg[12..];
            else if (arg.StartsWith("--modes=")) modesArg = arg[8..];
            else if (arg.StartsWith("--codecs=")) codecsArg = arg[9..];
            else if (arg.StartsWith("--output=")) outputArg = arg[9..];
            else if (arg.StartsWith("--seed=")) seed = int.Parse(arg[7..]);
            else if (arg == "--verify") verify = true;
            else if (arg.StartsWith("--verify-dir=")) { verify = true; verifyDir = arg[13..]; }
        }

        if (!SizeRows.TryGetValue(size, out int numRows))
        {
            Console.Error.WriteLine("Invalid --size. Use S, M, or L.");
            return 2;
        }

        var selectedWorkloads = workloadsArg.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var selectedModes = modesArg.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var codecConfigs = GetCodecConfigs()
            .Where(c => codecsArg.Split(',').Select(s => s.Trim()).Contains(c.label))
            .ToArray();

        if (codecConfigs.Length == 0)
        {
            Console.Error.WriteLine($"No valid codecs in: {codecsArg}. Use flat, delta, delta+batch");
            return 2;
        }

        string root = FindProjectRoot();
        string tmpDir = Path.Combine(root, "tmp", "csbench");
        Directory.CreateDirectory(tmpDir);

        string stamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");
        string outPath = string.IsNullOrWhiteSpace(outputArg)
            ? Path.Combine(root, "benchmark", "results", Environment.MachineName,
                "csharp", $"cs_macro_results_{stamp}.json")
            : outputArg;
        if (!Path.IsPathRooted(outPath))
            outPath = Path.Combine(root, outPath);
        Directory.CreateDirectory(Path.GetDirectoryName(outPath)!);

        Console.WriteLine($"BCSV C# Benchmark — size={size} rows={numRows}");
        Console.WriteLine($"  workloads: {string.Join(", ", selectedWorkloads)}");
        Console.WriteLine($"  modes:     {string.Join(", ", selectedModes)}");
        Console.WriteLine($"  codecs:    {string.Join(", ", codecConfigs.Select(c => c.label))}");
        Console.WriteLine();

        var results = new List<Dictionary<string, object>>();

        foreach (var workload in selectedWorkloads)
        {
            if (!WorkloadSpecs.TryGetValue(workload, out var cols))
            {
                Console.Error.WriteLine($"Unknown workload: {workload}");
                return 2;
            }

            foreach (var (rowCodec, flags, codecLabel) in codecConfigs)
            {
                string fileBase = Path.Combine(tmpDir, $"{workload}_{codecLabel}");
                string rowFile = $"{fileBase}_row.bcsv";
                string colFile = $"{fileBase}_col.bcsv";
                int caseSeed = seed + (workload.GetHashCode() & 0x7FFFFFFF) % 10000;

                // ── Write benchmarks ───────────────────────────────
                if (selectedModes.Contains("row_write"))
                {
                    Console.Write($"  [{workload}] {codecLabel} row_write... ");
                    var (wMs, fSize) = BenchSequentialWrite(workload, cols, numRows,
                        rowFile, rowCodec, flags, caseSeed);
                    Console.WriteLine($"{numRows / (wMs / 1000.0):N0} row/s ({wMs:F1}ms, {fSize:N0} bytes)");

                    results.Add(MakeResult(workload, "row_write", codecLabel, numRows,
                        wMs, 0, fSize, cols.Length));
                }
                else
                {
                    BenchSequentialWrite(workload, cols, numRows, rowFile, rowCodec, flags, caseSeed);
                }

                // ── Read benchmarks ────────────────────────────────
                if (selectedModes.Contains("row_read"))
                {
                    Console.Write($"  [{workload}] {codecLabel} row_read... ");
                    var (rMs, cnt) = BenchSequentialRead(rowFile);
                    Console.WriteLine($"{cnt / (rMs / 1000.0):N0} row/s ({rMs:F1}ms)");

                    results.Add(MakeResult(workload, "row_read", codecLabel, cnt,
                        0, rMs, new FileInfo(rowFile).Length, cols.Length));
                }

                if (selectedModes.Contains("row_read_fields"))
                {
                    Console.Write($"  [{workload}] {codecLabel} row_read_fields... ");
                    var (rMs, cnt) = BenchSequentialReadFields(rowFile, cols);
                    Console.WriteLine($"{cnt / (rMs / 1000.0):N0} row/s ({rMs:F1}ms)");

                    results.Add(MakeResult(workload, "row_read_fields", codecLabel, cnt,
                        0, rMs, new FileInfo(rowFile).Length, cols.Length));
                }

                if (selectedModes.Contains("direct_access"))
                {
                    Console.Write($"  [{workload}] {codecLabel} direct_access... ");
                    var (rMs, cnt) = BenchDirectAccess(rowFile, cols, numRows);
                    Console.WriteLine($"{cnt / (rMs / 1000.0):N0} row/s ({rMs:F1}ms)");

                    results.Add(MakeResult(workload, "direct_access", codecLabel, cnt,
                        0, rMs, new FileInfo(rowFile).Length, cols.Length));
                }

                // ── Columnar benchmarks ────────────────────────────
                if (selectedModes.Contains("columnar_write"))
                {
                    Console.Write($"  [{workload}] {codecLabel} columnar_write... ");
                    var (wMs, fSize) = BenchColumnarWrite(workload, cols, numRows,
                        colFile, rowCodec, flags, caseSeed);
                    Console.WriteLine($"{numRows / (wMs / 1000.0):N0} row/s ({wMs:F1}ms, {fSize:N0} bytes)");

                    results.Add(MakeResult(workload, "columnar_write", codecLabel, numRows,
                        wMs, 0, fSize, cols.Length));
                }
                else if (selectedModes.Contains("columnar_read"))
                {
                    BenchColumnarWrite(workload, cols, numRows, colFile, rowCodec, flags, caseSeed);
                }

                if (selectedModes.Contains("columnar_read"))
                {
                    Console.Write($"  [{workload}] {codecLabel} columnar_read... ");
                    var (rMs, cnt) = BenchColumnarRead(colFile);
                    Console.WriteLine($"{cnt / (rMs / 1000.0):N0} row/s ({rMs:F1}ms)");

                    results.Add(MakeResult(workload, "columnar_read", codecLabel, cnt,
                        0, rMs, new FileInfo(colFile).Length, cols.Length));
                }
            }
        }

        // ── Cross-compatibility verification ───────────────────────────
        if (verify)
        {
            Console.WriteLine();
            Console.WriteLine("=== Cross-compatibility verification ===");
            string vDir = string.IsNullOrEmpty(verifyDir) ? tmpDir : verifyDir;

            foreach (var pattern in new[] { "*_python.bcsv", "*_cpp.bcsv" })
            {
                var files = Directory.GetFiles(vDir, pattern);
                foreach (var f in files)
                {
                    Console.Write($"  C# reading {Path.GetFileName(f)}... ");
                    try
                    {
                        using var reader = new BcsvReader();
                        reader.Open(f);
                        int count = 0;
                        while (reader.ReadNext()) count++;
                        reader.Close();
                        Console.WriteLine($"OK ({count} rows)");
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"FAIL: {ex.Message}");
                    }
                }
            }
        }

        // ── Write results JSON ─────────────────────────────────────────
        var payload = new Dictionary<string, object>
        {
            ["run_type"] = "CSHARP-MACRO",
            ["size"] = size,
            ["num_rows"] = numRows,
            ["generated_at"] = DateTime.UtcNow.ToString("O"),
            ["results"] = results,
        };

        var json = JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(outPath, json);
        Console.WriteLine($"\nWrote {outPath}");
        return 0;
    }

    static Dictionary<string, object> MakeResult(string dataset, string mode,
        string codec, int rows, double writeMs, double readMs, long fileSize, int numCols)
    {
        return new Dictionary<string, object>
        {
            ["dataset"] = dataset,
            ["mode"] = ModeLabel(mode),
            ["scenario_id"] = "baseline",
            ["access_path"] = mode.Contains("direct") ? "random" : "dense",
            ["codec"] = codec,
            ["selected_columns"] = numCols,
            ["num_columns"] = numCols,
            ["num_rows"] = rows,
            ["write_time_ms"] = writeMs,
            ["read_time_ms"] = readMs,
            ["write_rows_per_sec"] = writeMs > 0 ? rows / (writeMs / 1000.0) : 0.0,
            ["read_rows_per_sec"] = readMs > 0 ? rows / (readMs / 1000.0) : 0.0,
            ["file_size"] = fileSize,
        };
    }

    static string FindProjectRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "CMakeLists.txt")))
                return dir.FullName;
            dir = dir.Parent;
        }
        dir = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "CMakeLists.txt")))
                return dir.FullName;
            dir = dir.Parent;
        }
        return Directory.GetCurrentDirectory();
    }
}
