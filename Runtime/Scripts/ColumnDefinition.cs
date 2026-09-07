// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

namespace BCSV
{
    /// <summary>Describes a single column in a BCSV layout.</summary>
    public readonly struct ColumnDefinition
    {
        public string Name { get; }
        public ColumnType Type { get; }
        public int Index { get; }

        public ColumnDefinition(string name, ColumnType type, int index)
        {
            Name = name;
            Type = type;
            Index = index;
        }
    }
}
