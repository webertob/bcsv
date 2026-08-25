// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Bcsv;

/// <summary>
/// Reads the <c>&lt;file&gt;.bcsv.meta.json</c> companion written by pybcsv's
/// <c>parquet2bcsv</c>, which carries file-level key/value metadata (provenance,
/// release identifiers, contract markers) that the BCSV header has no room for.
/// </summary>
/// <remarks>
/// <para>
/// <b>Transitional — scheduled for removal.</b> This class exists only because
/// the BCSV format has no key/value metadata section. Version 1.6.0 adds one,
/// exposed as <c>BcsvReader.Metadata</c>, and this class is retired in the first
/// release after that: see item E12 in <c>ToDo.md</c>. Read metadata through the
/// reader as soon as it is available, and treat anything written against this
/// class as code with a known end date.
/// </para>
/// <para>
/// The companion is addressed only by path, so it verifies that it describes
/// the BCSV file beside it and refuses one left behind by an earlier conversion
/// to the same output name — a companion that quietly described the wrong file
/// would defeat the point of carrying a contract marker at all. The recorded
/// <c>bcsv_sha256</c> is the identity check; <c>bcsv_bytes</c> and
/// <c>bcsv_rows</c> are cheap pre-checks that fail faster with a clearer
/// message but are only a heuristic on their own, since two recordings of the
/// same shape can share both. When the writer omitted the digest
/// (<c>--no-bcsv-hash</c>) that heuristic is all there is.
/// </para>
/// <para>
/// Verifying the digest costs a full read of the BCSV file, which is the very
/// cost a caller opening a large recording through direct access is trying to
/// avoid. The three-argument overload of <see cref="ReadCompanion(string, long, bool)"/>
/// lets that caller keep the cheap pre-checks alone: verify once where the file
/// enters the project, skip it on the hot path.
/// </para>
/// </remarks>
public static class BcsvMetadata
{
    /// <summary>Suffix appended to a BCSV path to locate its companion.</summary>
    public const string CompanionSuffix = ".meta.json";

    /// <summary>Path of the companion belonging to <paramref name="bcsvPath"/>.</summary>
    public static string CompanionPath(string bcsvPath)
    {
        if (bcsvPath is null) throw new ArgumentNullException(nameof(bcsvPath));
        return bcsvPath + CompanionSuffix;
    }

    /// <summary>
    /// Read the key/value metadata companion for <paramref name="bcsvPath"/>,
    /// verifying the recorded digest against the file.
    /// </summary>
    /// <param name="bcsvPath">Path to the <c>.bcsv</c> file (not the companion).</param>
    /// <param name="expectedRows">
    /// Row count of the BCSV file, if known (e.g. <c>reader.RowCount</c>). When
    /// non-negative it is checked against the recorded count. Pass -1 to skip.
    /// </param>
    /// <returns>The metadata pairs, or <c>null</c> if no companion exists.</returns>
    /// <exception cref="BcsvException">
    /// The companion exists but is malformed, or does not describe
    /// <paramref name="bcsvPath"/>.
    /// </exception>
    public static IReadOnlyDictionary<string, string>? ReadCompanion(
        string bcsvPath, long expectedRows = -1)
        => ReadCompanion(bcsvPath, expectedRows, verifyDigest: true);

    /// <summary>
    /// Read the key/value metadata companion for <paramref name="bcsvPath"/>,
    /// choosing which binding checks to pay for.
    /// </summary>
    /// <param name="bcsvPath">Path to the <c>.bcsv</c> file (not the companion).</param>
    /// <param name="expectedRows">
    /// Row count of the BCSV file, if known (e.g. <c>reader.RowCount</c>). When
    /// non-negative it is checked against the recorded count. Pass -1 to skip.
    /// </param>
    /// <param name="verifyDigest">
    /// <c>true</c> to hash the BCSV file and compare it against the recorded
    /// <c>bcsv_sha256</c> — the identity check. <c>false</c> to keep only the cheap
    /// pre-checks, <c>bcsv_bytes</c> and <c>bcsv_rows</c>, which read no file data
    /// at all. Skipping the digest trades certainty for a full read of the file:
    /// bytes and rows together are a heuristic, since two recordings of the same
    /// shape can share both. It is the right trade for a reader that opens a
    /// multi-gigabyte recording to touch a few rows of it — verify once at ingest,
    /// pass <c>false</c> per open.
    /// </param>
    /// <returns>The metadata pairs, or <c>null</c> if no companion exists.</returns>
    /// <exception cref="BcsvException">
    /// The companion exists but is malformed, or does not describe
    /// <paramref name="bcsvPath"/>.
    /// </exception>
    // A separate overload rather than a third optional parameter: C# bakes default
    // argument values into the call site, so adding one would break callers already
    // compiled against this assembly.
    public static IReadOnlyDictionary<string, string>? ReadCompanion(
        string bcsvPath, long expectedRows, bool verifyDigest)
    {
        string path = CompanionPath(bcsvPath);
        if (!File.Exists(path)) return null;

        string text;
        try
        {
            text = File.ReadAllText(path, Encoding.UTF8);
        }
        catch (IOException ex)
        {
            throw new BcsvException($"Metadata companion '{path}' could not be read: {ex.Message}", ex);
        }

        Dictionary<string, object?> doc;
        try
        {
            doc = MiniJson.ParseObject(text);
        }
        catch (FormatException ex)
        {
            throw new BcsvException(
                $"Metadata companion '{path}' is not valid JSON: {ex.Message}. " +
                "Delete it or regenerate it with parquet2bcsv.", ex);
        }

        VerifyBindsTo(doc, path, bcsvPath, expectedRows, verifyDigest);

        if (!doc.TryGetValue("key_value_metadata", out object? raw) ||
            raw is not Dictionary<string, object?> pairs)
        {
            throw new BcsvException(
                $"Metadata companion '{path}' has no 'key_value_metadata' object.");
        }

        var result = new Dictionary<string, string>(pairs.Count, StringComparer.Ordinal);
        foreach (KeyValuePair<string, object?> kv in pairs)
        {
            if (kv.Value is not string value)
            {
                throw new BcsvException(
                    $"Metadata companion '{path}': key '{kv.Key}' does not hold a string.");
            }
            result[kv.Key] = value;
        }
        return result;
    }

    /// <summary>
    /// Read an integral binding field, refusing a malformed one. A field that
    /// is present but not a non-negative integer is a corrupt document, not an
    /// absent field: skipping it would silently drop the very check the caller
    /// is relying on. <c>null</c> means "not computed" and is treated as absent.
    /// </summary>
    private static long? BindingInt(Dictionary<string, object?> doc, string key, string path)
    {
        if (!doc.TryGetValue(key, out object? raw) || raw is null) return null;
        if (raw is not double value)
        {
            throw new BcsvException(
                $"Metadata companion '{path}': '{key}' is not a number.");
        }
        if (value < 0 || value > long.MaxValue || Math.Floor(value) != value)
        {
            throw new BcsvException(
                $"Metadata companion '{path}': '{key}' is {value}, not a non-negative integer.");
        }
        return (long)value;
    }

    /// <summary>
    /// Refuse a companion that does not describe this BCSV file. Mirrors
    /// <c>_verify_metadata_json_binds_to</c> in pybcsv's parquet_utils:
    /// <c>bcsv_sha256</c> is the identity check, while <c>bcsv_bytes</c> and
    /// <c>bcsv_rows</c> are cheap pre-checks that on their own are only a
    /// heuristic — two recordings of the same shape can share both. With
    /// <paramref name="verifyDigest"/> false the identity check is skipped and
    /// that heuristic is all that runs.
    /// </summary>
    private static void VerifyBindsTo(
        Dictionary<string, object?> doc, string path, string bcsvPath, long expectedRows,
        bool verifyDigest)
    {
        bool fileExists = File.Exists(bcsvPath);

        long? recordedBytes = BindingInt(doc, "bcsv_bytes", path);
        if (recordedBytes.HasValue && fileExists)
        {
            long actual = new FileInfo(bcsvPath).Length;
            if (actual != recordedBytes.Value)
            {
                throw new BcsvException(
                    $"Metadata companion '{path}' does not describe '{bcsvPath}': it records a " +
                    $"{recordedBytes.Value}-byte file, but that file is {actual} bytes. It is " +
                    "most likely left over from an earlier conversion to the same output name.");
            }
        }

        long? recordedRows = BindingInt(doc, "bcsv_rows", path);
        if (recordedRows.HasValue && expectedRows >= 0 && recordedRows.Value != expectedRows)
        {
            throw new BcsvException(
                $"Metadata companion '{path}' does not describe '{bcsvPath}': it records " +
                $"{recordedRows.Value} rows, but the file holds {expectedRows}. It is most " +
                "likely left over from an earlier conversion to the same output name.");
        }

        if (!doc.TryGetValue("bcsv_sha256", out object? rawDigest) || rawDigest is null) return;
        if (rawDigest is not string digest || !IsSha256Hex(digest))
        {
            // Checked even when the digest is not verified: a field that is present
            // but malformed is a corrupt document, and saying so costs no I/O.
            // Same rule as BindingInt.
            throw new BcsvException(
                $"Metadata companion '{path}': 'bcsv_sha256' is not a SHA-256 hex digest.");
        }
        if (!verifyDigest || !fileExists) return;
        string actualDigest = FileSha256(bcsvPath);
        if (!string.Equals(actualDigest, digest, StringComparison.Ordinal))
        {
            throw new BcsvException(
                $"Metadata companion '{path}' does not describe '{bcsvPath}': the file's SHA-256 " +
                $"is {actualDigest}, but the document records {digest}. The provenance in it " +
                "belongs to different data.");
        }
    }

    private static bool IsSha256Hex(string value)
    {
        if (value.Length != 64) return false;
        foreach (char c in value)
        {
            bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!hex) return false;
        }
        return true;
    }

    private static string FileSha256(string path)
    {
        using var sha = System.Security.Cryptography.SHA256.Create();
        using FileStream fs = File.OpenRead(path);
        byte[] hash = sha.ComputeHash(fs);
        var sb = new StringBuilder(hash.Length * 2);
        foreach (byte b in hash) sb.Append(b.ToString("x2", CultureInfo.InvariantCulture));
        return sb.ToString();
    }

    /// <summary>
    /// Just enough JSON to read the companion document. Unity 2021.3 has no
    /// <c>System.Text.Json</c>, and the Unity package deliberately carries no
    /// third-party dependencies, so this is hand-rolled and kept identical
    /// between the NuGet and Unity copies. It retires with the class.
    /// </summary>
    internal static class MiniJson
    {
        public static Dictionary<string, object?> ParseObject(string text)
        {
            int i = 0;
            object? value = ParseValue(text, ref i);
            SkipWhitespace(text, ref i);
            if (i != text.Length) throw new FormatException($"trailing content at offset {i}");
            if (value is not Dictionary<string, object?> obj)
                throw new FormatException("document root is not an object");
            return obj;
        }

        private static object? ParseValue(string s, ref int i)
        {
            SkipWhitespace(s, ref i);
            if (i >= s.Length) throw new FormatException("unexpected end of input");
            return s[i] switch
            {
                '{' => ParseObjectBody(s, ref i),
                '[' => ParseArray(s, ref i),
                '"' => ParseString(s, ref i),
                't' => ParseLiteral(s, ref i, "true", true),
                'f' => ParseLiteral(s, ref i, "false", false),
                'n' => ParseLiteral(s, ref i, "null", null),
                _ => ParseNumber(s, ref i),
            };
        }

        private static Dictionary<string, object?> ParseObjectBody(string s, ref int i)
        {
            var obj = new Dictionary<string, object?>(StringComparer.Ordinal);
            i++; // '{'
            SkipWhitespace(s, ref i);
            if (i < s.Length && s[i] == '}') { i++; return obj; }
            while (true)
            {
                SkipWhitespace(s, ref i);
                if (i >= s.Length || s[i] != '"') throw new FormatException($"expected key at offset {i}");
                string key = ParseString(s, ref i);
                SkipWhitespace(s, ref i);
                if (i >= s.Length || s[i] != ':') throw new FormatException($"expected ':' at offset {i}");
                i++;
                obj[key] = ParseValue(s, ref i);
                SkipWhitespace(s, ref i);
                if (i >= s.Length) throw new FormatException("unterminated object");
                if (s[i] == ',') { i++; continue; }
                if (s[i] == '}') { i++; return obj; }
                throw new FormatException($"expected ',' or '}}' at offset {i}");
            }
        }

        private static List<object?> ParseArray(string s, ref int i)
        {
            var list = new List<object?>();
            i++; // '['
            SkipWhitespace(s, ref i);
            if (i < s.Length && s[i] == ']') { i++; return list; }
            while (true)
            {
                list.Add(ParseValue(s, ref i));
                SkipWhitespace(s, ref i);
                if (i >= s.Length) throw new FormatException("unterminated array");
                if (s[i] == ',') { i++; continue; }
                if (s[i] == ']') { i++; return list; }
                throw new FormatException($"expected ',' or ']' at offset {i}");
            }
        }

        private static string ParseString(string s, ref int i)
        {
            i++; // opening quote
            var sb = new StringBuilder();
            while (true)
            {
                if (i >= s.Length) throw new FormatException("unterminated string");
                char c = s[i++];
                if (c == '"') return sb.ToString();
                if (c != '\\') { sb.Append(c); continue; }

                if (i >= s.Length) throw new FormatException("unterminated escape");
                char esc = s[i++];
                switch (esc)
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        if (i + 4 > s.Length) throw new FormatException("truncated \\u escape");
                        if (!ushort.TryParse(s.AsSpan(i, 4), NumberStyles.HexNumber,
                                             CultureInfo.InvariantCulture, out ushort code))
                            throw new FormatException($"bad \\u escape at offset {i}");
                        sb.Append((char)code);
                        i += 4;
                        break;
                    default: throw new FormatException($"unknown escape '\\{esc}' at offset {i - 1}");
                }
            }
        }

        private static object? ParseLiteral(string s, ref int i, string literal, object? value)
        {
            if (i + literal.Length > s.Length ||
                string.CompareOrdinal(s, i, literal, 0, literal.Length) != 0)
                throw new FormatException($"invalid literal at offset {i}");
            i += literal.Length;
            return value;
        }

        private static double ParseNumber(string s, ref int i)
        {
            int start = i;
            if (i < s.Length && (s[i] == '-' || s[i] == '+')) i++;
            while (i < s.Length && (char.IsDigit(s[i]) || s[i] == '.' ||
                                    s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) i++;
            if (i == start ||
                !double.TryParse(s.AsSpan(start, i - start), NumberStyles.Float,
                                 CultureInfo.InvariantCulture, out double value))
                throw new FormatException($"invalid number at offset {start}");
            return value;
        }

        private static void SkipWhitespace(string s, ref int i)
        {
            while (i < s.Length && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        }
    }
}
