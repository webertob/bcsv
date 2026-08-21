<#
.SYNOPSIS
    Builds bcsv_c_api.dll for the Unity package, self-contained.

.DESCRIPTION
    The Unity package ships prebuilt natives because a Unity project cannot
    build C++. The requirement is therefore the same one the ABB robotics
    package had to meet: the DLL must load on a machine that has never had
    Visual Studio on it.

    That means the static CRT (/MT). With /MD the DLL imports MSVCP140.dll and
    VCRUNTIME140.dll and fails to load without the Visual C++ Redistributable,
    which is not something a Unity package can ask its users to install. The
    cost of /MT is a private CRT per module, which is safe here because nothing
    CRT-owned crosses the boundary - the C API passes handles, primitives and
    caller-supplied buffers, never a FILE*, a std::string or an allocation the
    caller must free.

    LZ4 and xxHash are vendored in the repository, so the build pulls in nothing
    external. pkg-config is disabled explicitly rather than left to chance: on a
    machine that happens to have a system LZ4 the link would silently pick it up
    and the DLL would stop being self-contained.
#>
param(
    [string]$Configuration = 'Release',
    [string]$BuildDir     = "$PSScriptRoot\..\build\unity-windows",
    [string]$OutputDir    = "$PSScriptRoot\..\unity\Runtime\Plugins\Windows\x86_64"
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\.."

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    Write-Host "  $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with $LASTEXITCODE" }
}

Write-Host "configuring" -ForegroundColor Cyan
Invoke-Checked cmake @(
    '-S', "$repo",
    '-B', "$BuildDir",
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    # CMP0091 must be NEW or CMAKE_MSVC_RUNTIME_LIBRARY is ignored and the
    # build silently falls back to /MD.
    '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
    '-DBUILD_TOOLS=OFF',
    '-DBUILD_TESTS=OFF',
    '-DBUILD_EXAMPLES=OFF',
    '-DBUILD_BENCHMARKS=OFF',
    '-DBCSV_WERROR=OFF',
    '-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON'
)

Write-Host "building" -ForegroundColor Cyan
Invoke-Checked cmake @('--build', "$BuildDir", '--config', $Configuration, '--target', 'bcsv_c_api')

$dll = Get-ChildItem -Path $BuildDir -Recurse -Filter 'bcsv_c_api.dll' |
       Where-Object { $_.FullName -match [regex]::Escape($Configuration) -or $_.Directory.Name -eq 'bin' } |
       Select-Object -First 1
if (-not $dll) { $dll = Get-ChildItem -Path $BuildDir -Recurse -Filter 'bcsv_c_api.dll' | Select-Object -First 1 }
if (-not $dll) { throw 'bcsv_c_api.dll was not produced' }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Copy-Item $dll.FullName (Join-Path $OutputDir 'bcsv_c_api.dll') -Force
Write-Host "deployed $($dll.FullName) -> $OutputDir" -ForegroundColor Green

# Verification, not decoration: a /MD build differs from a /MT build only in its
# import table, so that is what gets checked.
$dumpbin = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -match 'Hostx64\x64' } | Select-Object -First 1
if ($dumpbin) {
    $imports = & $dumpbin.FullName /DEPENDENTS (Join-Path $OutputDir 'bcsv_c_api.dll') |
               Select-String -Pattern '^\s+\S+\.dll$' | ForEach-Object { $_.Matches.Value.Trim() }
    Write-Host "imports:" -ForegroundColor Cyan
    $imports | ForEach-Object { Write-Host "  $_" }

    $bad = $imports | Where-Object { $_ -match 'MSVCP|VCRUNTIME|api-ms-win-crt' }
    if ($bad) { throw "still needs the Visual C++ Redistributable: $($bad -join ', ')" }
    Write-Host "PLUGIN PASS | no Visual C++ Redistributable needed" -ForegroundColor Green
} else {
    Write-Host "PLUGIN WARN | dumpbin not found, import table unverified" -ForegroundColor Yellow
}
