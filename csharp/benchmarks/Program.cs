using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;

enum ColumnType
{
    BOOL = 0,
    UINT8 = 1,
    UINT16 = 2,
    UINT32 = 3,
    UINT64 = 4,
    INT8 = 5,
    INT16 = 6,
    INT32 = 7,
    INT64 = 8,
    FLOAT = 9,
    DOUBLE = 10,
    STRING = 11
}

[Flags]
enum FileFlags
{
    NONE = 0,
    ZOH = 1
}

static class Native
{
    const string Lib = "bcsv_c_api";

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr bcsv_layout_create();
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_layout_destroy(IntPtr layout);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern bool bcsv_layout_add_column(IntPtr layout, UIntPtr index, string name, ColumnType type);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr bcsv_writer_create(IntPtr layout);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_writer_destroy(IntPtr writer);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern bool bcsv_writer_open(IntPtr writer, string filename, bool overwrite, int compress, int blockSizeKb, FileFlags flags);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr bcsv_writer_row(IntPtr writer);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern bool bcsv_writer_next(IntPtr writer);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_writer_close(IntPtr writer);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr bcsv_reader_create(int mode);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_reader_destroy(IntPtr reader);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern bool bcsv_reader_open(IntPtr reader, string filename);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern bool bcsv_reader_next(IntPtr reader);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_reader_close(IntPtr reader);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_bool(IntPtr row, int col, bool value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_uint8(IntPtr row, int col, byte value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_uint32(IntPtr row, int col, uint value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_int32(IntPtr row, int col, int value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_int64(IntPtr row, int col, long value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_float(IntPtr row, int col, float value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_double(IntPtr row, int col, double value);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void bcsv_row_set_string(IntPtr row, int col, string value);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr bcsv_last_error();

    public static string LastError()
    {
        var ptr = bcsv_last_error();
        return ptr == IntPtr.Zero ? "unknown" : Marshal.PtrToStringAnsi(ptr) ?? "unknown";
    }
}

record ColumnDef(string Name, ColumnType Type);

record WorkloadSpec(string Name, List<ColumnDef> Columns);

static class Workloads
{
    private static readonly string[] WeatherStations = { "SEA", "HAM", "MUC", "SFO", "NYC" };
    private static readonly string[] IotFirmware = { "1.2.0", "1.2.1", "1.3.0", "1.3.1" };
    private static readonly string[] IotRegions = { "eu-west", "us-east", "ap-south" };
    private static readonly string[] FinancialSymbols = { "AAPL", "MSFT", "NVDA", "AMZN", "GOOG" };
    private static readonly string[] FinancialVenues = { "XNAS", "XNYS", "BATS" };
    private static readonly string[] FinancialSides = { "BUY", "SELL" };
    private static readonly string[] FinancialTypes = { "LMT", "MKT", "IOC" };
    private static readonly string[] FinancialTraders = { "T1", "T2", "T3", "T4" };

    public static readonly Dictionary<string, WorkloadSpec> Specs = new()
    {
        ["weather_timeseries"] = new(
            "weather_timeseries",
            new()
            {
                new("timestamp", ColumnType.INT64),
                new("station", ColumnType.STRING),
                new("temperature", ColumnType.FLOAT),
                new("humidity", ColumnType.FLOAT),
                new("pressure", ColumnType.DOUBLE),
                new("wind_speed", ColumnType.FLOAT),
                new("raining", ColumnType.BOOL),
                new("quality", ColumnType.UINT8),
            }
        ),
        ["iot_fleet"] = new(
            "iot_fleet",
            new()
            {
                new("timestamp", ColumnType.INT64),
                new("device_id", ColumnType.UINT32),
                new("firmware", ColumnType.STRING),
                new("region", ColumnType.STRING),
                new("battery", ColumnType.FLOAT),
                new("temperature", ColumnType.FLOAT),
                new("vibration", ColumnType.DOUBLE),
                new("online", ColumnType.BOOL),
            }
        ),
        ["financial_orders"] = new(
            "financial_orders",
            new()
            {
                new("timestamp", ColumnType.INT64),
                new("symbol", ColumnType.STRING),
                new("venue", ColumnType.STRING),
                new("side", ColumnType.STRING),
                new("qty", ColumnType.INT32),
                new("price", ColumnType.DOUBLE),
                new("order_type", ColumnType.STRING),
                new("trader", ColumnType.STRING),
                new("is_cancel", ColumnType.BOOL),
            }
        )
    };

    public static void SetRow(string workload, IntPtr row, int rowIndex, Random rng)
    {
        long ts = 1_700_000_000L + rowIndex;

        if (workload == "weather_timeseries")
        {
            Native.bcsv_row_set_int64(row, 0, ts);
            Native.bcsv_row_set_string(row, 1, WeatherStations[rowIndex % WeatherStations.Length]);
            Native.bcsv_row_set_float(row, 2, (float)(12.0 + 8.0 * (rowIndex % 97) / 97.0 + (rng.NextDouble() - 0.5)));
            Native.bcsv_row_set_float(row, 3, (float)(45.0 + 40.0 * (rowIndex % 53) / 53.0 + 2.0 * (rng.NextDouble() - 0.5)));
            Native.bcsv_row_set_double(row, 4, 1000.0 + 15.0 * (rowIndex % 41) / 41.0 + 0.4 * (rng.NextDouble() - 0.5));
            Native.bcsv_row_set_float(row, 5, (float)(3.0 + 10.0 * (rowIndex % 31) / 31.0));
            Native.bcsv_row_set_bool(row, 6, rowIndex % 17 == 0);
            Native.bcsv_row_set_uint8(row, 7, (byte)(rowIndex % 4));
            return;
        }

        if (workload == "iot_fleet")
        {
            Native.bcsv_row_set_int64(row, 0, ts);
            Native.bcsv_row_set_uint32(row, 1, (uint)(1000 + (rowIndex % 5000)));
            Native.bcsv_row_set_string(row, 2, IotFirmware[rowIndex % IotFirmware.Length]);
            Native.bcsv_row_set_string(row, 3, IotRegions[rowIndex % IotRegions.Length]);
            Native.bcsv_row_set_float(row, 4, (float)(100.0 - (rowIndex % 100) * 0.3));
            Native.bcsv_row_set_float(row, 5, (float)(20.0 + (rowIndex % 120) * 0.1));
            Native.bcsv_row_set_double(row, 6, (rowIndex % 40) * 0.02 + rng.NextDouble() * 0.01);
            Native.bcsv_row_set_bool(row, 7, rowIndex % 29 != 0);
            return;
        }

        if (workload == "financial_orders")
        {
            Native.bcsv_row_set_int64(row, 0, ts);
            Native.bcsv_row_set_string(row, 1, FinancialSymbols[rowIndex % FinancialSymbols.Length]);
            Native.bcsv_row_set_string(row, 2, FinancialVenues[rowIndex % FinancialVenues.Length]);
            Native.bcsv_row_set_string(row, 3, FinancialSides[rowIndex % FinancialSides.Length]);
            Native.bcsv_row_set_int32(row, 4, 1 + (rowIndex % 5000));
            Native.bcsv_row_set_double(row, 5, 90.0 + (rowIndex % 200) * 0.05 + 0.04 * (rng.NextDouble() - 0.5));
            Native.bcsv_row_set_string(row, 6, FinancialTypes[rowIndex % FinancialTypes.Length]);
            Native.bcsv_row_set_string(row, 7, FinancialTraders[rowIndex % FinancialTraders.Length]);
            Native.bcsv_row_set_bool(row, 8, rowIndex % 23 == 0);
            return;
        }

        throw new ArgumentException($"Unknown workload: {workload}");
    }
}

class Program
{
    static readonly Dictionary<string, int> SizeRows = new()
    {
        ["S"] = 10_000,
        ["M"] = 100_000,
        ["L"] = 500_000,
    };

    static IntPtr BuildLayout(WorkloadSpec spec)
    {
        IntPtr layout = Native.bcsv_layout_create();
        for (int i = 0; i < spec.Columns.Count; i++)
        {
            bool ok = Native.bcsv_layout_add_column(layout, (UIntPtr)i, spec.Columns[i].Name, spec.Columns[i].Type);
            if (!ok)
            {
                throw new InvalidOperationException($"bcsv_layout_add_column failed: {Native.LastError()}");
            }
        }
        return layout;
    }

    static FileFlags ParseFileFlags(string token)
    {
        string normalized = (token ?? string.Empty).Trim().ToLowerInvariant();
        return normalized switch
        {
            "" => FileFlags.NONE,
            "none" => FileFlags.NONE,
            "zoh" => FileFlags.ZOH,
            "zero_order_hold" => FileFlags.ZOH,
            _ => throw new ArgumentException($"Invalid --flags value: {token}. Use none or zoh."),
        };
    }

    static (double writeMs, double readMs, int rows, long fileSize) RunCase(string workload, WorkloadSpec spec, int numRows, string outputDir, int seed, FileFlags flags)
    {
        IntPtr layout = BuildLayout(spec);
        IntPtr writer = Native.bcsv_writer_create(layout);
        string filePath = Path.Combine(outputDir, $"{workload}_csharp.bcsv");

        try
        {
            bool openOk = Native.bcsv_writer_open(writer, filePath, true, 1, 64, flags);
            if (!openOk)
            {
                throw new InvalidOperationException($"bcsv_writer_open failed: {Native.LastError()}");
            }

            var swWrite = Stopwatch.StartNew();
            IntPtr row = Native.bcsv_writer_row(writer);
            var rng = new Random(seed);
            for (int i = 0; i < numRows; i++)
            {
                Workloads.SetRow(workload, row, i, rng);
                if (!Native.bcsv_writer_next(writer))
                {
                    throw new InvalidOperationException($"bcsv_writer_next failed: {Native.LastError()}");
                }
            }
            swWrite.Stop();
            Native.bcsv_writer_close(writer);

            IntPtr reader = Native.bcsv_reader_create(0);
            try
            {
                if (!Native.bcsv_reader_open(reader, filePath))
                {
                    throw new InvalidOperationException($"bcsv_reader_open failed: {Native.LastError()}");
                }

                int count = 0;
                var swRead = Stopwatch.StartNew();
                while (Native.bcsv_reader_next(reader))
                {
                    count++;
                }
                swRead.Stop();
                Native.bcsv_reader_close(reader);

                long size = new FileInfo(filePath).Length;
                return (swWrite.Elapsed.TotalMilliseconds, swRead.Elapsed.TotalMilliseconds, count, size);
            }
            finally
            {
                Native.bcsv_reader_destroy(reader);
            }
        }
        finally
        {
            Native.bcsv_writer_destroy(writer);
            Native.bcsv_layout_destroy(layout);
        }
    }

    static int Main(string[] args)
    {
        string size = "S";
        string workloadsArg = "weather_timeseries,iot_fleet,financial_orders";
        string outputArg = "";
        int seed = 42;
        string flagsArg = "none";

        foreach (string arg in args)
        {
            if (arg.StartsWith("--size=")) size = arg[7..].ToUpperInvariant();
            if (arg.StartsWith("--workloads=")) workloadsArg = arg[12..];
            if (arg.StartsWith("--output=")) outputArg = arg[9..];
            if (arg.StartsWith("--seed=")) seed = int.Parse(arg[7..]);
            if (arg.StartsWith("--flags=")) flagsArg = arg[8..];
        }

        FileFlags selectedFlags;
        try
        {
            selectedFlags = ParseFileFlags(flagsArg);
        }
        catch (ArgumentException ex)
        {
            Console.Error.WriteLine(ex.Message);
            return 2;
        }

        if (!SizeRows.TryGetValue(size, out int numRows))
        {
            Console.Error.WriteLine("Invalid --size. Use S, M, or L.");
            return 2;
        }

        string[] selected = workloadsArg.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        foreach (string name in selected)
        {
            if (!Workloads.Specs.ContainsKey(name))
            {
                Console.Error.WriteLine($"Unknown workload: {name}");
                return 2;
            }
        }

        string root = Directory.GetCurrentDirectory();
        string outPath = outputArg;
        if (string.IsNullOrWhiteSpace(outPath))
        {
            string stamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");
            outPath = Path.Combine(root, "benchmark", "results", Environment.MachineName, "csharp", $"cs_macro_results_{stamp}.json");
        }

        if (!Path.IsPathRooted(outPath))
        {
            outPath = Path.Combine(root, outPath);
        }

        Directory.CreateDirectory(Path.GetDirectoryName(outPath)!);
        string tempDir = Path.Combine(root, "tmp", "csbench");
        Directory.CreateDirectory(tempDir);

        var results = new List<Dictionary<string, object>>();
        for (int index = 0; index < selected.Length; index++)
        {
            string workload = selected[index];
            Console.WriteLine($"[run] workload={workload} rows={numRows}");
            var spec = Workloads.Specs[workload];
            int caseSeed = seed + index * 101;
            var run = RunCase(workload, spec, numRows, tempDir, caseSeed, selectedFlags);

            results.Add(new Dictionary<string, object>
            {
                ["dataset"] = workload,
                ["mode"] = "BCSV CSharp PInvoke",
                ["scenario_id"] = "baseline",
                ["access_path"] = "dense",
                ["selected_columns"] = spec.Columns.Count,
                ["num_columns"] = spec.Columns.Count,
                ["num_rows"] = run.rows,
                ["write_time_ms"] = run.writeMs,
                ["read_time_ms"] = run.readMs,
                ["write_rows_per_sec"] = run.rows / (run.writeMs / 1000.0),
                ["read_rows_per_sec"] = run.rows / (run.readMs / 1000.0),
                ["file_size"] = run.fileSize,
            });
        }

        var payload = new Dictionary<string, object>
        {
            ["run_type"] = "CSHARP-MACRO",
            ["size"] = size,
            ["num_rows"] = numRows,
            ["file_flags"] = selectedFlags.ToString(),
            ["generated_at"] = DateTime.UtcNow.ToString("O"),
            ["results"] = results,
        };

        var json = JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(outPath, json);
        Console.WriteLine($"Wrote {outPath}");
        return 0;
    }
}
