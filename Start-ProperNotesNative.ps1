$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$taskLog = $null
$taskServer = $null
$taskServerOutput = $null
$taskServerErrors = $null
$taskLaunchMutex = $null
$taskOwnsMutex = $false
$taskExitCode = 0

# Ordinary launch never builds the native host, restores packages or installs
# runtimes. Build-ProperNotesPortable is a separate, explicit release step.
function Get-TaskPath {
    param([string]$Relative)
    if ([string]::IsNullOrWhiteSpace($Relative) -or [System.IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':')) { throw 'Invalid portable package path.' }
    $taskCandidate = [System.IO.Path]::GetFullPath((Join-Path $taskRoot $Relative))
    if (-not $taskCandidate.StartsWith(($taskRoot.TrimEnd('\') + '\'), [System.StringComparison]::OrdinalIgnoreCase)) { throw 'A package path leaves its root folder.' }
    return $taskCandidate
}
function Assert-TaskPath {
    param([string]$Relative, [switch]$Directory)
    $taskCandidate = Get-TaskPath $Relative
    $taskCurrent = $taskCandidate
    while ($taskCurrent -ne $taskRoot) {
        $taskItem = Get-Item -LiteralPath $taskCurrent -Force -ErrorAction Stop
        if (($taskItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Portable package links are not supported: ' + $Relative) }
        $taskCurrent = [System.IO.Path]::GetDirectoryName($taskCurrent)
    }
    $taskItem = Get-Item -LiteralPath $taskCandidate -Force
    if ($Directory -and -not $taskItem.PSIsContainer) { throw ('Required folder is missing: ' + $Relative) }
    if (-not $Directory -and $taskItem.PSIsContainer) { throw ('Required file is missing: ' + $Relative) }
    return $taskCandidate
}
function Write-TaskLog {
    param([string]$Message)
    if ($taskLog) { [System.IO.File]::AppendAllText($taskLog, ([DateTime]::UtcNow.ToString('o') + ' ' + $Message + [Environment]::NewLine), [System.Text.UTF8Encoding]::new($false)) }
}
function ConvertTo-TaskArgument {
    param([string]$Value)
    # Windows CreateProcess quoting, including embedded quotes/trailing slashes.
    $taskBuilder = [System.Text.StringBuilder]::new()
    [void]$taskBuilder.Append('"'); $taskSlashes = 0
    foreach ($taskCharacter in $Value.ToCharArray()) {
        if ($taskCharacter -eq '\') { $taskSlashes++; continue }
        if ($taskCharacter -eq '"') { [void]$taskBuilder.Append(('\' * ($taskSlashes * 2 + 1))); [void]$taskBuilder.Append('"') }
        else { [void]$taskBuilder.Append(('\' * $taskSlashes)); [void]$taskBuilder.Append($taskCharacter) }
        $taskSlashes = 0
    }
    [void]$taskBuilder.Append(('\' * ($taskSlashes * 2))); [void]$taskBuilder.Append('"')
    return $taskBuilder.ToString()
}
function New-TaskProcessInfo {
    param([string]$Executable, [string[]]$Arguments)
    $taskInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $taskInfo.FileName = $Executable
    $taskInfo.Arguments = (($Arguments | ForEach-Object { ConvertTo-TaskArgument $_ }) -join ' ')
    $taskInfo.WorkingDirectory = $taskRoot; $taskInfo.UseShellExecute = $false
    return $taskInfo
}
function Invoke-TaskHostCommand {
    param([string[]]$Arguments)
    $taskInfo = New-TaskProcessInfo $taskHost $Arguments
    $taskInfo.CreateNoWindow = $true
    $taskCommand = [System.Diagnostics.Process]::Start($taskInfo)
    try {
        if (-not $taskCommand.WaitForExit(30000)) { throw 'The native host preflight did not finish. No second window was opened.' }
        return $taskCommand.ExitCode
    } finally { $taskCommand.Dispose() }
}
function Assert-TaskHash {
    param([string]$Relative, [string]$Expected, $Size)
    if ($Expected -notmatch '^[a-f0-9]{64}$') { throw 'Invalid portable file fingerprint.' }
    $taskFile = Assert-TaskPath $Relative
    if ($null -ne $Size -and (Get-Item -LiteralPath $taskFile).Length -ne [long]$Size) { throw ('Portable file size mismatch: ' + $Relative) }
    if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Expected) { throw ('Portable file fingerprint mismatch: ' + $Relative) }
}
function Close-TaskService {
    if ($null -eq $script:taskServer) { return }
    try {
        if (-not $script:taskServer.HasExited) {
            Write-TaskLog 'Closing the owned server input; waiting for file transactions.'
            $script:taskServer.StandardInput.Close()
            # A disconnected HTTP client can leave a file transaction in flight.
            # Never kill a process or impose a deadline on its file commit.
            $script:taskServer.WaitForExit()
        }
        if ($script:taskServerOutput) { Write-TaskLog ($script:taskServerOutput.GetAwaiter().GetResult()) }
        if ($script:taskServerErrors) { Write-TaskLog ($script:taskServerErrors.GetAwaiter().GetResult()) }
    } finally {
        $script:taskServer.Dispose(); $script:taskServer = $null
        $script:taskServerOutput = $null; $script:taskServerErrors = $null
    }
}
function Test-TaskService {
    param([string]$ServiceUrl, [string]$OwnerId)
    try {
        $taskHealth = Invoke-RestMethod -Uri ($ServiceUrl + '/__inkpdf_health') -TimeoutSec 1
        if ($taskHealth.app -ne 'inkpdf-local' -or $taskHealth.apiVersion -lt 2 -or $taskHealth.lifetime -ne 'owner-stdin' -or $taskHealth.ownerId -ne $OwnerId) { return $false }
        if ($taskHealth.instance -ne $taskBuildInfo.instance -or $taskHealth.revision -ne $taskBuildInfo.revision) { return $false }
        $taskMarker = Invoke-RestMethod -Uri ($ServiceUrl + '/build-info.json') -TimeoutSec 1
        return ($taskMarker.instance -eq $taskBuildInfo.instance -and $taskMarker.revision -eq $taskBuildInfo.revision -and $taskMarker.app -eq 'inkpdf-local')
    } catch { return $false }
}
try {
    if ($env:OS -ne 'Windows_NT' -or $taskRoot.StartsWith('\\')) { throw 'Run the portable Windows package from a local Windows folder.' }
    $taskRootItem = Get-Item -LiteralPath $taskRoot -Force
    if (($taskRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Run the package from its real folder, not a directory link.' }
    $taskManifestPath = Get-TaskPath 'propernotes-portable.json'
    if (-not (Test-Path -LiteralPath $taskManifestPath -PathType Leaf)) { throw 'This is a source checkout, not a published native package. On a build machine with the .NET 10 SDK, use Build-ProperNotesPortable.cmd first. Nothing was built or installed by this launcher.' }
    $taskManifestPath = Assert-TaskPath 'propernotes-portable.json'
    if ((Get-Item -LiteralPath $taskManifestPath).Length -gt 8MB) { throw 'The portable manifest is too large.' }
    $taskManifest = Get-Content -LiteralPath $taskManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($taskManifest.schemaVersion -ne 1 -or $taskManifest.app -ne 'propernotes-windows' -or $taskManifest.rid -notin @('win-x64','win-arm64') -or $taskManifest.host.path -ne 'host/ProperNotes.Host.exe' -or $taskManifest.node.path -ne 'runtime/node.exe') { throw 'Unsupported portable package manifest.' }
    $taskOsArchitecture = $env:PROCESSOR_ARCHITEW6432
    if (-not $taskOsArchitecture) { $taskOsArchitecture = $env:PROCESSOR_ARCHITECTURE }
    $taskExpectedArchitecture = if ($taskOsArchitecture -eq 'AMD64') { 'x64' } elseif ($taskOsArchitecture -eq 'ARM64') { 'arm64' } else { '' }
    if (-not $taskExpectedArchitecture -or $taskManifest.rid -ne ('win-' + $taskExpectedArchitecture) -or $taskManifest.node.architecture -ne $taskExpectedArchitecture) { throw 'This package does not match the native Windows architecture. Emulation is not validated.' }
    $taskHostFiles = @($taskManifest.host.files); $taskHostSources = @($taskManifest.host.sources)
    if (-not $taskHostFiles.Count -or $taskHostFiles.Count -gt 2048 -or -not $taskHostSources.Count -or $taskHostSources.Count -gt 256) { throw 'The host release manifest is incomplete.' }
    $taskVerifiedHostPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($taskFile in $taskHostFiles) {
        if ($taskFile.path -notlike 'host/*' -or -not $taskVerifiedHostPaths.Add([string]$taskFile.path)) { throw 'Invalid or duplicate host release entry.' }
        Assert-TaskHash $taskFile.path $taskFile.sha256 $taskFile.size
    }
    foreach ($taskRequired in @('host/ProperNotes.Host.exe','host/ProperNotes.Host.dll','host/ProperNotes.Host.deps.json','host/ProperNotes.Host.runtimeconfig.json','host/coreclr.dll','host/Microsoft.Web.WebView2.Core.dll','host/Microsoft.Web.WebView2.WinForms.dll')) {
        if (-not $taskVerifiedHostPaths.Contains($taskRequired)) { throw ('Required published host file is missing: ' + $taskRequired) }
    }
    $taskHostSourceRoot = Assert-TaskPath 'windows-host' -Directory
    $taskActualSources = @(Get-ChildItem -LiteralPath $taskHostSourceRoot -Recurse -File | Where-Object { $_.FullName.Substring($taskHostSourceRoot.TrimEnd('\').Length + 1) -notmatch '(^|[\\/])(bin|obj)([\\/]|$)' -and $_.Extension -in @('.cs','.csproj','.manifest','.json') })
    if ($taskActualSources.Count -ne $taskHostSources.Count) { throw 'The native host source set changed. Publish a new package before launching.' }
    $taskSourceNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($taskFile in $taskHostSources) {
        if ($taskFile.path -notlike 'windows-host/*' -or -not $taskSourceNames.Add([string]$taskFile.path)) { throw 'Invalid or duplicate host source entry.' }
        Assert-TaskHash $taskFile.path $taskFile.sha256 $taskFile.size
    }
    foreach ($taskFile in $taskActualSources) {
        $taskRelativeSource = $taskFile.FullName.Substring($taskRoot.TrimEnd('\').Length + 1).Replace('\','/')
        if (-not $taskSourceNames.Contains($taskRelativeSource)) { throw 'The native source set does not match the published host.' }
    }
    [string[]]$taskSortedSources = @($taskSourceNames)
    [Array]::Sort($taskSortedSources, [System.StringComparer]::Ordinal)
    $taskSourceHasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($taskRelativeSource in $taskSortedSources) {
            $taskNameBytes = [System.Text.Encoding]::UTF8.GetBytes($taskRelativeSource + [char]0)
            [void]$taskSourceHasher.TransformBlock($taskNameBytes,0,$taskNameBytes.Length,$taskNameBytes,0)
            $taskBytes = [System.IO.File]::ReadAllBytes((Get-TaskPath $taskRelativeSource))
            if ($taskBytes.Length) { [void]$taskSourceHasher.TransformBlock($taskBytes,0,$taskBytes.Length,$taskBytes,0) }
        }
        [void]$taskSourceHasher.TransformFinalBlock([byte[]]@(),0,0)
        $taskSourceDigest = ([BitConverter]::ToString($taskSourceHasher.Hash)).Replace('-','').ToLowerInvariant()
        if ($taskSourceDigest -ne $taskManifest.host.sourceHash) { throw 'The published native host source fingerprint does not match.' }
    } finally { $taskSourceHasher.Dispose() }
    Assert-TaskHash $taskManifest.node.path $taskManifest.node.sha256 $taskManifest.node.size
    $taskHost = Assert-TaskPath $taskManifest.host.path; $taskNode = Assert-TaskPath $taskManifest.node.path
    foreach ($taskDirectory in @('Logs','UserData','UserData/WebView2')) {
        $taskPath = Get-TaskPath $taskDirectory
        if (-not (Test-Path -LiteralPath $taskPath)) { [void][System.IO.Directory]::CreateDirectory($taskPath) }
        [void](Assert-TaskPath $taskDirectory -Directory)
    }
    $taskLog = Get-TaskPath ('Logs/startup-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N') + '.log')
    Write-TaskLog 'Portable native launch requested.'
    $taskIdentityBytes = [System.Text.Encoding]::UTF8.GetBytes(([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value + '|' + $taskRoot.ToLowerInvariant()))
    $taskHasher = [System.Security.Cryptography.SHA256]::Create()
    try { $taskLaunchIdentity = ([BitConverter]::ToString($taskHasher.ComputeHash($taskIdentityBytes))).Replace('-','').ToLowerInvariant() } finally { $taskHasher.Dispose() }
    $taskLaunchMutex = [System.Threading.Mutex]::new($false, ('Local\ProperNotes.Launch.' + $taskLaunchIdentity))
    try { $taskOwnsMutex = $taskLaunchMutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $taskOwnsMutex = $true }
    $taskActivation = Invoke-TaskHostCommand @('--activate','--root',$taskRoot)
    if ($taskActivation -eq 0) { Write-TaskLog 'Activated the existing native window.'; exit 0 }
    if ($taskActivation -eq 3 -or -not $taskOwnsMutex) { throw 'Another launch or native window is still active. No duplicate service was created.' }
    if ($taskActivation -ne 2) { throw ('The native host could not start (exit code ' + $taskActivation + '). Keep its complete published folder and inspect Logs/host.log.') }
    if ((Invoke-TaskHostCommand @('--check-runtime','--root',$taskRoot)) -ne 0) { throw 'The native runtime check failed. No runtime was installed and no browser was opened.' }
    $taskPrepare = Assert-TaskPath 'prepare.cjs'
    $taskPreparedOutput = & $taskNode $taskPrepare --ui-only 2>&1
    $taskPreparedExit = $LASTEXITCODE; Write-TaskLog ($taskPreparedOutput -join [Environment]::NewLine)
    if ($taskPreparedExit -ne 0) { throw 'Preparing the local UI failed. The native window was not opened.' }
    $taskBuildInfo = Get-Content -LiteralPath (Assert-TaskPath 'build-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($taskBuildInfo.app -ne 'inkpdf-local' -or $taskBuildInfo.instance -notmatch '^[a-f0-9]{64}$' -or $taskBuildInfo.revision -notmatch '^[a-f0-9]{64}$') { throw 'The local UI marker is invalid.' }
    $taskServerSource = Assert-TaskPath 'server.cjs'; $taskReady = $false
    for ($taskPort = 9150; $taskPort -le 9157; $taskPort++) {
        $taskOwnerId = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
        $taskInfo = New-TaskProcessInfo $taskNode @($taskServerSource,'--port',[string]$taskPort,'--owner-stdin','--owner-id',$taskOwnerId)
        $taskInfo.CreateNoWindow = $true; $taskInfo.RedirectStandardInput = $true; $taskInfo.RedirectStandardOutput = $true; $taskInfo.RedirectStandardError = $true
        $taskServer = [System.Diagnostics.Process]::Start($taskInfo)
        $taskServerOutput = $taskServer.StandardOutput.ReadToEndAsync(); $taskServerErrors = $taskServer.StandardError.ReadToEndAsync()
        $taskUrl = 'http://127.0.0.1:' + $taskPort
        for ($taskWait = 0; $taskWait -lt 35; $taskWait++) {
            if ($taskServer.HasExited) { break }
            if (Test-TaskService $taskUrl $taskOwnerId) { $taskReady = $true; break }
            Start-Sleep -Milliseconds 100
        }
        if ($taskReady) { break }
        Write-TaskLog ('Owned service was not ready on port ' + $taskPort + '. Existing services remain untouched.')
        Close-TaskService
    }
    if (-not $taskReady) { throw 'No local port from 9150 through 9157 was available for this window.' }
    Write-TaskLog ('Owned local service is ready on port ' + $taskPort + '.')
    $taskInfo = New-TaskProcessInfo $taskHost @('--url',$taskUrl,'--instance',[string]$taskBuildInfo.instance,'--revision',[string]$taskBuildInfo.revision,'--root',$taskRoot,'--user-data',(Get-TaskPath 'UserData/WebView2'))
    $taskWindow = [System.Diagnostics.Process]::Start($taskInfo)
    try { $taskWindow.WaitForExit(); if ($taskWindow.ExitCode -ne 0) { throw ('The native window exited with code ' + $taskWindow.ExitCode + '.') } } finally { $taskWindow.Dispose() }
    Write-TaskLog 'Native window closed.'
} catch {
    $taskExitCode = 1; $taskMessage = $_.Exception.Message
    try { Write-TaskLog $taskMessage } catch { }
    if ($taskLog) { $taskMessage += [Environment]::NewLine + [Environment]::NewLine + 'Startup log: ' + $taskLog }
    try { Add-Type -AssemblyName System.Windows.Forms; [void][System.Windows.Forms.MessageBox]::Show($taskMessage, 'ProperNotes for Windows', [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) }
    catch { [Console]::Error.WriteLine($taskMessage) }
} finally {
    try { Close-TaskService } catch { $taskExitCode = 1; try { Write-TaskLog ('Owned service cleanup: ' + $_.Exception.Message) } catch { } }
    if ($taskOwnsMutex -and $taskLaunchMutex) { $taskLaunchMutex.ReleaseMutex() }
    if ($taskLaunchMutex) { $taskLaunchMutex.Dispose() }
}
exit $taskExitCode
