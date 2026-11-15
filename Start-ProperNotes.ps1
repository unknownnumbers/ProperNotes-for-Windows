$ErrorActionPreference = 'Stop'
# Native portable entry. Browser preview is an explicit separate command.
& (Join-Path $PSScriptRoot 'Start-ProperNotesNative.ps1')
exit $LASTEXITCODE
