@echo off
setlocal
REM Build script for whisper_win32 project
REM Usage: build.bat [Configuration] [MSBuildPath]
REM   Configuration: Debug (default) or Release
REM   MSBuildPath:   Path to MSBuild.exe (optional, for CI)

REM --- Change to script directory ---
cd /d "%~dp0"

REM --- Parse arguments ---
set "CONFIG=%~1"
set "MSBUILD_ARG=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"

echo Building whisper_win32 (%CONFIG% x64)...

REM --- Find MSBuild ---
if not "%MSBUILD_ARG%"=="" (
    set "MSBUILD=%MSBUILD_ARG%"
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
