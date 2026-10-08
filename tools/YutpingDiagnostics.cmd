@echo off
setlocal
set "YUTPING_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "YUTPING_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%YUTPING_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0YutpingDiagnostics.ps1"
echo.
pause
