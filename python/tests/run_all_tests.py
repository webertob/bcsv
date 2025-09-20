#!/usr/bin/env python3
"""
Comprehensive test runner for PyBCSV unit tests.
Runs all test suites and provides a summary report.
"""

import unittest
import sys
import os
import time
from io import StringIO

# Add the parent directory to Python path to import tests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Import all test modules
from test_basic_functionality import TestBasicFunctionality
from test_interoperability import TestInteroperability
from test_error_handling_edge_cases import TestErrorHandling
from test_pandas_integration import TestPandasIntegration
from test_performance_edge_cases import TestPerformanceEdgeCases


class ColoredTextTestResult(unittest.TextTestResult):
    """Test result with colored output for better readability."""
    
    def startTest(self, test):
        super().startTest(test)
        self.stream.write(f"Running {test._testMethodName}... ")
        self.stream.flush()
    
    def addSuccess(self, test):
        super().addSuccess(test)
        self.stream.write("✅ PASS\n")
        self.stream.flush()
    
    def addError(self, test, err):
        super().addError(test, err)
        self.stream.write("❌ ERROR\n")
        self.stream.flush()
    
    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.stream.write("❌ FAIL\n")
        self.stream.flush()
    
    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.stream.write(f"⏭️  SKIP ({reason})\n")
        self.stream.flush()


def run_test_suite(test_class, suite_name):
    """Run a specific test suite and return results."""
    print(f"\n{'='*60}")
    print(f"🧪 {suite_name}")
    print(f"{'='*60}")
    
    # Create test suite
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(test_class)
    
    # Run tests with custom result class
    stream = sys.stdout
    runner = unittest.TextTestRunner(
        stream=stream, 
        verbosity=0,
        resultclass=ColoredTextTestResult
    )
    
    start_time = time.time()
    result = runner.run(suite)
    end_time = time.time()
    
    # Print summary for this suite
    total_tests = result.testsRun
    failures = len(result.failures)
    errors = len(result.errors)
    skipped = len(result.skipped)
    passed = total_tests - failures - errors - skipped
    
    print(f"\n📊 Suite Summary:")
    print(f"   ✅ Passed: {passed}/{total_tests}")
    if failures > 0:
        print(f"   ❌ Failed: {failures}")
    if errors > 0:
        print(f"   💥 Errors: {errors}")
    if skipped > 0:
        print(f"   ⏭️  Skipped: {skipped}")
    print(f"   ⏱️  Time: {end_time - start_time:.2f}s")
    
    return result, end_time - start_time


def print_detailed_failures(all_results):
    """Print detailed information about failures and errors."""
    has_failures = False
    
    for suite_name, result, _ in all_results:
        if result.failures or result.errors:
            if not has_failures:
                print(f"\n{'='*60}")
                print("🔍 DETAILED FAILURE/ERROR REPORT")
                print(f"{'='*60}")
                has_failures = True
            
            print(f"\n📋 {suite_name}:")
            
            for test, traceback in result.failures:
                print(f"\n❌ FAILURE: {test}")
                print("-" * 40)
                print(traceback)
            
            for test, traceback in result.errors:
                print(f"\n💥 ERROR: {test}")
                print("-" * 40)
                print(traceback)


def main():
    """Run all PyBCSV tests and provide comprehensive report."""
    print("🚀 PyBCSV Comprehensive Test Suite")
    print("Testing Python BCSV implementation for:")
    print("  • Core functionality and data integrity")
    print("  • C++/Python interoperability") 
    print("  • Error handling and edge cases")
    print("  • Pandas DataFrame integration")
    print("  • Performance and boundary conditions")
    
    # Test suites to run
    test_suites = [
        (TestBasicFunctionality, "Basic Functionality Tests"),
        (TestInteroperability, "C++/Python Interoperability Tests"),
        (TestErrorHandling, "Error Handling & Edge Cases"),
        (TestPandasIntegration, "Pandas Integration Tests"),
        (TestPerformanceEdgeCases, "Performance & Edge Cases"),
    ]
    
    # Run all test suites
    all_results = []
    total_start_time = time.time()
    
    for test_class, suite_name in test_suites:
        try:
            result, duration = run_test_suite(test_class, suite_name)
            all_results.append((suite_name, result, duration))
        except Exception as e:
            print(f"❌ Failed to run {suite_name}: {e}")
            continue
    
    total_end_time = time.time()
    
    # Calculate overall statistics
    total_tests = sum(result.testsRun for _, result, _ in all_results)
    total_failures = sum(len(result.failures) for _, result, _ in all_results)
    total_errors = sum(len(result.errors) for _, result, _ in all_results)
    total_skipped = sum(len(result.skipped) for _, result, _ in all_results)
    total_passed = total_tests - total_failures - total_errors - total_skipped
    total_time = total_end_time - total_start_time
    
    # Print overall summary
    print(f"\n{'='*60}")
    print("🎯 OVERALL TEST SUMMARY")
    print(f"{'='*60}")
    print(f"📊 Total Tests: {total_tests}")
    print(f"✅ Passed: {total_passed} ({total_passed/total_tests*100:.1f}%)" if total_tests > 0 else "✅ Passed: 0")
    print(f"❌ Failed: {total_failures}")
    print(f"💥 Errors: {total_errors}")
    print(f"⏭️  Skipped: {total_skipped}")
    print(f"⏱️  Total Time: {total_time:.2f}s")
    
    # Success rate
    if total_tests > 0:
        success_rate = (total_passed / total_tests) * 100
        if success_rate >= 95:
            print(f"🎉 Excellent! {success_rate:.1f}% success rate")
        elif success_rate >= 80:
            print(f"✅ Good! {success_rate:.1f}% success rate")
        elif success_rate >= 60:
            print(f"⚠️  Acceptable: {success_rate:.1f}% success rate")
        else:
            print(f"❌ Needs improvement: {success_rate:.1f}% success rate")
    
    # Per-suite breakdown
    print(f"\n📋 Per-Suite Results:")
    for suite_name, result, duration in all_results:
        total = result.testsRun
        passed = total - len(result.failures) - len(result.errors) - len(result.skipped)
        status = "✅" if len(result.failures) == 0 and len(result.errors) == 0 else "❌"
        print(f"   {status} {suite_name}: {passed}/{total} passed ({duration:.2f}s)")
    
    # Print detailed failures if any
    print_detailed_failures(all_results)
    
    # Final recommendations
    print(f"\n{'='*60}")
    print("📝 RECOMMENDATIONS")
    print(f"{'='*60}")
    
    if total_failures == 0 and total_errors == 0:
        print("🎉 All tests passed! PyBCSV implementation is working correctly.")
        print("✅ The Python wrapper demonstrates:")
        print("   • Correct basic functionality")
        print("   • Proper error handling")
        print("   • C++/Python interoperability")
        print("   • Pandas integration")
        print("   • Good performance characteristics")
    else:
        print("⚠️  Some tests failed. Please review the detailed failure report above.")
        print("🔧 Recommended actions:")
        if total_errors > 0:
            print("   • Fix critical errors that prevent test execution")
        if total_failures > 0:
            print("   • Address test failures to ensure correct behavior")
        if total_skipped > 0:
            print("   • Review skipped tests and their skip reasons")
    
    # Return exit code
    exit_code = 0 if (total_failures == 0 and total_errors == 0) else 1
    return exit_code


if __name__ == '__main__':
    exit_code = main()
    sys.exit(exit_code)