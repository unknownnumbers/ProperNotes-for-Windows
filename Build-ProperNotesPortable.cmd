@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-ProperNotesPortable.ps1" %*
set "properNotesBuildExit=%ERRORLEVEL%"
if not "%properNotesBuildExit%"=="0" (
    echo.
    echo ProperNotes portable build failed. No application was launched.
    pause
)
exit /b %properNotesBuildExit%
