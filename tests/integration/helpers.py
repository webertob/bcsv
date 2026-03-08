"""Shared helper functions for BCSV integration tests."""
import csv
import math
import subprocess
from pathlib import Path
from typing import Optional


def run_tool(tool_path: Path, *args: str, check: bool = True,
             timeout: float = 30) -> subprocess.CompletedProcess:
    """Run a BCSV CLI tool and return the completed process."""
    return subprocess.run(
        [str(tool_path), *(str(a) for a in args)],
        capture_output=True, text=True, check=check, timeout=timeout,
    )


def csv_rows(path: Path) -> int:
    """Count data rows (excluding header) in a CSV file."""
    lines = path.read_text().strip().splitlines()
    return max(0, len(lines) - 1)


def csv_cell(path: Path, row: int, col: int) -> str:
    """Extract a cell value. row and col are 1-based; row 1 = first data row."""
    with open(path, newline='') as f:
        reader = csv.reader(f)
        for i, fields in enumerate(reader):
            if i == row:  # row 1 data = line index 1 (header is index 0)
                return fields[col - 1].strip()
    return ""


def csv_header_col1(path: Path) -> str:
    """Return the first column name from the header."""
    with open(path, newline='') as f:
        reader = csv.reader(f)
        header = next(reader)
        return header[0].strip()


def csv_normalize(path: Path) -> str:
    """Normalize CSV for semantic comparison.

    Strips quotes and removes trailing .0 from integer-like floats
    (e.g. 31.0 → 31), which is valid BCSV round-trip behaviour.
    """
    lines = []
    with open(path, newline='') as f:
        reader = csv.reader(f)
        for row in reader:
            normalized = []
            for cell in row:
                cell = cell.strip()
                # Remove trailing .0 on integer-like values
                if _is_integer_float(cell):
                    cell = cell.split('.')[0]
                normalized.append(cell)
            lines.append(','.join(normalized))
    return '\n'.join(lines)


def _is_integer_float(s: str) -> bool:
    """Check if string represents an integer-valued float like '31.0' or '-42.0'."""
    try:
        f = float(s)
        return f == int(f) and '.' in s
    except (ValueError, OverflowError):
        return False


def csv_equal(path_a: Path, path_b: Path) -> bool:
    """Compare two CSVs semantically (after normalization)."""
    return csv_normalize(path_a) == csv_normalize(path_b)


def float_eq(a, b, eps: float = 0.01) -> bool:
    """Float comparison with tolerance."""
    return math.isclose(float(a), float(b), abs_tol=eps)


def expect_fail(tool_path: Path, *args: str, timeout: float = 30) -> bool:
    """Return True if the tool exits with non-zero status."""
    result = run_tool(tool_path, *args, check=False, timeout=timeout)
    return result.returncode != 0


def line_count(path: Path) -> int:
    """Count total lines in a file."""
    return len(path.read_text().strip().splitlines())
