[CmdletBinding()]
param()

# Cloud-only compiler entry. It restores project packages on an ephemeral
# GitHub-hosted Windows runner; it never installs an SDK or opens the app.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This entry only runs in an explicitly started GitHub-hosted Windows build. It does not restore dependencies on the local PC.'
}
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskSourceRoot = Join-Path $taskRepo 'windows-host'
$taskBuildRoot = Join-Path $env:RUNNER_TEMP ('propernotes-host-build-' + [Guid]::NewGuid().ToString('N'))
$taskArtifact = Join-Path $env:RUNNER_TEMP 'propernotes-host-artifact'
$taskCache = Join-Path $taskBuildRoot 'packages'
$taskBuildSources = Join-Path $taskBuildRoot 'windows-host'
$taskHostOutput = Join-Path $taskArtifact 'host'
$taskRid = 'win-x64'
$taskWebViewVersion = '1.0.4314.35'
$taskProjectNotices = @(
    @{ source='LICENSE'; target='LICENSE' }
    @{ source='LICENSE-WEBVIEW2-EXCEPTION'; target='LICENSE-WEBVIEW2-EXCEPTION' }
    @{ source='LICENSE-DOCS'; target='LICENSE-DOCS' }
    @{ source='release-docs/COPYRIGHT.md'; target='COPYRIGHT.md' }
    @{ source='release-docs/THIRD_PARTY_NOTICES.md'; target='THIRD_PARTY_NOTICES.md' }
    @{ source='release-docs/third-party-inventory.json'; target='third-party-inventory.json' }
    @{ source='licenses/CC-BY-SA-4.0.txt'; target='licenses/CC-BY-SA-4.0.txt' }
)

function Assert-TaskPlain {
    param([string]$Path, [switch]$Directory)
    $taskItem = Get-Item -LiteralPath $Path -Force
    if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $taskItem.PSIsContainer -ne [bool]$Directory) { throw ('Unexpected linked or invalid build content: ' + $Path) }
    return $taskItem
}
function Get-TaskSourcePaths {
    $taskPaths = [Collections.Generic.List[string]]::new()
    function Visit-TaskDirectory {
        param([string]$Directory)
        [void](Assert-TaskPlain $Directory -Directory)
        foreach ($taskItem in Get-ChildItem -LiteralPath $Directory -Force) {
            if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked host source is not supported.' }
            if ($taskItem.PSIsContainer) {
                if ($taskItem.Name -notin @('bin','obj','.git')) { Visit-TaskDirectory $taskItem.FullName }
            } elseif ($taskItem.Extension.ToLowerInvariant() -in @('.cs','.csproj','.manifest','.json')) {
                $taskPaths.Add($taskItem.FullName.Substring($taskRepo.TrimEnd('\').Length + 1).Replace('\','/'))
            }
        }
    }
    Visit-TaskDirectory $taskSourceRoot
    [string[]]$taskSorted = $taskPaths.ToArray()
    [Array]::Sort($taskSorted, [StringComparer]::Ordinal)
    return $taskSorted
}
function Get-TaskSourceHash {
    param([string[]]$Paths)
    $taskSHA = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($taskRelative in $Paths) {
            $taskNames = [Text.Encoding]::UTF8.GetBytes($taskRelative + [char]0)
            [void]$taskSHA.TransformBlock($taskNames,0,$taskNames.Length,$taskNames,0)
            $taskBytes = [IO.File]::ReadAllBytes((Join-Path $taskRepo $taskRelative))
            if ($taskBytes.Length) { [void]$taskSHA.TransformBlock($taskBytes,0,$taskBytes.Length,$taskBytes,0) }
        }
        [void]$taskSHA.TransformFinalBlock([byte[]]@(),0,0)
        return ([BitConverter]::ToString($taskSHA.Hash)).Replace('-','').ToLowerInvariant()
    } finally { $taskSHA.Dispose() }
}
function Get-TaskFileRecord {
    param([string]$Path, [string]$Root)
    $taskItem = Assert-TaskPlain $Path
    $taskBoundary = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $taskItem.FullName.StartsWith($taskBoundary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Artifact file lies outside its root.' }
    return [ordered]@{ path=$taskItem.FullName.Substring($taskBoundary.Length).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant(); size=$taskItem.Length }
}
function Get-TaskPEArchitecture {
    param([string]$Path)
    [void](Assert-TaskPlain $Path)
    $taskStream = [IO.File]::OpenRead($Path)
    $taskReader = [IO.BinaryReader]::new($taskStream)
    try {
        if ($taskStream.Length -lt 64 -or $taskReader.ReadUInt16() -ne 0x5A4D) { throw 'Invalid PE image.' }
        $taskStream.Position=0x3C; $taskOffset=$taskReader.ReadUInt32()
        if ($taskOffset -lt 64 -or $taskOffset -gt $taskStream.Length-24) { throw 'Invalid PE header offset.' }
        $taskStream.Position=$taskOffset
        if ($taskReader.ReadUInt32() -ne 0x00004550) { throw 'Invalid PE signature.' }
        return $taskReader.ReadUInt16()
    } finally { $taskReader.Dispose(); $taskStream.Dispose() }
}
function Write-TaskWebViewEvidence {
    # Inspect restored files only. Do not load SDK assemblies or fetch symbols/sources.
    $taskEvidence = [ordered]@{
        package='Microsoft.Web.WebView2'; version=$taskWebViewVersion
        scope='Restored package files only; no external source or symbol lookup.'
        nuspecStatus='unknown'
        repository=[ordered]@{ url='not specified'; commit='not specified'; branch='not specified' }
        license=[ordered]@{ type='not specified'; value='not specified'; url='not specified' }
        pdbFiles='unknown'; implementationSourceFiles='unknown'; interfaceSourceFiles='unknown'; sourceArchiveCandidates='unknown'; licenseFiles='unknown'
        sourceLink='unknown (not inspected)'; embeddedSource='unknown (not inspected)'; embeddedPdb='unknown (not inspected)'
    }
    function Get-TaskMetadataText {
        param([string]$Value)
        if ([string]::IsNullOrWhiteSpace($Value)) { return 'not specified' }
        $taskText = ($Value.Trim() -replace '[\x00-\x1f]', ' ')
        if ($taskText -match '(?i)(?:^[a-z]:[\\/]|^\\\\|/(?:Users|home)/)') { return 'redacted nonportable metadata' }
        $taskUri = $null
        if ([Uri]::TryCreate($taskText,[UriKind]::Absolute,[ref]$taskUri) -and $taskUri.Scheme -in @('http','https')) {
            if ($taskUri.UserInfo) { return 'redacted credential-bearing URL' }
            $taskText = $taskUri.GetLeftPart([UriPartial]::Path)
        }
        return $taskText.Substring(0,[Math]::Min(1024,$taskText.Length))
    }
    try {
        $taskSdkRoot = (Assert-TaskPlain (Join-Path $taskCache ('microsoft.web.webview2/' + $taskWebViewVersion)) -Directory).FullName
        $taskSdkFiles = @(Get-ChildItem -LiteralPath $taskSdkRoot -File -Recurse -Force)
        foreach ($taskSdkFile in $taskSdkFiles) { [void](Assert-TaskPlain $taskSdkFile.FullName) }
        $taskSpecs = @($taskSdkFiles | Where-Object { $_.DirectoryName -eq $taskSdkRoot -and $_.Extension -eq '.nuspec' })
        if ($taskSpecs.Count -eq 1) {
            $taskXmlSettings = [Xml.XmlReaderSettings]::new()
            $taskXmlSettings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
            $taskXmlSettings.XmlResolver = $null
            $taskXmlReader = [Xml.XmlReader]::Create($taskSpecs[0].FullName,$taskXmlSettings)
            try {
                $taskXml = [Xml.XmlDocument]::new(); $taskXml.XmlResolver = $null; $taskXml.Load($taskXmlReader)
            } finally { $taskXmlReader.Dispose() }
            $taskMetadata = $taskXml.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']")
            if ($null -eq $taskMetadata) { throw 'Package metadata element is missing.' }
            $taskRepository = $taskMetadata.SelectSingleNode("./*[local-name()='repository']")
            if ($null -ne $taskRepository) {
                foreach ($taskField in @('url','commit','branch')) { $taskEvidence.repository[$taskField] = Get-TaskMetadataText ($taskRepository.GetAttribute($taskField)) }
            }
            $taskLicense = $taskMetadata.SelectSingleNode("./*[local-name()='license']")
            if ($null -ne $taskLicense) {
                $taskEvidence.license.type = Get-TaskMetadataText ($taskLicense.GetAttribute('type'))
                $taskEvidence.license.value = Get-TaskMetadataText $taskLicense.InnerText
            }
            $taskLicenseUrl = $taskMetadata.SelectSingleNode("./*[local-name()='licenseUrl']")
            if ($null -ne $taskLicenseUrl) { $taskEvidence.license.url = Get-TaskMetadataText $taskLicenseUrl.InnerText }
            $taskEvidence.nuspecStatus = 'read from restored package'
        } else { $taskEvidence.nuspecStatus = 'unknown (expected one package-root nuspec)' }
        $taskGroups = [ordered]@{
            pdbFiles=@($taskSdkFiles | Where-Object { $_.Extension -eq '.pdb' })
            implementationSourceFiles=@($taskSdkFiles | Where-Object { $_.Extension -in @('.cs','.vb','.fs','.c','.cc','.cpp','.cxx','.rs') })
            interfaceSourceFiles=@($taskSdkFiles | Where-Object { $_.Extension -in @('.h','.hpp','.idl') })
            sourceArchiveCandidates=@($taskSdkFiles | Where-Object { $_.Name -match '(?i)(?:\.snupkg$|\.symbols\.nupkg$|sources?[^/\\]*\.(?:zip|nupkg|tgz|7z|tar(?:\.gz|\.xz)?)$)' })
        }
        foreach ($taskGroup in $taskGroups.Keys) {
            $taskEvidence[$taskGroup] = [ordered]@{ count=$taskGroups[$taskGroup].Count; samplePaths=@($taskGroups[$taskGroup] | Select-Object -First 6 | ForEach-Object { $_.FullName.Substring($taskSdkRoot.Length + 1).Replace('\','/') }) }
        }
        $taskEvidence.licenseFiles = @($taskSdkFiles | Where-Object { $_.Name -match '^(LICENSE|COPYING)(?:[._-].*)?$' } | ForEach-Object { Get-TaskFileRecord $_.FullName $taskSdkRoot })
    } catch { $taskEvidence['inspectionError'] = $_.Exception.GetType().Name }
    Write-Host ('WebView2 SDK metadata: ' + ($taskEvidence | ConvertTo-Json -Depth 6 -Compress))
}

foreach ($taskNoticeDirectory in @('release-docs','licenses')) {
    [void](Assert-TaskPlain (Join-Path $taskRepo $taskNoticeDirectory) -Directory)
}
foreach ($taskProjectNotice in $taskProjectNotices) {
    $taskNoticeInput = Assert-TaskPlain (Join-Path $taskRepo $taskProjectNotice.source)
    if ($taskNoticeInput.Length -eq 0) { throw ('Required project license/notice is empty: ' + $taskProjectNotice.source) }
}

$taskSdkPinPath = Join-Path $taskSourceRoot 'global.json'
[void](Assert-TaskPlain $taskSdkPinPath)
$taskSdkPin = [IO.File]::ReadAllText($taskSdkPinPath) | ConvertFrom-Json
$taskSDKVersion = [string]$taskSdkPin.sdk.version
if ($taskSDKVersion -notmatch '^10\.\d+\.\d+$' -or $taskSdkPin.sdk.rollForward -cne 'disable' -or $taskSdkPin.sdk.allowPrerelease -isnot [bool] -or $taskSdkPin.sdk.allowPrerelease) {
    throw 'windows-host/global.json must pin an exact stable .NET 10 SDK with rollForward=disable and allowPrerelease=false.'
}
$taskDotnet = Get-Command dotnet -CommandType Application -ErrorAction Stop | Select-Object -First 1
$taskSDKLines = & $taskDotnet.Source --list-sdks
if ($LASTEXITCODE -ne 0) { throw 'Could not inventory the preinstalled SDKs.' }
$taskPinnedSdkPattern = '^' + [Regex]::Escape($taskSDKVersion) + '\s+\['
$taskInstalledSdk = @($taskSDKLines | Where-Object { $_ -match $taskPinnedSdkPattern })
if ($taskInstalledSdk.Count -lt 1) { throw ('Pinned .NET SDK ' + $taskSDKVersion + ' is not preinstalled on this cloud image. No SDK was installed.') }
$taskPaths = @(Get-TaskSourcePaths)
if (-not $taskPaths.Count) { throw 'The cloud repository has no host source.' }
$taskSourceHash = Get-TaskSourceHash $taskPaths
if (Test-Path -LiteralPath $taskArtifact) { throw 'The artifact directory already exists; no existing data was replaced.' }
[void][IO.Directory]::CreateDirectory($taskBuildRoot)
[void][IO.Directory]::CreateDirectory($taskHostOutput)
foreach ($taskRelative in $taskPaths) {
    $taskDestination = Join-Path $taskBuildRoot $taskRelative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskDestination))
    Copy-Item -LiteralPath (Join-Path $taskRepo $taskRelative) -Destination $taskDestination
}
Copy-Item -LiteralPath $taskSdkPinPath -Destination (Join-Path $taskBuildRoot 'global.json')
$taskNuGetConfig = Join-Path $taskBuildRoot 'NuGet.Config'
[IO.File]::WriteAllText($taskNuGetConfig,@'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /><add key="nuget.org" value="https://api.nuget.org/v3/index.json" /></packageSources>
  <packageSourceMapping><packageSource key="nuget.org"><package pattern="*" /></packageSource></packageSourceMapping>
  <auditSources><clear /></auditSources>
</configuration>
'@,[Text.UTF8Encoding]::new($false))
$env:DOTNET_CLI_HOME = Join-Path $taskBuildRoot 'dotnet-home'
$env:NUGET_PACKAGES = $taskCache
$taskProject = Join-Path $taskBuildSources 'ProperNotes.Host.csproj'
Push-Location -LiteralPath $taskBuildRoot
try {
    & $taskDotnet.Source restore $taskProject --runtime $taskRid --configfile $taskNuGetConfig --packages $taskCache '-p:NuGetAudit=false' '-p:SelfContained=true'
    if ($LASTEXITCODE -ne 0) { throw 'Cloud project dependency restore failed. The application was not launched.' }
    Write-TaskWebViewEvidence
    & $taskDotnet.Source publish $taskProject --configuration Release --runtime $taskRid --self-contained true --no-restore --output $taskHostOutput '-p:PublishSingleFile=false' '-p:PublishTrimmed=false' '-p:NuGetAudit=false' ('-p:RestorePackagesPath=' + $taskCache)
    if ($LASTEXITCODE -ne 0) { throw 'Compiling the Windows host failed. No app or test was launched.' }
} finally { Pop-Location }
if ((Get-TaskSourceHash @(Get-TaskSourcePaths)) -ne $taskSourceHash) { throw 'Repository host sources changed during compilation.' }
foreach ($taskRequired in @('ProperNotes.Host.exe','ProperNotes.Host.dll','ProperNotes.Host.deps.json','ProperNotes.Host.runtimeconfig.json','Microsoft.Web.WebView2.Core.dll','Microsoft.Web.WebView2.WinForms.dll','coreclr.dll')) {
    [void](Assert-TaskPlain (Join-Path $taskHostOutput $taskRequired))
}
if ((Get-TaskPEArchitecture (Join-Path $taskHostOutput 'ProperNotes.Host.exe')) -ne 0x8664 -or (Get-TaskPEArchitecture (Join-Path $taskHostOutput 'coreclr.dll')) -ne 0x8664) { throw 'The host output is not Windows x64.' }
$taskMatchingLoader = $false
foreach ($taskLoader in Get-ChildItem -LiteralPath $taskHostOutput -File -Recurse -Filter WebView2Loader.dll) {
    if ((Get-TaskPEArchitecture $taskLoader.FullName) -eq 0x8664) { $taskMatchingLoader=$true }
}
if (-not $taskMatchingLoader) { throw 'The matching WebView2 Loader is missing.' }
foreach ($taskPackageName in @('microsoft.web.webview2','microsoft.netcore.app.runtime.win-x64','microsoft.windowsdesktop.app.runtime.win-x64')) {
    $taskPackageRoot = Join-Path $taskCache $taskPackageName
    [void](Assert-TaskPlain $taskPackageRoot -Directory)
    $taskNoticeFound = $false
    foreach ($taskVersion in Get-ChildItem -LiteralPath $taskPackageRoot -Directory) {
        if ($taskPackageName -eq 'microsoft.web.webview2' -and $taskVersion.Name -ne $taskWebViewVersion) { continue }
        foreach ($taskNotice in Get-ChildItem -LiteralPath $taskVersion.FullName -File) {
            if ($taskNotice.Name -notmatch '^(LICENSE|NOTICE|THIRD[-_ ]?PARTY|ThirdPartyNotices)') { continue }
            [void](Assert-TaskPlain $taskNotice.FullName)
            $taskDestination = Join-Path $taskArtifact ('licenses/host-runtime/' + $taskPackageName + '/' + $taskVersion.Name + '/' + $taskNotice.Name)
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskDestination))
            Copy-Item -LiteralPath $taskNotice.FullName -Destination $taskDestination
            $taskNoticeFound=$true
        }
    }
    if (-not $taskNoticeFound) { throw ('Required runtime license/notice is missing: ' + $taskPackageName) }
}
foreach ($taskProjectNotice in $taskProjectNotices) {
    $taskNoticeSource = Join-Path $taskRepo $taskProjectNotice.source
    [void](Assert-TaskPlain $taskNoticeSource)
    $taskNoticeDestination = Join-Path $taskArtifact ('licenses/project/' + $taskProjectNotice.target)
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskNoticeDestination))
    Copy-Item -LiteralPath $taskNoticeSource -Destination $taskNoticeDestination
    if ((Get-FileHash -LiteralPath $taskNoticeSource -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath $taskNoticeDestination -Algorithm SHA256).Hash) {
        throw ('Project notice copy differs from its source: ' + $taskProjectNotice.source)
    }
}
$taskFiles = @(Get-ChildItem -LiteralPath $taskArtifact -Recurse -Force | ForEach-Object {
    if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked artifact content is not supported.' }
    if (-not $_.PSIsContainer) { Get-TaskFileRecord $_.FullName $taskArtifact }
})
$taskSources = @($taskPaths | ForEach-Object { Get-TaskFileRecord (Join-Path $taskRepo $_) $taskRepo })
$taskMarker = [ordered]@{
    schemaVersion=1; app='propernotes-windows-host'; rid=$taskRid; webView2SdkVersion=$taskWebViewVersion
    sourceHash=$taskSourceHash; sources=$taskSources; files=$taskFiles
    builtAtUtc=[DateTime]::UtcNow.ToString('o'); sdkVersion=$taskSDKVersion; sourceCommit=$env:GITHUB_SHA
}
[IO.File]::WriteAllText((Join-Path $taskArtifact 'host-artifact.json'),($taskMarker | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Host ('Complete host artifact prepared: ' + $taskArtifact)
Write-Host 'Only compilation and packaging ran. The application, server and tests were not started.'
