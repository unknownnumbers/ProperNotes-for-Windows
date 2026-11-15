$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskNode = Join-Path $taskRoot 'runtime\node.exe'
if (-not (Test-Path -LiteralPath $taskNode)) {
    throw 'The portable package is incomplete: runtime\node.exe is missing. No installation was attempted.'
}
$taskPrepare = Join-Path $taskRoot 'prepare.cjs'
# This runs only when the user explicitly launches the package. It uses bundled
# resources and never installs dependencies or downloads application assets.
& $taskNode $taskPrepare --ui-only
if ($LASTEXITCODE -ne 0) {
    throw 'Preparing ProperNotes from its sources failed. The application was not opened.'
}
$taskBuildInfo = Get-Content -LiteralPath (Join-Path $taskRoot 'build-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
function Test-ProperNotesService {
    param([string]$ServiceUrl)
    try {
        $serviceHealth = Invoke-RestMethod -Uri ($ServiceUrl + '/__inkpdf_health') -TimeoutSec 1
        if ($serviceHealth.app -ne 'inkpdf-local' -or $serviceHealth.apiVersion -lt 2) { return $false }
        if ($serviceHealth.instance -ne $taskBuildInfo.instance -or $serviceHealth.revision -ne $taskBuildInfo.revision) { return $false }
        $serviceBuild = Invoke-RestMethod -Uri ($ServiceUrl + '/build-info.json') -TimeoutSec 1
        return ($serviceBuild.app -eq 'inkpdf-local' -and $serviceBuild.instance -eq $taskBuildInfo.instance -and $serviceBuild.revision -eq $taskBuildInfo.revision)
    } catch { return $false }
}
$taskPort = 9150
$taskReady = $false
for ($taskAttempt = 0; $taskAttempt -lt 8; $taskAttempt++) {
    $taskUrl = 'http://127.0.0.1:' + $taskPort
    try {
        $taskHealth = Invoke-RestMethod -Uri ($taskUrl + '/__inkpdf_health') -TimeoutSec 1
        if (Test-ProperNotesService -ServiceUrl $taskUrl) { $taskReady = $true; break }
        $taskPort++
        continue
    } catch { }
    $taskServer = Join-Path $taskRoot 'server.cjs'
    Start-Process -FilePath $taskNode -ArgumentList @(('"' + $taskServer + '"'), '--port', $taskPort) -WorkingDirectory $taskRoot -WindowStyle Hidden
    for ($taskWait = 0; $taskWait -lt 35; $taskWait++) {
        Start-Sleep -Milliseconds 100
        try {
            if (Test-ProperNotesService -ServiceUrl $taskUrl) { $taskReady = $true; break }
        } catch { }
    }
    if ($taskReady) { break }
    $taskPort++
}
if (-not $taskReady) { throw 'ProperNotes local service could not start.' }
$taskBrowser = @('C:\Program Files\Google\Chrome\Application\chrome.exe', 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe', 'C:\Program Files\Microsoft\Edge\Application\msedge.exe') | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($taskBrowser) {
    Start-Process -FilePath $taskBrowser -ArgumentList ('--app=' + $taskUrl)
} else {
    Start-Process $taskUrl
}
