# Changelog

All notable changes to the BCSV Unity package will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.5.0] - 2026-03-22

### Added
- `upm` branch with pre-built native binaries — Git URL installs now work without local builds
- Tarball (`.tgz`) from GitHub Releases includes pre-built natives for all platforms
- `.meta` files for all package assets (stable GUIDs)
- `upm-branch.yml` workflow auto-updates the `upm` branch after each build

### Fixed
- Replaced `UIntPtr.MaxValue` with .NET Standard 2.1 compatible `(UIntPtr)ulong.MaxValue`
- Updated `package.json` version to 1.5.0
- README updated: Git URL now points to `#upm` branch with pre-built natives

## [1.4.3] - 2026-03-21

### Added
- Initial UPM package structure (`package.json`, assembly definition, Samples~)
- GitHub Actions workflow for multi-platform native builds and `.tgz` packaging
- `FileFlags` enum: `NoFileIndex`, `StreamMode`, `BatchCompress`, `DeltaEncoding`

### Fixed
- Double-free bug in `BcsvRowBase.Layout` (now uses non-owning handle)
- README path references (`unity/plugin/` → `unity/Runtime/Scripts/`)
