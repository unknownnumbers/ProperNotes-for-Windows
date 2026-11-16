@echo off
setlocal
if not exist "%~dp0ProperNotes.Native.exe" (
  echo This source folder is not a compiled portable package.
  echo Keep the complete compiled package together.
  pause
  exit /b 1
)
start "" /D "%~dp0" "%~dp0ProperNotes.Native.exe"
exit /b %errorlevel%
