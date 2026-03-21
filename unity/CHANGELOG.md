# Changelog

All notable changes to the BCSV Unity package will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.4.3] - 2026-03-21

### Added
- Initial UPM package structure (`package.json`, assembly definition, Samples~)
- GitHub Actions workflow for multi-platform native builds and `.tgz` packaging
- `FileFlags` enum: `NoFileIndex`, `StreamMode`, `BatchCompress`, `DeltaEncoding`

### Fixed
- Double-free bug in `BcsvRowBase.Layout` (now uses non-owning handle)
- README path references (`unity/plugin/` → `unity/Runtime/Scripts/`)
