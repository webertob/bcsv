// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
namespace Bcsv;

/// <summary>Exception thrown when a BCSV native operation fails.</summary>
public class BcsvException : Exception
{
    public BcsvException(string message) : base(message) { }
    public BcsvException(string message, Exception inner) : base(message, inner) { }
}
