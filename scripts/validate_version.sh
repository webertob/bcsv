#!/bin/bash
# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# validate_version.sh - Validate that every version stamp agrees with VERSION.txt
#
# Thin wrapper around scripts/check_versions.py, which is the single
# implementation used by the CI workflows too. This script used to compare git
# tags against include/bcsv/version_generated.h, but that file is gitignored and
# generated into the build tree, so it was absent in a clean checkout.
#
# Any arguments are forwarded, so the richer checks remain available:
#   scripts/validate_version.sh --tag v1.5.13
#   scripts/validate_version.sh --native build/libbcsv_c_api.so

set -e

cd "$(dirname "$0")/.."

if [ $# -eq 0 ] && git rev-parse --git-dir > /dev/null 2>&1; then
    # If HEAD sits exactly on a release tag, check the tag agrees as well.
    EXACT_TAG=$(git describe --tags --exact-match --match "v[0-9]*.[0-9]*.[0-9]*" 2>/dev/null || true)
    if [ -n "$EXACT_TAG" ]; then
        exec python3 scripts/check_versions.py --tag "$EXACT_TAG"
    fi
fi

exec python3 scripts/check_versions.py "$@"
