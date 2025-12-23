@echo off
setlocal
REM Publish script for whisper_win32 project
REM Usage: publish.bat <build_number> <share_path> [app_name] [config]
REM   build_number: TeamCity build number (required)
REM   share_path:   UNC path to publish destination (required)
REM   app_name:     Application name (default: whisper_win32)
REM   config:       Build configuration (default: Release)

REM --- Parse arguments ---
set "BUILD_NUM=%~1"
set "SHARE=%~2"
set "APP=%~3"
set "CONFIG=%~4"

if "%BUILD_NUM%"=="" (
    echo ERROR: Build number is required
    echo Usage: publish.bat ^<build_number^> ^<share_path^> [app_name] [config]
    exit /b 1
)
if "%SHARE%"=="" (
    echo ERROR: Share path is required
    echo Usage: publish.bat ^<build_number^> ^<share_path^> [app_name] [config]
    exit /b 1
)
if "%APP%"=="" set "APP=whisper_win32"
if "%CONFIG%"=="" set "CONFIG=Release"

REM --- Setup paths ---
set "ROOT=%~dp0"
set "OUTDIR=%ROOT%x64\%CONFIG%"
set "STAGE=%ROOT%_artifacts\stage"
set "ZIP=%ROOT%_artifacts\%APP%-%BUILD_NUM%.zip"

echo Publishing %APP% build %BUILD_NUM%
echo   Source: %OUTDIR%
echo   Target: %SHARE%

REM --- Validate build output exists ---
if not exist "%OUTDIR%\%APP%.exe" (
    echo ERROR: Missing %OUTDIR%\%APP%.exe
    exit /b 2
)

REM --- Stage files ---
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 3

copy /y "%OUTDIR%\%APP%.exe" "%STAGE%\" >nul || exit /b 4
echo %BUILD_NUM%> "%STAGE%\build.txt"

REM --- Create zip ---
if not exist "%ROOT%_artifacts" mkdir "%ROOT%_artifacts"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "if (Test-Path '%ZIP%') { Remove-Item -Force '%ZIP%' }; Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%'" || exit /b 5

echo Created: %ZIP%

REM --- Copy to share ---
if not exist "%SHARE%" mkdir "%SHARE%"
copy /y "%ZIP%" "%SHARE%\" >nul || exit /b 6
echo %BUILD_NUM%> "%SHARE%\latest.txt"

echo.
echo Published to %SHARE%\%APP%-%BUILD_NUM%.zip
echo Updated %SHARE%\latest.txt
exit /b 0
