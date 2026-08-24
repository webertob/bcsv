#!/bin/bash
# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# update_version.sh - Set the release version across every packaging manifest.
#
# VERSION.txt is the single source of truth (see cmake/GetGitVersion.cmake).
# This script writes it and the committed manifests that mirror it, so the bump
# lands in one commit before the release tag is created. Order matters: tagging
# first and bumping later is what shipped v1.5.12 with 1.5.11 natives.
#
# It deliberately does NOT touch include/bcsv/version_generated.h - that file is
# gitignored and generated into the build tree by CMake. Writing a copy into the
# source tree only creates a stale shadow of the real one.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_error() { echo -e "${RED}✗ $1${NC}"; }
print_success() { echo -e "${GREEN}✓ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠ $1${NC}"; }
print_info() { echo -e "${BLUE}ℹ $1${NC}"; }

show_usage() {
    echo "Usage: $0 VERSION"
    echo ""
    echo "Set the BCSV release version across all packaging manifests:"
    echo "  VERSION.txt                      (single source of truth)"
    echo "  unity/package.json               (Unity Package Manager)"
    echo "  csharp/src/Bcsv/Bcsv.csproj      (NuGet)"
    echo "  python/VERSION.txt               (generated; refreshed only if present)"
    echo ""
    echo "Arguments:"
    echo "  VERSION    Version to set, X.Y.Z (e.g. 1.5.13)"
    echo ""
    echo "Example:"
    echo "  $0 1.5.13"
    echo "  git commit -am 'release: 1.5.13' && git tag v1.5.13"
    echo ""
}

validate_version() {
    local version=$1
    if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        print_error "Invalid version format: $version"
        print_info "Expected format: X.Y.Z (e.g., 1.5.13)"
        return 1
    fi
    return 0
}

main() {
    if [ ! -f "VERSION.txt" ]; then
        print_error "VERSION.txt not found. Run this script from the BCSV root directory."
        exit 1
    fi

    if [ $# -ne 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        [ $# -eq 1 ] || print_error "Exactly one argument is required"
        show_usage
        [ $# -eq 1 ] && exit 0
        exit 1
    fi

    local target_version=$1
    validate_version "$target_version" || exit 1

    local previous_version
    previous_version=$(tr -d '[:space:]' < VERSION.txt)
    print_info "Bumping $previous_version -> $target_version"

    echo "$target_version" > VERSION.txt
    print_success "VERSION.txt -> $target_version"

    python3 - "$target_version" <<'PY'
import json
import pathlib
import re
import sys

version = sys.argv[1]

manifest = pathlib.Path("unity/package.json")
package = json.loads(manifest.read_text())
package["version"] = version
manifest.write_text(json.dumps(package, indent=2) + "\n")
print(f"  unity/package.json -> {version}")

csproj = pathlib.Path("csharp/src/Bcsv/Bcsv.csproj")
text = csproj.read_text()
if not re.search(r"<Version>[^<]+</Version>", text):
    sys.exit("csharp/src/Bcsv/Bcsv.csproj has no <Version> element")
csproj.write_text(re.sub(r"<Version>[^<]+</Version>", f"<Version>{version}</Version>", text, count=1))
print(f"  csharp/src/Bcsv/Bcsv.csproj -> {version}")
PY
    print_success "Packaging manifests updated"

    # python/VERSION.txt is generated (gitignored) and only consumed by sdist
    # builds, but a stale copy in the working tree would make check_versions.py
    # fail below.  Refresh it if it exists; never create one that did not.
    if [ -f "python/VERSION.txt" ]; then
        echo "$target_version" > python/VERSION.txt
        print_info "  python/VERSION.txt (generated) -> $target_version"
    fi

    python3 scripts/check_versions.py

    echo ""
    print_success "Version set to $target_version"
    print_info "Next steps - commit before tagging, the release workflows verify the order:"
    echo "    git commit -am \"release: $target_version\""
    echo "    git tag v$target_version"
    echo "    git push origin master --tags"
    print_warning "Remember to add a CHANGELOG.md entry (and unity/CHANGELOG.md if the package changed)."
}

main "$@"
