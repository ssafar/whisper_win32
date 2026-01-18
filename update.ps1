$base = "http://teamcity.ltn.simonsafar.com/guestAuth/repository/download/WhisperWin32_Build/.lastSuccessful"
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $dir

Write-Host "Updating whisper_win32..."

# Update whisper_win32.exe
if (Test-Path whisper_win32.exe) { Move-Item -Force whisper_win32.exe whisper_win32_old.exe }
Invoke-WebRequest "$base/whisper_win32.exe" -OutFile whisper_win32.exe
if (Test-Path whisper_win32_old.exe) { Remove-Item whisper_win32_old.exe }

Write-Host "Done!"
