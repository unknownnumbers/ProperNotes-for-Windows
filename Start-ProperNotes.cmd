@echo off
powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0Start-ProperNotes.ps1"
exit /b %errorlevel%
