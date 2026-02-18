# Binding Consistency

This document is the single maintenance guide for keeping the C API and language bindings aligned.

## Canonical Ownership

- Root C API source of truth:
  - `include/bcsv/bcsv_c_api.h`
  - `include/bcsv/bcsv_c_api.cpp`
- Python vendored mirror (must stay byte-identical to root C API files above):
  - `python/include/bcsv/bcsv_c_api.h`
  - `python/include/bcsv/bcsv_c_api.cpp`
- Unity C# interop layer (must match exported C symbols):
  - `unity/Scripts/BcsvNative.cs`
  - `unity/Scripts/BcsvRow.cs`

## Required Update Flow

When C API changes are made:

1. Update root C API files in `include/bcsv/`.
2. Resync Python vendored files:
   - `cd python && python sync_headers.py --force --verbose`
3. Update Unity P/Invoke signatures/wrappers if the exported C surface changed.
4. Update API docs if behavior/symbols changed:
   - `docs/API_OVERVIEW.md`
   - `python/README.md`
   - `unity/README.md`

## CI Guard

CI enforces root/Python C API parity via:

- Workflow: `.github/workflows/api-consistency.yml`
- Script: `scripts/check_python_c_api_sync.py`

The check fails if either Python-vendored C API file drifts from the root equivalent.

## Local Validation

Run locally before pushing:

```bash
python3 scripts/check_python_c_api_sync.py
```
