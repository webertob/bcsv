#!/usr/bin/env bash
# install.sh — Build and install BCSV CLI tools + library system-wide.
#
# Usage:
#   scripts/install.sh [--prefix PATH] [--uninstall]
#
# Defaults:
#   --prefix /usr/local
#
# Works on Ubuntu and CachyOS (any FHS Linux). Requires sudo.
# Uninstall via:  scripts/install.sh --uninstall
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_DIR/build/ninja-release"
MANIFEST="$BUILD_DIR/install_manifest.txt"
PREFIX="/usr/local"
UNINSTALL=0

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)   PREFIX="$2"; shift 2 ;;
        --prefix=*) PREFIX="${1#*=}"; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Uninstall ────────────────────────────────────────────────────────────────
if [[ $UNINSTALL -eq 1 ]]; then
    if [[ ! -f "$MANIFEST" ]]; then
        echo "Error: install manifest not found at $MANIFEST"
        echo "Cannot uninstall — no record of a previous installation."
        exit 1
    fi
    echo "Removing installed files (from $MANIFEST)..."
    sudo xargs rm -f < "$MANIFEST"
    sudo ldconfig
    # Remove empty directories left behind
    sudo rmdir --ignore-fail-on-non-empty \
        "$PREFIX/lib/cmake/bcsv" \
        "$PREFIX/lib/cmake" 2>/dev/null || true
    echo "Done. BCSV uninstalled from $PREFIX"
    exit 0
fi

# ── Build ────────────────────────────────────────────────────────────────────
echo "=== BCSV system install ==="
echo "Prefix : $PREFIX"
echo "Build  : $BUILD_DIR"
echo ""

cd "$REPO_DIR"

echo "--- Configuring release build ---"
cmake --preset ninja-release -DCMAKE_INSTALL_PREFIX="$PREFIX"

echo ""
echo "--- Building ---"
cmake --build --preset ninja-release-build -j"$(nproc)"

# ── Install ──────────────────────────────────────────────────────────────────
echo ""
echo "--- Installing to $PREFIX (sudo required) ---"
sudo cmake --install "$BUILD_DIR" --prefix "$PREFIX"

# Register shared library with the dynamic linker
echo ""
echo "--- Updating ldconfig ---"
sudo ldconfig

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "=== Installation complete ==="
echo ""
echo "CLI tools installed to $PREFIX/bin:"
for tool in csv2bcsv bcsv2csv bcsvHead bcsvTail bcsvHeader \
            bcsvSampler bcsvGenerator bcsvValidate bcsvRepair; do
    if command -v "$tool" &>/dev/null; then
        echo "  $tool  $(command -v "$tool")"
    else
        echo "  $tool  (not found on PATH — is $PREFIX/bin in your PATH?)"
    fi
done

echo ""
echo "Shared library:"
ldconfig -p | grep -i libbcsv || echo "  (not found in ldconfig cache)"

echo ""
echo "Headers: $PREFIX/include/bcsv/"
echo ""
echo "To uninstall:  scripts/install.sh --uninstall"
