<#
.SYNOPSIS
    Build script for whisper_win32 project
.PARAMETER Config
    Build configuration: Debug (default) or Release
.PARAMETER MSBuild
    Path to MSBuild.exe (optional, auto-detected if not specified)
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$MSBuild = ""
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "Building whisper_win32 ($Config x64)..."

# Find MSBuild
if (-not $MSBuild) {
    $MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
}

if (-not (Test-Path $MSBuild)) {
    Write-Error "MSBuild not found at: $MSBuild"
    exit 1
}

# Build
& $MSBuild whisper_win32.sln /p:Configuration=$Config /p:Platform=x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`nBuild completed: x64\$Config\whisper_win32.exe"
