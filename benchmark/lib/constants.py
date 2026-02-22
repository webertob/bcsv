"""Canonical constants for the BCSV benchmark suite.

Every script imports from here — no more divergent copies of MODE_ALIASES,
MICRO_GROUPS, TYPE_ROWS, etc.
"""

from __future__ import annotations

import re
from collections import OrderedDict

# ---------------------------------------------------------------------------
# Macro sizing
# ---------------------------------------------------------------------------
TYPE_ROWS: dict[str, int] = {
    "MACRO-SMALL": 10_000,
    "MACRO-LARGE": 500_000,
}

MACRO_FILE_STEMS: dict[str, str] = {
    "MACRO-SMALL": "macro_small",
    "MACRO-LARGE": "macro_large",
}

LANGUAGE_SIZE_BY_TYPE: dict[str, str] = {
    "MACRO-SMALL": "S",
    "MACRO-LARGE": "L",
}

# ---------------------------------------------------------------------------
# Macro mode aliases (canonical display name → raw JSON mode strings)
#
# Used by report generation, operator summary, and interleaved comparison.
# ---------------------------------------------------------------------------
MODE_ALIASES: OrderedDict[str, list[str]] = OrderedDict([
    ("CSV", ["CSV"]),
    ("BCSV Flexible Dense",
     ["BCSV Flexible", "BCSV Flat", "BCSV Standard", "BCSV Flexible Flat"]),
    ("BCSV Flexible ZoH",
     ["BCSV Flexible ZoH", "BCSV ZoH", "BCSV Flexible-ZoH"]),
    ("BCSV Static Dense",
     ["BCSV Static", "BCSV Static Flat", "BCSV Static Standard"]),
    ("BCSV Static ZoH",
     ["BCSV Static ZoH", "BCSV Static-ZoH"]),
])

# ---------------------------------------------------------------------------
# Micro benchmark group classification
# ---------------------------------------------------------------------------
MICRO_GROUPS: dict[str, str] = {
    # prefix → group label
    "BM_Get_":          "Get",
    "BM_Set_":          "Set",
    "BM_Visit":         "Visit",
    "BM_RowViewVisit":  "Visit",
    "BM_Serialize":     "Serialize",
    "BM_Deserialize":   "Serialize",
    "BM_RowClear":      "Lifecycle",
    "BM_RowConstruct":  "Lifecycle",
    "BM_RowViewCopy":   "Lifecycle",
    "BM_RowViewMove":   "Lifecycle",
    "BM_CsvWrite":      "Other",
}


def classify_micro(name: str) -> str:
    """Return the group label for a micro benchmark by name prefix."""
    for prefix, group in MICRO_GROUPS.items():
        if name.startswith(prefix):
            return group
    return "Other"


# ---------------------------------------------------------------------------
# Mode matching helpers
# ---------------------------------------------------------------------------

_TRK_SUFFIX = re.compile(r"\s*\[trk=[^\]]*\]$")


def mode_base(raw_mode: str) -> str:
    """Strip tracking suffix from a mode string.

    ``"BCSV Flexible [trk=off]"`` → ``"BCSV Flexible"``
    ``"CSV"``                     → ``"CSV"``
    """
    return _TRK_SUFFIX.sub("", raw_mode)


def mode_matches(raw_mode: str, aliases: list[str]) -> bool:
    """Return *True* if *raw_mode* (possibly with tracking suffix) matches any alias."""
    base = mode_base(raw_mode)
    return base in aliases


def pick_mode_rows(rows: list[dict], aliases: list[str]) -> list[dict]:
    """Filter rows whose ``mode`` field matches any of *aliases* (suffix-tolerant)."""
    return [r for r in rows if mode_matches(str(r.get("mode", "")), aliases)]


# ---------------------------------------------------------------------------
# Regression threshold
# ---------------------------------------------------------------------------
DEFAULT_REGRESSION_THRESHOLD_PCT = 5.0
