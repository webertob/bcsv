// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

namespace BCSV
{
    /// <summary>
    /// Defaults shared by every managed entry point that opens a writer.
    /// </summary>
    /// <remarks>
    /// These mirror the native <c>bcsv::DEFAULT_*</c> constants in
    /// <c>include/bcsv/definitions.h</c>. They exist as a single named constant
    /// because the value used to be repeated as a literal in each writer
    /// overload, and the copies drifted: <see cref="BcsvWriter.Open"/> moved to
    /// the current default while <see cref="BcsvColumns.WriteColumns"/> kept
    /// writing level 1, so the same data compressed differently depending on
    /// which API you called. Every managed default must reference these, never
    /// a literal.
    /// </remarks>
    public static class BcsvDefaults
    {
        /// <summary>
        /// Default LZ4 compression level. Level selects between two compressors
        /// rather than moving a smooth dial: on the default batch codec, 1-5 are
        /// <c>LZ4_compress_fast</c> and 6-9 are LZ4HC, so 6 is a step change and
        /// the knee of the size/CPU curve. The wire format is unaffected.
        /// </summary>
        public const int CompressionLevel = 6;

        /// <summary>Default packet/block size in KiB.</summary>
        public const int BlockSizeKb = 8192;
    }
}
