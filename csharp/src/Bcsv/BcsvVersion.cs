// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>Library and file format version information.</summary>
public static class BcsvVersion
{
    public static string Version
        => NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_version());

    public static int Major => NativeMethods.bcsv_version_major();
    public static int Minor => NativeMethods.bcsv_version_minor();
    public static int Patch => NativeMethods.bcsv_version_patch();

    public static string FormatVersion
        => NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_format_version());
}
