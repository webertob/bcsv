#!/usr/bin/env python3
"""
Test runner for the pybcsv test suite.

This script runs all tests and provides a summary of results.
"""

import unittest
import sys
import os

# Add the pybcsv package to the path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

def run_tests():
    """Run all tests and return the result."""
    # Discover and run all tests
    loader = unittest.TestLoader()
    start_dir = os.path.dirname(__file__)
    suite = loader.discover(start_dir, pattern='test_*.py')
    
    # Run tests with verbose output
    runner = unittest.TextTestRunner(verbosity=2, buffer=True)
    result = runner.run(suite)
    
    return result

def main():
    """Main test runner function."""
    print("=" * 60)
    print("Running pybcsv test suite")
    print("=" * 60)
    
    try:
        import pybcsv
        print(f"✓ pybcsv version {pybcsv.__version__} loaded successfully")
    except ImportError as e:
        print(f"❌ Failed to import pybcsv: {e}")
        print("Make sure pybcsv is installed: pip install -e .")
        return 1
    
    # Check optional dependencies
    try:
        import pandas
        print(f"✓ pandas {pandas.__version__} available")
    except ImportError:
        print("⚠ pandas not available - some tests will be skipped")
    
    try:
        import numpy
        print(f"✓ numpy {numpy.__version__} available")
    except ImportError:
        print("⚠ numpy not available - some tests may fail")
    
    print("\n" + "=" * 60)
    print("Starting tests...")
    print("=" * 60)
    
    result = run_tests()
    
    print("\n" + "=" * 60)
    print("Test Summary")
    print("=" * 60)
    
    if result.wasSuccessful():
        print(f"✅ All tests passed!")
        print(f"   Tests run: {result.testsRun}")
        if result.skipped:
            print(f"   Tests skipped: {len(result.skipped)}")
        return 0
    else:
        print(f"❌ Some tests failed")
        print(f"   Tests run: {result.testsRun}")
        print(f"   Failures: {len(result.failures)}")
        print(f"   Errors: {len(result.errors)}")
        if result.skipped:
            print(f"   Tests skipped: {len(result.skipped)}")
        
        if result.failures:
            print("\nFailures:")
            for test, traceback in result.failures:
                print(f"  - {test}")
        
        if result.errors:
            print("\nErrors:")
            for test, traceback in result.errors:
                print(f"  - {test}")
        
        return 1

if __name__ == '__main__':
    sys.exit(main())