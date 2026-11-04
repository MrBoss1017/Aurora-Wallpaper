#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$QtRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!(Test-Path (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) { throw 'QtRoot must point to the MSVC 2022 x64 Qt kit.' }
$env:AURORA_QT_ROOT = (Resolve-Path $QtRoot).Path
Push-Location $projectRoot
try {
    cmake --preset windows-release
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    cmake --build --preset windows-release --parallel
    if ($LASTEXITCODE) { throw 'Build failed.' }
    ctest --preset windows-release
    if ($LASTEXITCODE) { throw 'Tests failed.' }
    cmake --install build/windows --config Release
    if ($LASTEXITCODE) { throw 'Deployment failed.' }
    Write-Host "Run: $projectRoot\dist\bin\AuroraWallpaper.exe"
} finally { Pop-Location }
