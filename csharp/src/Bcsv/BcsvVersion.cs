// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>Library version information (unified with wire format since v1.5.0).</summary>
public static class BcsvVersion
{
    public static string Version
        => NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_version());

    public static int Major => NativeMethods.bcsv_version_major();
    public static int Minor => NativeMethods.bcsv_version_minor();
    public static int Patch => NativeMethods.bcsv_version_patch();
}
