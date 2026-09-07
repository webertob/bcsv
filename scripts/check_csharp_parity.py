#!/usr/bin/env python3
"""
Check parity between the NuGet and Unity C# bindings.

The two trees are the same code twice. Fourteen files under
csharp/src/Bcsv/ and unity/Runtime/Scripts/ share a name and, modulo the
namespace form, a body. They cannot be merged into one physical source:
the NuGet package publishes `namespace Bcsv;` (file-scoped) and Unity's
BCSV.asmdef publishes `namespace BCSV { }` (block-scoped), so unifying them
would break every consumer of the published package. What can be done is
notice when they drift, which is what this script is for — edit one side,
forget the other, and CI says so instead of a binding shipping a behaviour
the other one does not have.

Two checks:

  1. P/Invoke parity — the set of bcsv_* entry points declared in
     NativeMethods.cs vs BcsvNative.cs.
  2. Body parity — the shared wrapper files, compared after normalising
     away the namespace form, indentation, blank lines and using-directives.

Usage:
    python scripts/check_csharp_parity.py            # 0 if in sync
    python scripts/check_csharp_parity.py --verbose
    python scripts/check_csharp_parity.py --diff     # show drifting lines
"""

import argparse
from collections import Counter
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

NUGET_DIR = REPO_ROOT / "csharp" / "src" / "Bcsv"
UNITY_DIR = REPO_ROOT / "unity" / "Runtime" / "Scripts"

NUGET_PINVOKE = NUGET_DIR / "NativeMethods.cs"
UNITY_PINVOKE = UNITY_DIR / "BcsvNative.cs"

# Matches P/Invoke declarations like:  static extern ... bcsv_foo_bar(
FUNC_RE = re.compile(r"\bbcsv_\w+(?=\s*\()")

# Functions intentionally excluded from Unity (e.g. row visitor — out of scope)
EXCLUDED_FUNCS = {
    "bcsv_row_visit_const",
}

# Files that exist on one side only, by design.
#   NuGet-only: ColumnType/FileFlags/SamplerMode live inside other files on the
#               Unity side; NativeMethods.cs is checked by the P/Invoke pass.
#   Unity-only: BcsvNative.cs (P/Invoke pass) plus the MonoBehaviour components,
#               which depend on UnityEngine and have no NuGet counterpart.
UNPAIRED_OK = {
    # Unity-only by design: the pacer is a marker interface for a MonoBehaviour that
    # sits beside the BcsvRecorder component; NuGet has no recorder component.
    "IBcsvPacer.cs",
    "NativeMethods.cs", "ColumnType.cs", "FileFlags.cs", "SamplerMode.cs",
    "BcsvNative.cs", "BcsvPlayer.cs", "BcsvRecorder.cs",
}

USING_RE = re.compile(r"^\s*using\s+[\w.]+\s*;\s*$")
NS_RE = re.compile(r"^\s*namespace\s+[\w.]+\s*[;{]?\s*$")


def strip_comments(src: str) -> str:
    """Remove //, /* */ and /// comments without touching string literals."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            # String literal, possibly verbatim (@"...") — copy it whole.
            verbatim = i > 0 and src[i - 1] == "@"
            out.append(c)
            i += 1
            while i < n:
                if verbatim:
                    if src[i] == '"':
                        if i + 1 < n and src[i + 1] == '"':
                            out.append(src[i:i + 2]); i += 2; continue
                        out.append(src[i]); i += 1; break
                else:
                    if src[i] == "\\" and i + 1 < n:
                        out.append(src[i:i + 2]); i += 2; continue
                    if src[i] == '"':
                        out.append(src[i]); i += 1; break
                out.append(src[i]); i += 1
            continue
        if c == "'":
            out.append(c); i += 1
            while i < n:
                if src[i] == "\\" and i + 1 < n:
                    out.append(src[i:i + 2]); i += 2; continue
                out.append(src[i]); i += 1
                if src[i - 1] == "'":
                    break
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(c); i += 1
    return "".join(out)


# Files whose two copies are deliberately different implementations. Exempt as a
# whole, with the reason, rather than statement by statement.
WHOLE_FILE_EXEMPT = {
    "ColumnDefinition.cs":
        "NuGet uses a positional `record struct`; Unity hand-writes the struct "
        "because its C# level has no record support.",
}

# Statements allowed to appear on one side only, with why. Anything NOT listed
# here that appears on one side only is drift and fails the check. Keep the
# reasons current: when one of these is resolved, delete the entry.
KNOWN_DIVERGENCE = {
    "BcsvLayout.cs": {
        # OPEN — see ToDo.md. NativeMethods.cs already declares both entry
        # points, so the NuGet wrapper is a half-finished port, not a
        # deliberate omission.
        "public int RowDataSize => (int)NativeMethods.bcsv_layout_row_data_size(Handle);",
        "public int ColumnCountByType(ColumnType type) => (int)NativeMethods.bcsv_layout_column_count_by_type(Handle, type);",
        # Unity-only convenience aliases; the NuGet names are the canonical ones.
        "public ColumnType GetColumnType(int index) => ColumnType(index);",
        "public int GetColumnIndex(string name) => ColumnIndex(name);",
        "public string GetColumnName(int index) => ColumnName(index);",
    },
    "BcsvWriter.cs": {
        # OPEN — see ToDo.md. Unity throws a clear ArgumentNullException; the
        # NuGet copy passes null through to the native call.
        "if (layout == null) throw new ArgumentNullException(nameof(layout));",
        # Equivalent: Unity extracted the marshalling into BcsvNative's
        # FilenameHelper, NuGet inlines it. Same native call either way.
        "public string Filename => FilenameHelper.GetWriterFilename(_handle);",
        "public string Filename {", "get {",
        "var ptr = NativeMethods.bcsv_writer_filename(_handle);",
        "return ptr == IntPtr.Zero ? null : NativeMethods.PtrToStringAuto(ptr);",
        "}",
    },
    "BcsvReader.cs": {
        # Equivalent, as above.
        "public string Filename => FilenameHelper.GetReaderFilename(_handle);",
        "public string Filename {", "get {",
        "var ptr = NativeMethods.bcsv_reader_filename(_handle);",
        "return ptr == IntPtr.Zero ? null : NativeMethods.PtrToStringAuto(ptr);",
        "}",
    },
    "BcsvMetadata.cs": {
        # IReadOnlyDictionary<,> is not in the denullable list because the
        # generic argument list contains a comma; the annotation is the only
        # difference on these two signatures.
        "public static IReadOnlyDictionary<string, string>? ReadCompanion( string bcsvPath, long expectedRows = -1) => ReadCompanion(bcsvPath, expectedRows, verifyDigest: true);",
        "public static IReadOnlyDictionary<string, string> ReadCompanion( string bcsvPath, long expectedRows = -1) => ReadCompanion(bcsvPath, expectedRows, verifyDigest: true);",
        "public static IReadOnlyDictionary<string, string>? ReadCompanion( string bcsvPath, long expectedRows, bool verifyDigest) {",
        "public static IReadOnlyDictionary<string, string> ReadCompanion( string bcsvPath, long expectedRows, bool verifyDigest) {",
    },
}


# Differences forced by the Unity runtime rather than by drift. Unity compiles
# with nullable reference types off, so its copy carries no "?" annotations on
# reference types, and its runtime lacks the span-based TryParse overloads.
# Normalising these keeps the gate on behaviour. Value-type nullables (long?,
# int?) are NOT stripped — those are real and present on both sides.
NULLABLE_REF_RE = re.compile(
    r"\b(object|string|BcsvLayout|BcsvRow|BcsvReader|BcsvWriter|ColumnData"
    r"|Dictionary<[^<>]*>|List<[^<>]*>|KeyValuePair<[^<>]*>)\?"
)


def denullable(text: str) -> str:
    prev = None
    while prev != text:                      # inner generics first, then outer
        prev = text
        text = NULLABLE_REF_RE.sub(r"\1", text)
    return text


def normalize(path: Path) -> list[str]:
    """Reduce a binding source to the statements that must match its twin.

    Compares code, not layout. Dropped: comments (including doc comments),
    using-directives (the NuGet project has ImplicitUsings, Unity does not),
    the namespace declaration in either form and the brace pair that a
    block-scoped one adds, and all whitespace and line-wrapping choices.

    What survives is the statement sequence, so reformatting one side does
    not trip the gate but changing what it does will.
    """
    src = strip_comments(path.read_text(encoding="utf-8"))

    kept, block_scoped, pending_brace = [], False, False
    for line in src.splitlines():
        if USING_RE.match(line):
            continue
        if NS_RE.match(line):
            stripped = line.rstrip()
            if stripped.endswith(";"):
                continue                      # file-scoped: adds no brace
            block_scoped = True               # block-scoped: adds a brace pair
            pending_brace = not stripped.endswith("{")
            continue
        if pending_brace and line.strip() == "{":
            pending_brace = False
            continue
        kept.append(line)

    # Re-split on statement and block boundaries so wrapping is irrelevant.
    text = " ".join(kept)
    text = re.sub(r"\s+", " ", text)
    text = denullable(text)
    text = text.replace(".AsSpan(", ".Substring(")
    stmts = [s.strip() for s in re.split(r"(?<=[;{}])", text) if s.strip()]

    # The brace closing a block-scoped namespace.
    if block_scoped and stmts and stmts[-1] == "}":
        stmts.pop()
    return stmts


def check_pinvoke(verbose: bool) -> list[str]:
    for f in (NUGET_PINVOKE, UNITY_PINVOKE):
        if not f.exists():
            return [f"file not found: {f.relative_to(REPO_ROOT)}"]

    nuget = set(FUNC_RE.findall(NUGET_PINVOKE.read_text(encoding="utf-8"))) - EXCLUDED_FUNCS
    unity = set(FUNC_RE.findall(UNITY_PINVOKE.read_text(encoding="utf-8"))) - EXCLUDED_FUNCS

    only_nuget = sorted(nuget - unity)
    only_unity = sorted(unity - nuget)

    if verbose:
        print(f"P/Invoke: {len(nuget)} NuGet, {len(unity)} Unity, "
              f"{len(nuget & unity)} common")

    problems = []
    if only_nuget:
        problems.append(f"{len(only_nuget)} P/Invoke entry points only in NuGet: "
                        + ", ".join(only_nuget[:5]))
    if only_unity:
        problems.append(f"{len(only_unity)} P/Invoke entry points only in Unity: "
                        + ", ".join(only_unity[:5]))
    return problems


def check_bodies(verbose: bool, show_diff: bool) -> list[str]:
    nuget_files = {p.name for p in NUGET_DIR.glob("*.cs")}
    unity_files = {p.name for p in UNITY_DIR.glob("*.cs")}

    problems = []

    for name in sorted((nuget_files ^ unity_files) - UNPAIRED_OK):
        side = "NuGet" if name in nuget_files else "Unity"
        problems.append(f"{name} exists only on the {side} side — add the twin, "
                        f"or list it in UNPAIRED_OK with a reason")

    shared = sorted((nuget_files & unity_files) - UNPAIRED_OK)
    for name in shared:
        if name in WHOLE_FILE_EXEMPT:
            if verbose:
                print(f"  exempt {name} — {WHOLE_FILE_EXEMPT[name]}")
            continue

        # Member order is not behaviour in C#, and the two copies have drifted
        # in ordering more than once; compare as multisets.
        ca = Counter(normalize(NUGET_DIR / name))
        cb = Counter(normalize(UNITY_DIR / name))
        allowed = KNOWN_DIVERGENCE.get(name, set())
        only_nuget = [s for s in (ca - cb).elements() if s not in allowed]
        only_unity = [s for s in (cb - ca).elements() if s not in allowed]

        if not only_nuget and not only_unity:
            if verbose:
                print(f"  ok     {name} ({sum(ca.values())} statements)")
            continue

        problems.append(f"{name}: {len(only_nuget)} statement(s) only in NuGet, "
                        f"{len(only_unity)} only in Unity")
        if show_diff:
            print(f"\n--- csharp/src/Bcsv/{name}\n+++ unity/Runtime/Scripts/{name}")
            for s in sorted(only_nuget):
                print(f"-{s}")
            for s in sorted(only_unity):
                print(f"+{s}")

    if verbose:
        print(f"Bodies: {len(shared)} shared files, "
              f"{len(WHOLE_FILE_EXEMPT)} exempt")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="Check NuGet/Unity C# binding parity")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--diff", action="store_true",
                        help="print the drifting lines for each mismatched file")
    args = parser.parse_args()

    problems = check_pinvoke(args.verbose) + check_bodies(args.verbose, args.diff)

    if problems:
        print("\nC# binding drift detected:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        print("\nThe NuGet and Unity bindings are the same code in two namespaces. "
              "Apply the change to both, then re-run.", file=sys.stderr)
        return 1

    print("C# binding parity OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
