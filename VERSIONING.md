# BCSV Versioning System

## Overview

BCSV ships one version number across five distribution channels - the C++
headers, the C API natives, PyPI (`pybcsv`), NuGet (`Bcsv`) and the Unity
package - and stamps that number into every `.bcsv` file header. The system is
built so that all five agree, and so that a build which *cannot* determine the
correct version fails instead of guessing.

**`VERSION.txt` is the single source of truth.** Git tags do not supply the
version; they are only checked against it.

That ordering is deliberate. Until v1.5.13 the version was derived from
`git describe` with `VERSION.txt` as a silent fallback, so any environment where
git was unavailable produced a wrong-but-plausible version with no warning. This
is exactly how the v1.5.12 release shipped Linux natives stamped `1.5.11`: those
jobs build inside a container, git refused to read the workspace ("detected
dubious ownership"), and the build quietly fell through to a `VERSION.txt` that
release tagging never updated. Because `version::MINOR` also selects the file
codec (see `writer.hpp`), a guessed version is not merely a cosmetic provenance
error - it can change how data is encoded.

## How It Works

### Resolving the version

`cmake/GetGitVersion.cmake` reads `VERSION.txt` and then verifies it:

| Situation | Result |
|---|---|
| HEAD is tagged `vX.Y.Z` matching `VERSION.txt` | Version confirmed |
| HEAD is tagged `vX.Y.Z` **not** matching `VERSION.txt` | **Fatal error** |
| HEAD is untagged (development build) | `VERSION.txt` used, distance from last tag reported |
| Git unusable, `BCSV_STRICT_VERSION=OFF` | `VERSION.txt` used, reason reported |
| Git unusable, `BCSV_STRICT_VERSION=ON` | **Fatal error** |

`-DBCSV_STRICT_VERSION=ON` is set by every release and packaging workflow, so a
build that cannot verify itself never produces a shippable artifact. Leave it off
for local development from a tarball or a git-less checkout.

### Checking every stamp

`scripts/check_versions.py` is the single implementation used by developers and
CI alike. It verifies the committed manifests against `VERSION.txt`, optionally
cross-checks a git tag, and - most importantly - loads a **built** shared library
and asks it what version it reports:

```bash
scripts/check_versions.py                                 # committed manifests
scripts/check_versions.py --tag v1.5.13                   # and the tag
scripts/check_versions.py --native build/libbcsv_c_api.so # and the artifact
```

Checking the binary rather than the build inputs is the point: that is the check
which catches a toolchain that resolved a different version than intended. Every
packaging workflow runs it against each native before uploading it.

### Release Workflow

1. **Bump and commit** - `scripts/update_version.sh 1.5.13` writes `VERSION.txt`,
   `unity/package.json` and `Bcsv.csproj`, then verifies them. Commit this.
2. **Tag** - `git tag v1.5.13`. The tag must come *after* the bump commit;
   tagging first is what causes the mismatch this system now rejects.
3. **Push** - `git push origin master --tags`. The release workflows verify the
   tag against `VERSION.txt` and fail the release if they disagree.

Note that no workflow commits back to the repository. `release-publish.yml`
generates `version_generated.h` only inside its own workspace, for the
header-only include artifact.

### Distribution

Users downloading the repository get:
- ✅ Correct embedded version in headers
- ✅ No need for CMake or Git to build
- ✅ Works as header-only library
- ✅ Version accessible via `bcsv::getVersion()`

## File Structure

```
bcsv/
├── VERSION.txt                  # SINGLE SOURCE OF TRUTH
├── .github/workflows/
│   ├── release-publish.yml      # Tag-driven GitHub release + include artifact
│   ├── build-and-publish.yml    # PyPI wheels
│   ├── csharp-nuget.yml         # NuGet package
│   ├── unity-package.yml        # Unity .tgz package
│   └── upm-branch.yml           # Unity install-by-git-URL branch
├── cmake/
│   ├── GetGitVersion.cmake      # Reads VERSION.txt, verifies against git
│   └── version.h.in             # Template for version header
├── include/bcsv/
│   └── version_generated.h      # Generated into the BUILD tree (gitignored)
├── scripts/
│   ├── check_versions.py        # Verify every stamp (used by CI)
│   ├── update_version.sh        # Bump the release version
│   └── validate_version.sh      # Wrapper around check_versions.py
├── unity/package.json           # Mirrors VERSION.txt
├── csharp/src/Bcsv/Bcsv.csproj  # Mirrors VERSION.txt
└── CMakeLists.txt               # Uses VERSION_STRING from VERSION.txt
```

## Usage Examples

### For Developers

#### Check Current Version
```bash
# From repository root
bash scripts/validate_version.sh
```

#### Set the Release Version
```bash
# Writes VERSION.txt, unity/package.json and Bcsv.csproj, then verifies them
bash scripts/update_version.sh 1.5.13
```

#### Development Build
```bash
cmake -B build            # Reads VERSION.txt, verifies it against git tags
cmake --build build
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

### Option 1: Command Line (recommended)
```bash
bash scripts/update_version.sh 1.5.13
git commit -am "release: 1.5.13"
git tag v1.5.13
git push origin master --tags
```

Creating a tag from the GitHub web interface is **not** sufficient on its own:
the bump commit must exist first, or the release workflows will reject the tag.

### Option 2: With Release Notes
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
the same.  The single version comes from `VERSION.txt` and is stamped into every
`.bcsv` file header.

- **MAJOR**: Incompatible API *and* wire-format changes (breaking in both directions)
- **MINOR**: New functionality that **changes the wire format** — new codecs, new
  column types, new header sections, new feature-flag bits.  A reader built
  before the bump cannot open a file that uses one (Rule B below).
- **PATCH**: Everything else that is backward compatible — bug fixes, and
  **additive API surface that leaves the wire format untouched**: a new keyword
  argument, a new CLI flag, a new binding method, a new tool.

Because the version number is *also* the file-format version stamped into every
header, spending a MINOR on a language-binding addition would falsely signal a
format change to every reader.  That is why additive API lands as PATCH here and
not as MINOR, which a strict reading of SemVer alone would suggest.

**Lock step means version parity, not feature parity.** All five channels ship
the same number, because that number is also the file-format version. A release
may well add something to only one binding — a Python keyword argument, a C#
helper — and the other four still bump. What each binding actually exposes at a
given version is recorded in the feature matrix in
[docs/API_OVERVIEW.md](docs/API_OVERVIEW.md); keep it current in the same commit
as the feature, or it goes stale silently.

**The deciding question:** *does a reader that predates this change still open
every file the new code writes?*

| Answer | Bump |
|---|---|
| Yes | **PATCH** |
| No — new files need the new reader | **MINOR** |
| No in both directions | **MAJOR** |

### Examples

- `v1.5.0` → `1.5.0` (Unified version baseline)
- `v1.5.1` → `1.5.1` (Bug fix — wire format identical to 1.5.0)
- `v1.5.15` → `1.5.15` (New `null_policy` argument on `parquet_to_bcsv` — additive
  API, wire format identical to 1.5.14, so every 1.5.x reader opens its output)
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

All three rules are covered by `tests/version_gate_test.cpp`, which stamps
patched version bytes into real files.  **This gate is what makes a MINOR safe.**
A new header section or feature bit may move the packet stream precisely because
every older reader refuses the file outright rather than parsing the prefix it
recognises and then reading packets from the wrong offset.  Note the corollary
the tests also pin: `FileHeader::readFromBinary` does *not* validate `FileFlags`,
so an unknown feature bit alone is not a gate — a feature bit must ship with the
`version::MINOR` bump that gates it.

Two direct callers of `FileHeader::readFromBinary` sit outside `Reader` and so
outside this gate: `bcsvRepair` (`src/tools/bcsvRepair.cpp`), and anything using
`FileHeader::getBinarySize(layout)` to locate the first packet.  Both need
updating in the same change that adds a header section.

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
# Check every stamp against VERSION.txt
bash scripts/validate_version.sh
```

**"HEAD is tagged vX.Y.Z but VERSION.txt says A.B.C"** - the bump commit is
missing or the tag landed on the wrong commit. Either bump and re-tag:

```bash
bash scripts/update_version.sh X.Y.Z
git commit -am "release: X.Y.Z"
git tag -f vX.Y.Z
```

**"Cannot verify version ... against git tags"** with `BCSV_STRICT_VERSION=ON` -
git could not read the repository. In a container this is almost always
ownership:

```bash
git config --global --add safe.directory "$PWD"
```

A shallow clone hides tags; use `fetch-depth: 0` in CI or `git fetch --tags`.

Never edit `include/bcsv/version_generated.h` to resolve a mismatch - it is
gitignored and regenerated into the build tree on every configure.

### GitHub Actions Not Triggering
1. Check that tag follows format: `v*.*.*`
2. Ensure tag was pushed: `git push origin v1.0.4`
3. Check GitHub Actions tab for workflow runs
4. Verify repository has Actions enabled

### CMake Version Warnings
```bash
# If you see "VERSION keyword not followed by a value", VERSION.txt is
# missing or malformed - configure now fails outright with an explicit message.
cmake -B build   # Should print: Version X.Y.Z (VERSION.txt, ...)
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

## Verifying a Release Locally

Reproduce what CI checks before pushing a tag:

```bash
cmake -B build -DBCSV_STRICT_VERSION=ON
cmake --build build --target bcsv_c_api
scripts/check_versions.py --tag "v$(cat VERSION.txt)" \
    --native build/libbcsv_c_api.so
```

This is the same three-part check the packaging workflows run: the manifests
agree with `VERSION.txt`, the tag agrees with `VERSION.txt`, and the artifact
that will actually ship reports the version it claims.
