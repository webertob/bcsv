#!/bin/bash
# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# validate_version.sh - Validate that git tags match embedded version

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_error() { echo -e "${RED}✗ $1${NC}"; }
print_success() { echo -e "${GREEN}✓ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠ $1${NC}"; }
print_info() { echo -e "$1"; }

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "Not in a git repository"
    exit 1
fi

# Get the latest git tag
GIT_TAG=$(git describe --tags --match "v*" --abbrev=0 2>/dev/null || echo "")

if [ -z "$GIT_TAG" ]; then
    print_warning "No version tags found in git"
    print_info "Current embedded version will be used as reference"
else
    print_info "Latest git tag: $GIT_TAG"
fi

# Check if version_generated.h exists
VERSION_FILE="include/bcsv/version_generated.h"
if [ ! -f "$VERSION_FILE" ]; then
    print_error "Version header not found: $VERSION_FILE"
    exit 1
fi

# Extract version from header file
EMBEDDED_MAJOR=$(grep "constexpr int MAJOR" "$VERSION_FILE" | sed 's/.*= *\([0-9]*\).*/\1/')
EMBEDDED_MINOR=$(grep "constexpr int MINOR" "$VERSION_FILE" | sed 's/.*= *\([0-9]*\).*/\1/')
EMBEDDED_PATCH=$(grep "constexpr int PATCH" "$VERSION_FILE" | sed 's/.*= *\([0-9]*\).*/\1/')
EMBEDDED_VERSION="$EMBEDDED_MAJOR.$EMBEDDED_MINOR.$EMBEDDED_PATCH"

print_info "Embedded version: $EMBEDDED_VERSION"

# If we have a git tag, validate it matches
if [ -n "$GIT_TAG" ]; then
    # Extract version from git tag (remove 'v' prefix)
    GIT_VERSION=${GIT_TAG#v}
    
    if [ "$EMBEDDED_VERSION" = "$GIT_VERSION" ]; then
        print_success "Version consistency check passed"
        print_info "Git tag $GIT_TAG matches embedded version $EMBEDDED_VERSION"
    else
        print_error "Version mismatch!"
        print_info "Git tag version: $GIT_VERSION"
        print_info "Embedded version: $EMBEDDED_VERSION"
        print_info ""
        print_info "To fix this, either:"
        print_info "1. Create a new git tag: git tag v$EMBEDDED_VERSION"
        print_info "2. Or update the embedded version to match: $GIT_VERSION"
        exit 1
    fi
else
    print_success "Version validation completed (no git tags to compare)"
fi

# Check if we have uncommitted changes to version file
if git diff --quiet "$VERSION_FILE" 2>/dev/null; then
    print_success "Version header is committed"
else
    print_warning "Version header has uncommitted changes"
fi

# Show additional info
if git describe --tags --always --dirty > /dev/null 2>&1; then
    GIT_DESCRIBE=$(git describe --tags --always --dirty)
    print_info "Git describe: $GIT_DESCRIBE"
fi

print_success "Version validation completed successfully!"
