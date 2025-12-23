<#
.SYNOPSIS
    Publish script for whisper_win32 project
.PARAMETER BuildNumber
    Build number (required)
.PARAMETER SharePath
    UNC path to publish destination (required)
.PARAMETER AppName
    Application name (default: whisper_win32)
.PARAMETER Config
    Build configuration (default: Release)
#>
param(
    [Parameter(Mandatory=$true)]
    [string]$BuildNumber,
    [Parameter(Mandatory=$true)]
    [string]$SharePath,
    [string]$AppName = "whisper_win32",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$OutDir = Join-Path $PSScriptRoot "x64\$Config"
$ArtifactsDir = Join-Path $PSScriptRoot "_artifacts"
$StageDir = Join-Path $ArtifactsDir "stage"
$ZipPath = Join-Path $ArtifactsDir "$AppName-$BuildNumber.zip"

Write-Host "Publishing $AppName build $BuildNumber"
Write-Host "  Source: $OutDir"
Write-Host "  Target: $SharePath"

# Validate build output exists
$ExePath = Join-Path $OutDir "$AppName.exe"
if (-not (Test-Path $ExePath)) {
    Write-Error "Missing: $ExePath"
    exit 2
}

# Stage files
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item $ExePath $StageDir
Set-Content (Join-Path $StageDir "build.txt") $BuildNumber

# Create zip
if (-not (Test-Path $ArtifactsDir)) { New-Item -ItemType Directory -Path $ArtifactsDir -Force | Out-Null }
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath

Write-Host "Created: $ZipPath"

# Copy to share
if (-not (Test-Path $SharePath)) { New-Item -ItemType Directory -Path $SharePath -Force | Out-Null }
Copy-Item $ZipPath $SharePath
Set-Content (Join-Path $SharePath "latest.txt") $BuildNumber

Write-Host "`nPublished to $SharePath\$AppName-$BuildNumber.zip"
Write-Host "Updated $SharePath\latest.txt"
