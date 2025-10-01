# Version Management

The BCSV project uses Git tags as the single source of truth for version numbers. This ensures consistency across:
- Python package on PyPI (`pybcsv`)
- C++ header files (`include/bcsv/definitions.h`)
- GitHub releases

## How It Works

### Git Tags
Version numbers are defined by Git tags following semantic versioning:
```bash
v1.0.0, v1.0.1, v1.2.0, etc.
```

The tag **must** start with `v` followed by `MAJOR.MINOR.PATCH` format.

### Python Package (`pybcsv`)
The Python package uses [setuptools-scm](https://github.com/pypa/setuptools-scm) to automatically extract the version from Git tags during build:

- **Configuration**: `python/pyproject.toml` contains setuptools-scm configuration
- **Generated file**: `python/pybcsv/_version.py` is auto-generated during build
- **Import**: `pybcsv.__version__` imports from the generated file
- **Fallback**: If not installed via setuptools (e.g., during development), defaults to `0.0.0.dev0`

### C++ Headers
The C++ version is extracted at CMake configuration time:

- **CMake script**: `cmake/GetGitVersion.cmake` extracts version from `git describe`
- **Template**: `cmake/version.h.in` is used to generate the version header
- **Generated file**: `include/bcsv/version_generated.h` contains version constants
- **Usage**: `include/bcsv/definitions.h` includes and uses the generated version

## Creating a New Release

### 1. Update Version (Create Git Tag)
```bash
# Create a new annotated tag
git tag -a v1.0.2 -m "Release version 1.0.2"

# Push the tag to GitHub
git push origin v1.0.2
```

### 2. Build Python Package Locally (Optional Testing)
```bash
cd python
pip install -e .

# Verify version
python -c "import pybcsv; print(pybcsv.__version__)"
# Should output: 1.0.2
```

### 3. Build C++ Project Locally (Optional Testing)
```bash
mkdir -p build && cd build
cmake ..
cmake --build .

# The version will be extracted from Git and used in version_generated.h
```

### 4. Automated CI/CD
When you push a tag to GitHub:
1. GitHub Actions workflow triggers automatically
2. Builds Python wheels for all platforms (~47 wheels)
3. Extracts version from the Git tag
4. Publishes to PyPI (and optionally TestPyPI)

## Workflow File Requirements

The `.github/workflows/build-and-publish.yml` workflow:
- Triggers on tags matching `v*` pattern: `on: push: tags: ["v*"]`
- Uses cibuildwheel to build Python packages
- Automatically extracts version via setuptools-scm
- Publishes to PyPI with `--skip-existing` flag (idempotent)

## Version Format

**Semantic Versioning**: `MAJOR.MINOR.PATCH`
- **MAJOR**: Incompatible API changes
- **MINOR**: Backwards-compatible functionality additions
- **PATCH**: Backwards-compatible bug fixes

**Tag Format**: `v<version>`
- Example: `v1.0.2`, `v2.0.0`, `v1.5.3`
- Regex: `^v[0-9]+\.[0-9]+\.[0-9]+$`

## Development Versions

### Python
During development (no tag checked out):
```bash
cd python
pip install -e .
python -c "import pybcsv; print(pybcsv.__version__)"
# Output: 0.0.0.dev0 (fallback version)
```

### C++
If no Git tag is found:
```bash
cmake ..
# Warning: No Git tag found, using default version 0.0.0
# Generated version will be 0.0.0
```

## Troubleshooting

### Python package shows wrong version
1. Ensure you have a Git tag: `git describe --tags`
2. Reinstall package: `pip install -e . --force-reinstall`
3. Check generated file exists: `python/pybcsv/_version.py`

### C++ shows wrong version
1. Ensure you have a Git tag: `git describe --tags`
2. Reconfigure CMake: `cd build && cmake ..`
3. Check generated file: `include/bcsv/version_generated.h`

### CI/CD fails to publish
- Check that tag format is correct: `v1.0.2` not `1.0.2`
- Verify GitHub Actions secrets are set (PYPI_TOKEN)
- Check workflow logs for specific errors

## File Reference

**Python:**
- `python/pyproject.toml` - setuptools-scm configuration
- `python/pybcsv/__version__.py` - imports from generated file
- `python/pybcsv/_version.py` - auto-generated (not in Git)

**C++:**
- `cmake/GetGitVersion.cmake` - version extraction script
- `cmake/version.h.in` - template for generated header
- `include/bcsv/version_generated.h` - auto-generated (not in Git)
- `include/bcsv/definitions.h` - uses generated version

**CI/CD:**
- `.github/workflows/build-and-publish.yml` - build and publish workflow

## Benefits

1. **Single source of truth**: Git tags
2. **No manual edits**: Version is extracted automatically
3. **Consistency**: Same version across Python, C++, and releases
4. **CI/CD ready**: Automated publishing on tag push
5. **Developer friendly**: Simple `git tag` command to release
