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

BCSV uses [Semantic Versioning](https://semver.org/):
- **MAJOR**: Incompatible API changes
- **MINOR**: Backward-compatible functionality
- **PATCH**: Backward-compatible bug fixes

### Examples

- `v1.0.0` → `1.0.0` (Initial release)
- `v1.0.1` → `1.0.1` (Bug fix)
- `v1.1.0` → `1.1.0` (New features)
- `v2.0.0` → `2.0.0` (Breaking changes)

### Development Versions

- `1.0.3-dev.5` (5 commits after v1.0.3)
- `1.0.3-dev.5-dirty` (5 commits + uncommitted changes)

## File Format Versioning

BCSV files embed version information for compatibility checking:

```cpp
// When writing files
BcsvWriter writer(layout);
// Version is automatically embedded: VERSION_MAJOR.VERSION_MINOR

// When reading files  
BcsvReader reader;
// Automatically validates file version against library version
```

### Compatibility Rules:
- **Major version mismatch**: File cannot be read (breaking changes)
- **File has older minor version** (file < library): File can be read — the reader understands all older format features (backward compatibility)
- **File has newer minor version** (file > library): File is **rejected** — the reader cannot safely interpret features from a newer format version

> **Note:** Within v1.x, each minor version introduced breaking wire-format changes
> (v1.2: xxHash64 checksums, v1.3: streaming LZ4 + new PacketHeader, v1.4: Delta002 + VLE + footer index).
> True forward compatibility is planned for v2.0.0. See [ARCHITECTURE.md](ARCHITECTURE.md) for the roadmap.

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
