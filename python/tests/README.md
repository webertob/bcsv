# PyBCSV Test Suite

This directory contains comprehensive unit tests for the PyBCSV Python implementation. The tests validate functionality, error handling, interoperability, and performance characteristics.

## Test Modules

### 1. `test_basic_functionality.py`
**Purpose**: Core BCSV functionality validation
- Layout creation and configuration
- All supported data types (int8, int16, int32, int64, float32, float64, strings)
- Batch operations and context managers
- Compression level testing
- File operations (create, read, write)

### 2. `test_interoperability.py`
**Purpose**: Python-C++ compatibility validation
- Cross-platform file compatibility
- Roundtrip testing (Python → C++ → Python)
- Unicode preservation across tools
- CSV conversion validation with C++ tools

### 3. `test_error_handling_edge_cases.py`
**Purpose**: Error detection and handling validation
- File not found scenarios
- Corrupted file detection
- Type validation and mismatches
- Data overflow handling
- Invalid operations and concurrent access

### 4. `test_pandas_integration.py`
**Purpose**: DataFrame integration testing
- DataFrame roundtrip operations
- Data type preservation (dtypes)
- Large dataset handling
- Missing value handling
- Unicode support in DataFrames

### 5. `test_performance_edge_cases.py`
**Purpose**: Performance characteristics and boundary testing
- Boundary value testing for all numeric types
- Large dataset handling (50,000+ rows)
- Memory usage profiling
- String edge cases (Unicode, control characters)
- Compression effectiveness analysis

## Running Tests

### Quick Start

```bash
# Run all tests with the comprehensive test runner
cd python/tests
python run_all_tests.py
```

### Individual Test Modules

```bash
# Run specific test module
python -m unittest test_basic_functionality.py -v
python -m unittest test_interoperability.py -v
python -m unittest test_error_handling_edge_cases.py -v
python -m unittest test_pandas_integration.py -v
python -m unittest test_performance_edge_cases.py -v
```

### Using pytest (if available)

```bash
# Run all tests with pytest
pytest tests/ -v

# Run specific test module
pytest test_basic_functionality.py -v

# Run with coverage
pytest tests/ --cov=pybcsv --cov-report=html
```

## Prerequisites

### Required Dependencies

- `pybcsv` (the Python BCSV implementation)
- `numpy` (for numerical operations and data types)
- `pandas` (for DataFrame integration tests)

### Optional Dependencies for Full Testing

- C++ build tools and compiled examples (`bcsv2csv`, `csv2bcsv`)
- `pytest` (alternative test runner)
- `pytest-cov` (for coverage reporting)

### Installation

```bash
# Install required Python packages
pip install numpy pandas

# For development/testing
pip install pytest pytest-cov
```

## Test Features

### Comprehensive Coverage

- **Positive Testing**: Validates expected functionality works correctly
- **Negative Testing**: Ensures proper error handling for invalid inputs
- **Edge Cases**: Tests boundary conditions and unusual scenarios
- **Performance**: Validates performance characteristics and memory usage
- **Interoperability**: Cross-validates with C++ implementation

### Automatic Cleanup

All tests use temporary files with automatic cleanup to prevent test pollution.

### Memory Profiling

Performance tests include memory usage monitoring to detect leaks or excessive memory consumption.

### Error Simulation

Tests include simulated error conditions (corrupted files, invalid data) to validate error handling.

## Understanding Test Output

### Test Runner Output

The `run_all_tests.py` script provides colored output:

- ✅ **PASS**: Test completed successfully
- ❌ **FAIL**: Test failed due to assertion error
- ❌ **ERROR**: Test failed due to exception
- ⏭️ **SKIP**: Test was skipped (usually due to missing dependencies)

### Summary Statistics

- **Passed**: Number of successful tests
- **Failed**: Number of failed assertions
- **Errors**: Number of tests that threw exceptions
- **Skipped**: Number of tests that were skipped
- **Time**: Execution time for each test suite

## Troubleshooting

### Common Issues

1. **ModuleNotFoundError: No module named 'pybcsv'**
   - Ensure PyBCSV is installed and accessible
   - Check Python path includes the PyBCSV module

2. **C++ interoperability tests failing**
   - Ensure C++ examples are built: `cmake --build build`
   - Check that `bcsv2csv` and `csv2bcsv` are in build/examples/

3. **Permission errors on temporary files**
   - Ensure write permissions in the test directory
   - Check that no test files are left open from previous runs

4. **Memory-related test failures**
   - Large dataset tests may fail on systems with limited memory
   - Consider reducing test data sizes for constrained environments

### Debugging Individual Tests

```bash
# Run single test with full output
python -m unittest test_basic_functionality.TestBasicFunctionality.test_layout_creation -v

# Run with Python debugger
python -m pdb -m unittest test_basic_functionality.py
```

## Test Development Guidelines

### Adding New Tests

1. Choose the appropriate test module based on functionality
2. Use `setUp()` and `tearDown()` for test isolation
3. Use temporary files with automatic cleanup
4. Include both positive and negative test cases
5. Add descriptive test names and docstrings

### Test Naming Convention

- `test_[functionality]_[scenario]`: e.g., `test_layout_creation_with_all_types`
- Use descriptive names that explain what is being tested
- Group related tests in the same test class

### Best Practices

- Keep tests focused and atomic
- Use assertions that provide clear failure messages
- Clean up resources in `tearDown()` methods
- Mock external dependencies when appropriate
- Include performance benchmarks for critical operations

## Integration with CI/CD

The test suite is designed to be easily integrated into continuous integration pipelines:

```yaml
# Example GitHub Actions workflow
- name: Run PyBCSV Tests
  run: |
    cd python/tests
    python run_all_tests.py
```

Exit codes:

- `0`: All tests passed
- `1`: One or more tests failed

## Test Coverage Goals

- **Functionality**: 100% of public API methods
- **Error Paths**: All documented error conditions
- **Data Types**: All supported BCSV data types
- **Edge Cases**: Boundary conditions and unusual inputs
- **Performance**: Key performance characteristics
- **Interoperability**: Cross-platform and cross-language compatibility