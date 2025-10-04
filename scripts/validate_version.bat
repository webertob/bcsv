@echo off
REM Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
REM 
REM This file is part of the BCSV library.
REM 
REM Licensed under the MIT License. See LICENSE file in the project root 
REM for full license information.

REM validate_version.bat - Windows version of version validation script

setlocal enabledelayedexpansion

echo BCSV Version Validation
echo ======================

REM Check if we're in a git repository
git rev-parse --git-dir >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Not in a git repository
    exit /b 1
)

REM Get the latest git tag
for /f "tokens=*" %%i in ('git describe --tags --match "v*" --abbrev=0 2^>nul') do set GIT_TAG=%%i

if "!GIT_TAG!"=="" (
    echo [WARNING] No version tags found in git
    echo Current embedded version will be used as reference
) else (
    echo Latest git tag: !GIT_TAG!
)

REM Check if version_generated.h exists
set VERSION_FILE=include\bcsv\version_generated.h
if not exist "!VERSION_FILE!" (
    echo [ERROR] Version header not found: !VERSION_FILE!
    exit /b 1
)

REM Extract version from header file (simplified parsing)
for /f "tokens=3" %%i in ('findstr "constexpr int MAJOR" "!VERSION_FILE!"') do (
    set MAJOR_LINE=%%i
    set EMBEDDED_MAJOR=!MAJOR_LINE:;=!
)
for /f "tokens=3" %%i in ('findstr "constexpr int MINOR" "!VERSION_FILE!"') do (
    set MINOR_LINE=%%i
    set EMBEDDED_MINOR=!MINOR_LINE:;=!
)
for /f "tokens=3" %%i in ('findstr "constexpr int PATCH" "!VERSION_FILE!"') do (
    set PATCH_LINE=%%i
    set EMBEDDED_PATCH=!PATCH_LINE:;=!
)

set EMBEDDED_VERSION=!EMBEDDED_MAJOR!.!EMBEDDED_MINOR!.!EMBEDDED_PATCH!
echo Embedded version: !EMBEDDED_VERSION!

REM If we have a git tag, validate it matches
if not "!GIT_TAG!"=="" (
    REM Remove 'v' prefix from git tag
    set GIT_VERSION=!GIT_TAG:v=!
    
    if "!EMBEDDED_VERSION!"=="!GIT_VERSION!" (
        echo [SUCCESS] Version consistency check passed
        echo Git tag !GIT_TAG! matches embedded version !EMBEDDED_VERSION!
    ) else (
        echo [ERROR] Version mismatch!
        echo Git tag version: !GIT_VERSION!
        echo Embedded version: !EMBEDDED_VERSION!
        echo.
        echo To fix this, either:
        echo 1. Create a new git tag: git tag v!EMBEDDED_VERSION!
        echo 2. Or update the embedded version to match: !GIT_VERSION!
        exit /b 1
    )
) else (
    echo [SUCCESS] Version validation completed (no git tags to compare)
)

REM Show git describe if available
for /f "tokens=*" %%i in ('git describe --tags --always --dirty 2^>nul') do (
    echo Git describe: %%i
)

echo [SUCCESS] Version validation completed successfully!
endlocal