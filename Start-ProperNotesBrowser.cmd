@echo off
rem Explicit browser preview; this is not the native Windows host.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-ProperNotesBrowser.ps1"
if errorlevel 1 (
    echo.
    echo ProperNotes browser preview could not start. See the error above.
    pause
    exit /b 1
)
