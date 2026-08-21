// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

using System;
using System.Collections;
using System.Collections.Generic;

namespace BCSV
{
    /// <summary>
    /// Defines the column schema (names and types) for a BCSV file.
    /// Wraps the native bcsv_layout_t handle.
    /// </summary>
    public sealed class BcsvLayout : IDisposable, IReadOnlyList<ColumnDefinition>
    {
        internal nint Handle { get; private set; }
        private readonly bool _ownsHandle;

        public BcsvLayout()
        {
            Handle = NativeMethods.bcsv_layout_create();
            _ownsHandle = true;
            if (Handle == 0)
                throw new BcsvException("Failed to create layout");
        }

        internal BcsvLayout(nint handle, bool ownsHandle)
        {
            Handle = handle;
            _ownsHandle = ownsHandle;
        }

        ~BcsvLayout() => Dispose(false);

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        private void Dispose(bool disposing)
        {
            if (_ownsHandle && Handle != 0)
            {
                NativeMethods.bcsv_layout_destroy(Handle);
                Handle = 0;
            }
        }

        public int ColumnCount => (int)NativeMethods.bcsv_layout_column_count(Handle);
        int IReadOnlyCollection<ColumnDefinition>.Count => ColumnCount;

        public ColumnDefinition this[int index]
        {
            get
            {
                var name = NativeMethods.PtrToStringUtf8(
                    NativeMethods.bcsv_layout_column_name(Handle, (nuint)index));
                var type = NativeMethods.bcsv_layout_column_type(Handle, (nuint)index);
                return new ColumnDefinition(name, type, index);
            }
        }

        public BcsvLayout AddColumn(string name, ColumnType type)
        {
            NativeMethods.bcsv_layout_add_column(Handle, (nuint)ColumnCount, name, type);
            return this;
        }

        public BcsvLayout AddColumn(int index, string name, ColumnType type)
        {
            NativeMethods.bcsv_layout_add_column(Handle, (nuint)index, name, type);
            return this;
        }

        public void RemoveColumn(int index)
            => NativeMethods.bcsv_layout_remove_column(Handle, (nuint)index);

        public void Clear()
            => NativeMethods.bcsv_layout_clear(Handle);

        public bool HasColumn(string name)
            => NativeMethods.bcsv_layout_has_column(Handle, name);

        public int ColumnIndex(string name)
            => (int)NativeMethods.bcsv_layout_column_index(Handle, name);

        public string ColumnName(int index)
            => NativeMethods.PtrToStringUtf8(
                NativeMethods.bcsv_layout_column_name(Handle, (nuint)index));

        public ColumnType ColumnType(int index)
            => NativeMethods.bcsv_layout_column_type(Handle, (nuint)index);

        // Keep old names as aliases for backward compat in samples
        public string GetColumnName(int index) => ColumnName(index);
        public ColumnType GetColumnType(int index) => ColumnType(index);
        public int GetColumnIndex(string name) => ColumnIndex(name);

        public void SetColumnName(int index, string name)
            => NativeMethods.bcsv_layout_set_column_name(Handle, (nuint)index, name);

        public void SetColumnType(int index, ColumnType type)
            => NativeMethods.bcsv_layout_set_column_type(Handle, (nuint)index, type);

        public bool IsCompatible(BcsvLayout other)
            => NativeMethods.bcsv_layout_is_compatible(Handle, other.Handle);

        public int ColumnCountByType(ColumnType type)
            => (int)NativeMethods.bcsv_layout_column_count_by_type(Handle, type);

        public int RowDataSize
            => (int)NativeMethods.bcsv_layout_row_data_size(Handle);

        public BcsvLayout Clone()
        {
            var h = NativeMethods.bcsv_layout_clone(Handle);
            return new BcsvLayout(h, ownsHandle: true);
        }

        public override string ToString()
            => NativeMethods.PtrToStringUtf8(NativeMethods.bcsv_layout_to_string(Handle));

        public IEnumerator<ColumnDefinition> GetEnumerator()
        {
            int count = ColumnCount;
            for (int i = 0; i < count; i++)
                yield return this[i];
        }

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}