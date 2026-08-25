// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using Xunit;

namespace Bcsv.Tests;

/// <summary>
/// Tests for the transitional <see cref="BcsvMetadata"/> companion reader.
/// Retire alongside the class when 1.6.0's in-format metadata lands (ToDo E12).
/// </summary>
public class BcsvMetadataTests : IDisposable
{
    private readonly string _tmpDir;

    public BcsvMetadataTests()
    {
        _tmpDir = Path.Combine(Path.GetTempPath(),
            "bcsv_meta_tests_" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(_tmpDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tmpDir)) Directory.Delete(_tmpDir, recursive: true);
        GC.SuppressFinalize(this);
    }

    private string Path_(string name) => Path.Combine(_tmpDir, name);

    /// <summary>Writes a .bcsv stand-in plus a companion describing it.</summary>
    private string WritePair(string name, string body, string json, bool patchSize = true)
    {
        string bcsv = Path_(name);
        File.WriteAllText(bcsv, body);
        // Always substitute: theory cases embed __BYTES__ to mean "the correct
        // size", so each case isolates the one field it is corrupting.
        json = json.Replace("__BYTES__",
            patchSize ? new FileInfo(bcsv).Length.ToString() : "999999");
        File.WriteAllText(bcsv + BcsvMetadata.CompanionSuffix, json);
        return bcsv;
    }

    private const string TenPairs = """
    {
      "metadata_json_version": 1,
      "source_path": "rec.parquet",
      "source_sha256": "aa841aee",
      "bcsv_bytes": __BYTES__,
      "bcsv_rows": 42,
      "key_value_metadata": {
        "rotation_contract": "unit_xyzw_v1",
        "release_id": "rotation-xyzw-v1",
        "producer_commit": "c7671383e9e865e97459ac9bd2e08fa7b6a97cb6",
        "producer_code_sha256": "497e3acf",
        "source_sha256": "aa841aee",
        "calibration_period": "period_1",
        "calibration_sha256": "e4ee24d9",
        "baseline_sha256": "3165192a",
        "time_alignment": "recomputed_from_raw",
        "coordinate_alignment": "period_calibration"
      }
    }
    """;

    [Fact]
    public void CompanionPath_AppendsSuffix()
        => Assert.Equal("a/b.bcsv.meta.json", BcsvMetadata.CompanionPath("a/b.bcsv"));

    [Fact]
    public void ReadCompanion_ReturnsNull_WhenAbsent()
    {
        string bcsv = Path_("lonely.bcsv");
        File.WriteAllText(bcsv, "data");
        Assert.Null(BcsvMetadata.ReadCompanion(bcsv));
    }

    [Fact]
    public void ReadCompanion_ReturnsAllTenPairs()
    {
        string bcsv = WritePair("ok.bcsv", "some data", TenPairs);
        var meta = BcsvMetadata.ReadCompanion(bcsv);

        Assert.NotNull(meta);
        Assert.Equal(10, meta!.Count);
        // The clause T13's acceptance check actually asks for.
        Assert.Equal("unit_xyzw_v1", meta["rotation_contract"]);
        Assert.Equal("rotation-xyzw-v1", meta["release_id"]);
    }

    [Fact]
    public void ReadCompanion_AcceptsMatchingRowCount()
    {
        string bcsv = WritePair("rows.bcsv", "some data", TenPairs);
        var meta = BcsvMetadata.ReadCompanion(bcsv, expectedRows: 42);
        Assert.Equal("unit_xyzw_v1", meta!["rotation_contract"]);
    }

    [Fact]
    public void ReadCompanion_RejectsMismatchedRowCount()
    {
        string bcsv = WritePair("rows2.bcsv", "some data", TenPairs);
        var ex = Assert.Throws<BcsvException>(
            () => BcsvMetadata.ReadCompanion(bcsv, expectedRows: 99));
        Assert.Contains("does not describe", ex.Message);
    }

    [Fact]
    public void ReadCompanion_RejectsMismatchedByteSize()
    {
        // Companion claims a size the file does not have: the stale-companion case.
        string bcsv = WritePair("stale.bcsv", "some data",
            TenPairs.Replace("__BYTES__", "999999"), patchSize: false);
        var ex = Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv));
        Assert.Contains("does not describe", ex.Message);
        Assert.Contains("earlier conversion", ex.Message);
    }

    [Theory]
    [InlineData("\"bcsv_bytes\": \"invalid\"")]
    [InlineData("\"bcsv_bytes\": -1")]
    [InlineData("\"bcsv_bytes\": 12.5")]
    [InlineData("\"bcsv_bytes\": true")]
    [InlineData("\"bcsv_bytes\": {}")]
    [InlineData("\"bcsv_bytes\": __BYTES__, \"bcsv_rows\": \"12\"")]
    [InlineData("\"bcsv_bytes\": __BYTES__, \"bcsv_rows\": -3")]
    [InlineData("\"bcsv_bytes\": __BYTES__, \"bcsv_sha256\": \"nothex\"")]
    [InlineData("\"bcsv_bytes\": __BYTES__, \"bcsv_sha256\": 42")]
    public void ReadCompanion_RejectsMalformedBindingFields(string binding)
    {
        // A binding field that is present but corrupt must not silently skip
        // its check: the API promises to throw for a malformed companion.
        string bcsv = WritePair("mal.bcsv", "some data",
            "{" + binding + ", \"key_value_metadata\": {\"k\": \"v\"}}");
        Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv, expectedRows: 42));
    }

    [Fact]
    public void ReadCompanion_TreatsNullBindingFieldsAsAbsent()
    {
        // null is how --no-source-hash / --no-bcsv-hash record "not computed".
        string bcsv = WritePair("nullbind.bcsv", "some data",
            "{\"bcsv_bytes\": null, \"bcsv_rows\": null, \"bcsv_sha256\": null, " +
            "\"key_value_metadata\": {\"k\": \"v\"}}", patchSize: false);
        Assert.Equal("v", BcsvMetadata.ReadCompanion(bcsv, expectedRows: 7)!["k"]);
    }

    [Fact]
    public void ReadCompanion_VerifiesSha256_AndAcceptsAMatch()
    {
        string bcsv = Path_("digest.bcsv");
        File.WriteAllText(bcsv, "payload bytes");
        string digest = Convert.ToHexString(
            System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(bcsv)))
            .ToLowerInvariant();
        File.WriteAllText(bcsv + BcsvMetadata.CompanionSuffix,
            "{\"bcsv_sha256\": \"" + digest + "\", \"key_value_metadata\": {\"k\": \"v\"}}");
        Assert.Equal("v", BcsvMetadata.ReadCompanion(bcsv)!["k"]);
    }

    [Fact]
    public void ReadCompanion_RejectsMismatchedSha256()
    {
        // The case size-and-rows cannot catch: same shape, different data.
        string bcsv = Path_("digest2.bcsv");
        File.WriteAllText(bcsv, "payload bytes");
        string wrong = new string('a', 64);
        File.WriteAllText(bcsv + BcsvMetadata.CompanionSuffix,
            "{\"bcsv_sha256\": \"" + wrong + "\", \"key_value_metadata\": {\"k\": \"v\"}}");
        var ex = Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv));
        Assert.Contains("SHA-256", ex.Message);
        Assert.Contains("belongs to different data", ex.Message);
    }

    /// <summary>Writes a file whose companion records a digest that is valid
    /// hex but belongs to different data.</summary>
    private string WriteWrongDigestPair(string name, string extraFields = "")
    {
        string bcsv = Path_(name);
        File.WriteAllText(bcsv, "payload bytes");
        string wrong = new string('a', 64);
        File.WriteAllText(bcsv + BcsvMetadata.CompanionSuffix,
            "{\"bcsv_sha256\": \"" + wrong + "\"" + extraFields +
            ", \"key_value_metadata\": {\"k\": \"v\"}}");
        return bcsv;
    }

    [Fact]
    public void ReadCompanion_SkipsDigest_WhenVerifyDigestIsFalse()
    {
        // The caller that cannot afford to read the file it is about to open
        // through direct access: the digest is wrong, and is never consulted.
        string bcsv = WriteWrongDigestPair("skipdigest.bcsv");
        Assert.Equal("v", BcsvMetadata.ReadCompanion(bcsv, -1, verifyDigest: false)!["k"]);
        Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv, -1, verifyDigest: true));
    }

    [Fact]
    public void ReadCompanion_KeepsCheapChecks_WhenVerifyDigestIsFalse()
    {
        // Skipping the digest must not turn ReadCompanion into a no-op: the
        // stale-companion case that bcsv_bytes catches still has to fail.
        string bcsv = WriteWrongDigestPair("skipdigest_bytes.bcsv", ", \"bcsv_bytes\": 999999");
        var ex = Assert.Throws<BcsvException>(
            () => BcsvMetadata.ReadCompanion(bcsv, -1, verifyDigest: false));
        Assert.Contains("999999-byte file", ex.Message);
    }

    [Fact]
    public void ReadCompanion_KeepsRowCheck_WhenVerifyDigestIsFalse()
    {
        string bcsv = WriteWrongDigestPair("skipdigest_rows.bcsv", ", \"bcsv_rows\": 42");
        var ex = Assert.Throws<BcsvException>(
            () => BcsvMetadata.ReadCompanion(bcsv, expectedRows: 41, verifyDigest: false));
        Assert.Contains("42 rows", ex.Message);
    }

    [Fact]
    public void ReadCompanion_RejectsMalformedDigestField_EvenWhenNotVerifying()
    {
        // A present-but-malformed field is a corrupt document either way, and
        // refusing it costs no I/O.
        string bcsv = Path_("baddigest.bcsv");
        File.WriteAllText(bcsv, "payload bytes");
        File.WriteAllText(bcsv + BcsvMetadata.CompanionSuffix,
            """{"bcsv_sha256": "not-a-digest", "key_value_metadata": {"k": "v"}}""");
        var ex = Assert.Throws<BcsvException>(
            () => BcsvMetadata.ReadCompanion(bcsv, -1, verifyDigest: false));
        Assert.Contains("not a SHA-256 hex digest", ex.Message);
    }

    [Fact]
    public void ReadCompanion_DefaultOverloadStillVerifies()
    {
        // The two-argument overload keeps 1.5.16 behaviour exactly.
        string bcsv = WriteWrongDigestPair("default_verifies.bcsv");
        Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv));
        Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv, -1));
    }

    [Fact]
    public void ReadCompanion_ThrowsOnMalformedJson()
    {
        string bcsv = WritePair("bad.bcsv", "d", "{not json");
        var ex = Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv));
        Assert.Contains("not valid JSON", ex.Message);
    }

    [Fact]
    public void ReadCompanion_ThrowsWhenSectionMissing()
    {
        string bcsv = WritePair("nosec.bcsv", "d", """{"bcsv_bytes": __BYTES__}""");
        var ex = Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv));
        Assert.Contains("key_value_metadata", ex.Message);
    }

    [Fact]
    public void ReadCompanion_ThrowsOnNonStringValue()
    {
        string bcsv = WritePair("nonstr.bcsv", "d",
            """{"bcsv_bytes": __BYTES__, "key_value_metadata": {"n": 5}}""");
        var ex = Assert.Throws<BcsvException>(() => BcsvMetadata.ReadCompanion(bcsv));
        Assert.Contains("does not hold a string", ex.Message);
    }

    [Fact]
    public void ReadCompanion_HandlesEscapesAndUnicode()
    {
        string bcsv = WritePair("esc.bcsv", "d", """
        {
          "bcsv_bytes": __BYTES__,
          "key_value_metadata": {
            "quote": "a\"b",
            "backslash": "a\\b",
            "newline": "a\nb",
            "tab": "a\tb",
            "unicode": "世界",
            "solidus": "a\/b"
          }
        }
        """);
        var meta = BcsvMetadata.ReadCompanion(bcsv)!;
        Assert.Equal("a\"b", meta["quote"]);
        Assert.Equal("a\\b", meta["backslash"]);
        Assert.Equal("a\nb", meta["newline"]);
        Assert.Equal("a\tb", meta["tab"]);
        Assert.Equal("世界", meta["unicode"]);
        Assert.Equal("a/b", meta["solidus"]);
    }

    [Fact]
    public void ReadCompanion_HandlesEmptyMetadataObject()
    {
        string bcsv = WritePair("empty.bcsv", "d",
            """{"bcsv_bytes": __BYTES__, "key_value_metadata": {}}""");
        var meta = BcsvMetadata.ReadCompanion(bcsv);
        Assert.NotNull(meta);
        Assert.Empty(meta!);
    }

    [Fact]
    public void ReadCompanion_TolerantOfNullAndUnknownFields()
    {
        // source_sha256 is null when --no-source-hash was used; unknown keys
        // must not break a reader written against an older schema version.
        string bcsv = WritePair("nulls.bcsv", "d", """
        {
          "bcsv_bytes": __BYTES__,
          "source_sha256": null,
          "future_field": ["a", 1, true, null],
          "key_value_metadata": {"k": "v"}
        }
        """);
        Assert.Equal("v", BcsvMetadata.ReadCompanion(bcsv)!["k"]);
    }

    [Fact]
    public void ReadCompanion_SkipsBindingCheckWhenFieldsAbsent()
    {
        // A hand-written companion with no binding fields still reads, so the
        // format stays usable for callers who generate it themselves.
        string bcsv = WritePair("nobind.bcsv", "d",
            """{"key_value_metadata": {"k": "v"}}""", patchSize: false);
        Assert.Equal("v", BcsvMetadata.ReadCompanion(bcsv, expectedRows: 7)!["k"]);
    }
}
