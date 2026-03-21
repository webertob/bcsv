# BCSV Automated Versioning System

## Overview

BCSV implements an automated versioning system designed to ensure that:
1. **Developers** always get the correct version during development builds
2. **End users** can download header files with embedded version information
3. **Git tags** and embedded versions stay synchronized automatically
4. **Header-only distribution** works without requiring CMake or Git

The system uses Git tags as the single source of truth for version numbers, ensuring consistency across:
- C++ header files (`include/bcsv/version_generated.h`)
- CMake builds and configuration
- GitHub releases and CI/CD workflows
- Python package on PyPI (`pybcsv`)

## How It Works

### 🔄 **Development Workflow**
During development, the version is automatically extracted from Git tags:
- `GetGitVersion.cmake` reads the latest git tag (e.g., `v1.0.3`)
- Updates `version_generated.h` with the extracted version
- Development builds show commits since last tag (e.g., `1.0.3-dev.18-dirty`)

### 🚀 **Release Workflow**
When a new version is released:
1. **Developer creates tag**: `git tag v1.0.4 && git push origin v1.0.4`
2. **GitHub Actions triggers**: The `.github/workflows/release.yml` workflow runs
3. **Version auto-update**: The workflow updates `version_generated.h` to match the tag
4. **Auto-commit**: Changes are committed back to the repository
5. **GitHub Release**: A release is automatically created

### 📦 **Distribution**
Users downloading the repository get:
- ✅ Correct embedded version in headers
- ✅ No need for CMake or Git to build
- ✅ Works as header-only library
- ✅ Version accessible via `bcsv::getVersion()`

## File Structure

```
bcsv/
├── .github/workflows/
│   └── release.yml              # GitHub Actions workflow for automated releases
├── cmake/
│   ├── GetGitVersion.cmake      # Git version extraction for dev builds
│   └── version.h.in             # Template for version header
├── include/bcsv/
│   └── version_generated.h      # Auto-generated version header (DO NOT EDIT)
├── scripts/
│   ├── update_version.sh        # Manual version update script
│   ├── validate_version.sh      # Version validation script
│   └── validate_version.bat     # Windows version validation
└── CMakeLists.txt               # Uses VERSION_STRING from git
```

## Usage Examples

### For Developers

#### Check Current Version
```bash
# From repository root
bash scripts/validate_version.sh
```

#### Update Version Manually
```bash
# Set specific version
bash scripts/update_version.sh 1.0.5

# Use latest git tag
bash scripts/update_version.sh
```

#### Development Build
```bash
mkdir build && cd build
cmake ..  # Automatically extracts version from git
cmake --build .
```

### For End Users

#### Header-Only Usage
```cpp
#include "bcsv/definitions.h"
#include <iostream>

int main() {
    std::cout << "Using BCSV version: " << bcsv::getVersion() << std::endl;
    std::cout << "Major: " << bcsv::VERSION_MAJOR << std::endl;
    std::cout << "Minor: " << bcsv::VERSION_MINOR << std::endl;
    std::cout << "Patch: " << bcsv::VERSION_PATCH << std::endl;
    return 0;
}
```

## Creating a New Release

### Option 1: GitHub Web Interface
1. Go to GitHub repository → Releases → "Create a new release"
2. Choose tag: `v1.0.4` (create new tag)
3. Set title: `Release v1.0.4`
4. Click "Publish release"
5. GitHub Actions will automatically update the version header

### Option 2: Command Line
```bash
# Create and push tag
git tag v1.0.4
git push origin v1.0.4

# GitHub Actions will trigger automatically
```

### Option 3: With Release Notes
```bash
# Create annotated tag with message
git tag -a v1.0.4 -m "Release version 1.0.4

- Added new feature X
- Fixed bug Y
- Improved performance Z"

git push origin v1.0.4
```

## Version Format

BCSV uses [Semantic Versioning](https://semver.org/) with a **unified version**:
since v1.5.0, the library version and the binary file format version are one and
the same.  The single version is derived from git tags and is stamped into every
`.bcsv` file header.

- **MAJOR**: Incompatible API *and* wire-format changes (breaking in both directions)
- **MINOR**: Backward-compatible new functionality (new codecs, new column types, etc.)
- **PATCH**: Backward-compatible bug fixes (wire format unchanged)

### Examples

- `v1.5.0` → `1.5.0` (Unified version baseline)
- `v1.5.1` → `1.5.1` (Bug fix — wire format identical to 1.5.0)
- `v1.6.0` → `1.6.0` (New codec or feature — can still read 1.5.x files)
- `v2.0.0` → `2.0.0` (Breaking — cannot read v1.x files, and vice versa)

### Development Versions

- `1.5.0-dev.5` (5 commits after v1.5.0)
- `1.5.0-dev.5-dirty` (5 commits + uncommitted changes)

## File Format Versioning

BCSV files embed `version_major.version_minor.version_patch` in the 24-byte
fixed header.  When reading, the library checks:

```
Rule A — Major must match exactly (breaking in both directions).
         Data written in 1.x cannot be read by 2.x, and vice versa.

Rule B — Minor is backward compatible only.
         BCSV 1.6.1 can read files written by 1.5.0.
         But 1.5.0 cannot read files written by 1.6.1.

Rule C — Patch is compatible in both directions.
         Wire format must not change within a minor version.
         BCSV 1.6.5 can read files from 1.6.2 and 1.6.7.
```

Implemented in `Reader::readFileHeader()`:
```cpp
if (file_header.versionMajor() != version::MAJOR ||     // Rule A
    file_header.versionMinor() > version::MINOR)         // Rule B
    → reject                                             // Rule C: patch not checked
```

## Codec Registry

Backward compatibility for minor versions is achieved through **version-gated
codec selection**.  When a new minor version introduces a new row or file codec,
the old codec is kept alongside the new one.  The library uses the file header's
minor version to select the correct codec:

```
resolveRowCodecId(fileMinor, flags)   →  RowCodecId
resolveFileCodecId(fileMinor, compressionLevel, flags)  →  FileCodecId
```

### Version → Codec Mapping

| Minor Version | Row Codecs Available | File Codecs Available |
|---|---|---|
| 0–4 | FLAT001, ZOH001, DELTA002 | STREAM_001, STREAM_LZ4_001, PACKET_001, PACKET_LZ4_001, PACKET_LZ4_BATCH_001 |

*This table grows as new codecs are added in future minor versions.*

### Codec Lifecycle

- **Added**: At a minor version bump (e.g., v1.7 adds DELTA003)
- **Retained**: Old codecs live alongside new ones for backward compatibility
- **Removed**: Only on a **major** version bump (e.g., v2.0 may drop FLAT001)

### Adding a New Codec — Checklist

See the `HOW TO ADD A NEW CODEC` recipe comments in `definitions.h` (above
`resolveRowCodecId()` and `resolveFileCodecId()`) and the detailed recipe in
`SKILLS.md`.  A `static_assert` guardrail in `definitions.h` will **break the
build** if a new enum value is added without updating the registry.

## Troubleshooting

### Version Mismatch Errors
```bash
# Check current state
bash scripts/validate_version.sh

# Fix by updating to match git tag
bash scripts/update_version.sh

# Or create matching git tag
git tag v$(grep "STRING" include/bcsv/version_generated.h | cut -d'"' -f2)
```

### GitHub Actions Not Triggering
1. Check that tag follows format: `v*.*.*`
2. Ensure tag was pushed: `git push origin v1.0.4`
3. Check GitHub Actions tab for workflow runs
4. Verify repository has Actions enabled

### CMake Version Warnings
```bash
# If you see "VERSION keyword not followed by a value"
cd build
cmake ..  # Should show extracted git version
```

### C++ shows wrong version
1. Ensure you have a Git tag: `git describe --tags`
2. Reconfigure CMake: `cd build && cmake ..`
3. Check generated file: `include/bcsv/version_generated.h`

### Manual Override
If automatic system fails, manually update version:
```bash
# Edit include/bcsv/version_generated.h
# Then commit changes
git add include/bcsv/version_generated.h
git commit -m "Manual version update to 1.0.4"
```

## Benefits of This System

### ✅ **For Developers**
- Automatic version management during development
- No manual version updates needed
- Clear development vs release version distinction
- Validation tools prevent version drift

### ✅ **For Users**
- Header-only distribution with correct versions
- No dependency on Git or CMake for basic usage
- Consistent version information across all builds
- Easy version checking in code

### ✅ **For Maintainers**
- Automated release process
- Reduced human error in version management
- Clear audit trail of version changes
- Consistent GitHub releases

## Migration from Old System

If upgrading from a manual versioning system:

1. **Backup current version**:
   ```bash
   cp include/bcsv/version_generated.h include/bcsv/version_generated.h.backup
   ```

2. **Run validation**:
   ```bash
   bash scripts/validate_version.sh
   ```

3. **Fix any mismatches**:
   ```bash
   # Either update header to match git tag
   bash scripts/update_version.sh
   
   # Or create git tag to match header
   git tag v$(grep "STRING" include/bcsv/version_generated.h | cut -d'"' -f2)
   ```

4. **Test the system**:
   ```bash
   mkdir test_build && cd test_build
   cmake ..
   cmake --build .
   ```

The automated versioning system ensures BCSV can be easily integrated into any project while maintaining clear version tracking and compatibility checking.
