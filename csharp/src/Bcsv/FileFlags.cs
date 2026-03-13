// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>File flags matching bcsv_file_flags_t in the C API.</summary>
[Flags]
public enum FileFlags
{
    None           = 0,
    ZeroOrderHold  = 1 << 0,
    NoFileIndex    = 1 << 1,
    StreamMode     = 1 << 2,
    BatchCompress  = 1 << 3,
    DeltaEncoding  = 1 << 4,
}
