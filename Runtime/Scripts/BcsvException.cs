// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.

using System;

namespace BCSV
{
    /// <summary>Exception thrown when a BCSV native operation fails.</summary>
    public class BcsvException : Exception
    {
        public BcsvException(string message) : base(message) { }
        public BcsvException(string message, Exception inner) : base(message, inner) { }
    }
}
