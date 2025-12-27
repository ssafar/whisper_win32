<#
.SYNOPSIS
    Downloads and installs the latest whisper_win32 build from the share
.PARAMETER SharePath
    UNC path to the builds share (default: \\anarillis.ltn.simonsafar.com\buttersky\builds\whisper_win32)
.PARAMETER InstallDir
    Local installation directory (default: C:\local\voice)
.PARAMETER ShortcutName
    Desktop shortcut name to update (default: recorder.lnk)
#>
param(
    [string]$SharePath = "\\anarillis.ltn.simonsafar.com\buttersky\builds\whisper_win32",
    [string]$InstallDir = "C:\local\voice",
    [string]$ShortcutName = "recorder.lnk"
)

$ErrorActionPreference = "Stop"

# Read latest build number
$latestFile = Join-Path $SharePath "latest.txt"
if (-not (Test-Path $latestFile)) {
    Write-Error "Cannot find $latestFile"
    exit 1
}
$buildNumber = (Get-Content $latestFile).Trim()
Write-Host "Latest build: $buildNumber"

# Check current version
$localBuildFile = Join-Path $InstallDir "build.txt"
if (Test-Path $localBuildFile) {
    $currentBuild = (Get-Content $localBuildFile).Trim()
    if ($currentBuild -eq $buildNumber) {
        Write-Host "Already up to date (build $buildNumber)"
        exit 0
    }
    Write-Host "Current build: $currentBuild"
}

# Download zip
$zipName = "whisper_win32-$buildNumber.zip"
$zipPath = Join-Path $SharePath $zipName
if (-not (Test-Path $zipPath)) {
    Write-Error "Cannot find $zipPath"
    exit 1
}

Write-Host "Downloading $zipName..."
$tempZip = Join-Path $env:TEMP $zipName
Copy-Item $zipPath $tempZip -Force

# Extract to install dir
Write-Host "Extracting to $InstallDir..."
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# Extract (overwrites existing files)
Expand-Archive -Path $tempZip -DestinationPath $InstallDir -Force

# Rename exe to recorder.exe if needed
$srcExe = Join-Path $InstallDir "whisper_win32.exe"
$dstExe = Join-Path $InstallDir "recorder.exe"
if ((Test-Path $srcExe) -and ($srcExe -ne $dstExe)) {
    if (Test-Path $dstExe) { Remove-Item $dstExe -Force }
    Rename-Item $srcExe $dstExe
}

# Update shortcut
$desktop = [Environment]::GetFolderPath('Desktop')
$lnkPath = Join-Path $desktop $ShortcutName
if (Test-Path $lnkPath) {
    $ws = New-Object -ComObject WScript.Shell
    $shortcut = $ws.CreateShortcut($lnkPath)
    $shortcut.TargetPath = $dstExe
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.Save()
    Write-Host "Updated shortcut: $lnkPath"
} else {
    Write-Host "Shortcut not found: $lnkPath (skipping)"
}

# Cleanup
Remove-Item $tempZip -Force

# Copy this script to install dir and create updater shortcut
$scriptDest = Join-Path $InstallDir "update-local.ps1"
if ($PSCommandPath -and ($PSCommandPath -ne $scriptDest)) {
    Copy-Item $PSCommandPath $scriptDest -Force
    Write-Host "Copied updater script to $scriptDest"
}

$updaterLnk = Join-Path $desktop "Update Recorder.lnk"
if (-not (Test-Path $updaterLnk)) {
    $ws = New-Object -ComObject WScript.Shell
    $shortcut = $ws.CreateShortcut($updaterLnk)
    $shortcut.TargetPath = "powershell.exe"
    $shortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$scriptDest`""
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.Description = "Update Recorder to latest build"
    $shortcut.Save()
    Write-Host "Created shortcut: $updaterLnk"
}

Write-Host "`nInstalled build $buildNumber to $InstallDir"
