// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>Column data types matching bcsv_type_t in the C API.</summary>
public enum ColumnType
{
    Bool   = 0,
    UInt8  = 1,
    UInt16 = 2,
    UInt32 = 3,
    UInt64 = 4,
    Int8   = 5,
    Int16  = 6,
    Int32  = 7,
    Int64  = 8,
    Float  = 9,
    Double = 10,
    String = 11
}
