@echo off
setlocal
REM Build script for whisper_win32 project
REM Usage: build.bat [Configuration]
REM   Configuration: Debug (default) or Release

REM --- Change to script directory ---
cd /d "%~dp0"

REM --- Parse arguments ---
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

echo Building whisper_win32 (%CONFIG% x64)...

REM --- Find MSBuild ---
REM Prefer TeamCity's MSBuild env var if available
if defined MSBuildTools17.0_x64_Path (
    set "MSBUILD=%MSBuildTools17.0_x64_Path%\MSBuild.exe"
) else (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
)

if not exist "%MSBUILD%" (
    echo ERROR: MSBuild not found at %MSBUILD%
    exit /b 1
)

REM --- Build ---
"%MSBUILD%" whisper_win32.sln /p:Configuration=%CONFIG% /p:Platform=x64 || exit /b 2

echo.
echo Build completed: x64\%CONFIG%\whisper_win32.exe
exit /b 0
