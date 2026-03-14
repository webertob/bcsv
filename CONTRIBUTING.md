# Contributing to BCSV

Thank you for your interest in contributing to BCSV! This document covers the workflow and requirements for contributions.

## Getting Started

1. **Fork** the repository and clone your fork
2. Create a **feature branch** from `master`: `git checkout -b feature/your-feature`
3. Make your changes, following the conventions below
4. **Test** your changes (see below)
5. Open a **Pull Request** against `master`

## Build & Test

All contributions must build and pass tests before merging.

```bash
# Debug build
cmake --preset ninja-debug && cmake --build --preset ninja-debug-build -j$(nproc)

# Run all C++ tests
ctest --test-dir build/ninja-debug --output-on-failure

# Run specific test suite
./build/ninja-debug/bin/bcsv_gtest --gtest_filter="YourTestSuite.*"

# C API tests
./build/ninja-debug/bin/test_c_api && ./build/ninja-debug/bin/test_row_api

# Python tests (if touching python/)
cd python && python -m pytest tests/ -v

# C# tests (if touching csharp/)
cd csharp && dotnet test
```

## Coding Conventions

See [SKILLS.md](SKILLS.md) for the full conventions reference. Key points:

- **Naming**: `lowerCamelCase` for functions, `UpperCamelCase` for classes, `snake_case_` for private members
- **Headers**: `.h` = declarations, `.hpp` = implementations, `#pragma once`
- **Include order**: standard library (`<>`) first, then project headers (`""`)
- **Error handling**: I/O operations return `bool` + `getErrorMsg()`; logic errors throw exceptions
- **Every code change must have a test.** No "fix and hope."

## Pull Request Checklist

Before submitting your PR, verify:

- [ ] Builds clean with `cmake --preset ninja-debug` (no warnings)
- [ ] All existing tests pass (`ctest --test-dir build/ninja-debug --output-on-failure`)
- [ ] New tests added for any code changes
- [ ] No breaking API changes without explicit deprecation
- [ ] Commit messages follow conventional format (e.g., `fix:`, `feat:`, `docs:`, `test:`)

## Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/) format:

```
feat: add Delta002 codec example
fix: Writer::close() now detects I/O errors
docs: update feature matrix in API_OVERVIEW.md
test: add boundary tests for MAX_COLUMNS
```

## Code of Conduct

This project follows the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). Please read it before contributing.

## Questions?

Open a [GitHub Issue](https://github.com/webertob/bcsv/issues) for questions, bug reports, or feature requests.
