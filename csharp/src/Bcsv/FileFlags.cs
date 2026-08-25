// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>File flags matching bcsv_file_flags_t in the C API.</summary>
[Flags]
public enum FileFlags
{
    None           = 0,
    /// <summary>
    /// Zero-order-hold row codec. <b>Output-only.</b>
    /// </summary>
    /// <remarks>
    /// Reported by <see cref="BcsvReader.FileFlags"/> and
    /// <see cref="BcsvWriter.FileFlags"/>, and ignored when passed to
    /// <c>BcsvWriter.Open</c>: the row-codec bits are set from the codec name given
    /// to the <see cref="BcsvWriter"/> constructor, so that the header cannot claim
    /// a codec the rows were not written with. Pass <c>"zoh"</c> there instead.
    /// </remarks>
    ZeroOrderHold  = 1 << 0,
    NoFileIndex    = 1 << 1,
    StreamMode     = 1 << 2,
    BatchCompress  = 1 << 3,
    /// <summary>
    /// Delta + variable-length row codec. <b>Output-only</b>, on the same terms as
    /// <see cref="ZeroOrderHold"/> — pass <c>"delta"</c> to the
    /// <see cref="BcsvWriter"/> constructor.
    /// </summary>
    DeltaEncoding  = 1 << 4,
}
