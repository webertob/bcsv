# Python Package Build Notes

## Source File Management

This Python package uses an automated header synchronization system to avoid source code duplication.

### Key Concepts

- **Source Files**: The canonical BCSV source files are in the main project's `../include/` directory
- **Synced Files**: The `include/` directory in this Python package contains automatically synchronized copies
- **Git Tracking**: The `include/` directory is **not tracked in git** since it contains temporary build artifacts

### Build Process

1. **Automatic Sync**: During package building, `sync_headers.py` automatically copies the latest headers from `../include/`
2. **Distribution**: The synced files are included in source distributions via `MANIFEST.in`
3. **Clean State**: After building, the `include/` directory can be safely deleted - it will be recreated as needed

### Commands

```bash
# Manual header sync (for development)
python sync_headers.py --verbose

# Force update all headers
python sync_headers.py --force --verbose

# Build package (automatically syncs headers)
python -m build

# Clean temporary files
rm -rf include/ build/ dist/ *.egg-info/
```

### Directory Structure

```text
python/
├── include/           # ← Auto-generated, not tracked in git
│   ├── bcsv/         # ← Synced from ../include/bcsv/
│   ├── lz4-1.10.0/   # ← Synced from ../include/lz4-1.10.0/
│   └── boost-1.89.0/ # ← Synced from ../include/boost-1.89.0/
├── pybcsv/           # ← Python package source
├── sync_headers.py   # ← Header synchronization script
└── setup.py          # ← Build configuration
```

### Benefits

- ✅ **No duplication**: Headers are always current from main project
- ✅ **Clean repository**: No redundant source files tracked in git
- ✅ **Automatic**: No manual copying or maintenance required
- ✅ **Distribution ready**: Source packages include all necessary files