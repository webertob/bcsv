// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>Describes a single column in a BCSV layout.</summary>
public readonly record struct ColumnDefinition(string Name, ColumnType Type, int Index);
