// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System.Collections;

namespace Bcsv;

/// <summary>
/// Expression-based filter and projection over a BcsvReader.
/// Wraps the native bcsv_sampler_t handle.
/// </summary>
public sealed class BcsvSampler : IDisposable, IEnumerable<BcsvRow>
{
    private nint _handle;
    private int _disposed;   // Interlocked claim flag: 0 = live
    private readonly BcsvReader _reader;   // keep-alive: sampler borrows its reader
    private BcsvLayout? _outputLayout;
    private BcsvRow _row;

    /// <param name="reader">An opened reader. Kept alive while the sampler lives.</param>
    public BcsvSampler(BcsvReader reader)
    {
        _handle = NativeMethods.bcsv_sampler_create(reader.Handle);
        if (_handle == 0)
            throw new BcsvException("Failed to create sampler");
        // The native sampler references the reader throughout its life.
        // Holding the managed wrapper here keeps a finalizer-reclaimed reader
        // from destroying its handle under the sampler's feet (finalizer order).
        _reader = reader;
    }

    ~BcsvSampler() => Dispose(false);

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        // One-shot claim; see BcsvWriter.Dispose() for why.
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        var handle = _handle;
        _handle = 0;
        if (handle != 0)
            NativeMethods.bcsv_sampler_destroy(handle);
    }

    /// <summary>The reader this sampler was created over.</summary>
    public BcsvReader Reader => _reader;

    public void SetConditional(string expr)
    {
        if (!NativeMethods.bcsv_sampler_set_conditional(_handle, expr))
            NativeMethods.ThrowWithError("Conditional compile failed", NativeMethods.bcsv_sampler_error_msg(_handle));
    }

    public void SetSelection(string expr)
    {
        if (!NativeMethods.bcsv_sampler_set_selection(_handle, expr))
            NativeMethods.ThrowWithError("Selection compile failed", NativeMethods.bcsv_sampler_error_msg(_handle));
    }

    public SamplerMode Mode
    {
        get => NativeMethods.bcsv_sampler_get_mode(_handle);
        set => NativeMethods.bcsv_sampler_set_mode(_handle, value);
    }

    /// <summary>Advance to next matching row. Returns false when done.</summary>
    public bool Next()
    {
        bool ok = NativeMethods.bcsv_sampler_next(_handle);
        if (ok)
            _row = new BcsvRow(NativeMethods.bcsv_sampler_row(_handle));
        return ok;
    }

    /// <summary>Current output row (valid after Next() returns true).</summary>
    public BcsvRow Row => _row;

    public BcsvLayout OutputLayout
    {
        get
        {
            _outputLayout ??= new BcsvLayout(
                NativeMethods.bcsv_sampler_output_layout(_handle), ownsHandle: false);
            return _outputLayout;
        }
    }

    public long SourceRowPos => (long)NativeMethods.bcsv_sampler_source_row_pos(_handle);

    public IEnumerator<BcsvRow> GetEnumerator()
    {
        while (Next())
            yield return _row;
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
