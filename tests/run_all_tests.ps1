#!/usr/bin/env pwsh
# Script to run both C++ and C API test suites

Write-Host "BCSV Library Test Suite Runner" -ForegroundColor Cyan
Write-Host "==============================" -ForegroundColor Cyan

Write-Host ""
Write-Host "Running C++ Test Suite (bcsv_gtest.exe)..." -ForegroundColor Yellow
$cppResult = & "..\build\bin\Debug\bcsv_gtest.exe" --gtest_brief=1
$cppExitCode = $LASTEXITCODE

Write-Host ""
Write-Host "Running C API Test Suite (test_c_api.exe)..." -ForegroundColor Yellow  
$cApiResult = & "..\build\Debug\test_c_api.exe"
$cApiExitCode = $LASTEXITCODE

Write-Host ""
Write-Host "Test Results Summary:" -ForegroundColor Cyan
Write-Host "===================="  -ForegroundColor Cyan

if ($cppExitCode -eq 0) {
    Write-Host "✅ C++ Tests: PASSED" -ForegroundColor Green
} else {
    Write-Host "❌ C++ Tests: FAILED" -ForegroundColor Red
}

if ($cApiExitCode -eq 0) {
    Write-Host "✅ C API Tests: PASSED" -ForegroundColor Green
} else {
    Write-Host "❌ C API Tests: FAILED" -ForegroundColor Red
}

if ($cppExitCode -eq 0 -and $cApiExitCode -eq 0) {
    Write-Host ""
    Write-Host "🎉 All tests passed!" -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Host "Some tests failed!" -ForegroundColor Red
    exit 1
}