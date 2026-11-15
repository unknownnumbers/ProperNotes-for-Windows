[CmdletBinding()]
param(
    [ValidateSet('win-x64', 'win-arm64')]
    [string]$RuntimeIdentifier = 'win-x64',
    [string]$NodePath,
    [string]$BuildNodePath,
    [string]$FixedRuntimePath,
    [string]$PackageCachePath,
    [string]$LocalFeedPath,
    [string]$HostArtifactPath,
    [string]$SourceRepositoryUrl,
    [string]$SourceCommit,
    [switch]$NoRestore
)

# Maintainer entry only. The normal launcher never invokes this script.
# No SDK/runtime installation, app launch, tests, or sandbox/ACL changes occur.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$taskExpectedArchitecture = if ($RuntimeIdentifier -eq 'win-arm64') { 'arm64' } else { 'x64' }
$taskOutputRoot = Join-Path $taskRoot 'output'
$taskBuildId = [Guid]::NewGuid().ToString('N')
$taskStage = Join-Path $taskOutputRoot ('propernotes-staging-' + $taskBuildId)
$taskFinal = Join-Path $taskOutputRoot ('ProperNotes-Windows-' + $RuntimeIdentifier + '-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + $taskBuildId.Substring(0, 8))
$taskEnvironmentBackup = @{}

function Assert-TaskDescendant {
    param([string]$Path, [string]$Parent)
    $taskAbsolute = [System.IO.Path]::GetFullPath($Path)
    $taskBoundary = [System.IO.Path]::GetFullPath($Parent).TrimEnd([char[]]'\/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $taskAbsolute.StartsWith($taskBoundary, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The output path is outside the portable output directory.'
    }
    return $taskAbsolute
}

function Assert-TaskPlainItem {
    param([string]$Path, [switch]$Directory)
    $taskItem = Get-Item -LiteralPath $Path -Force
    if (($taskItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw ('Linked files/directories are not copied into a portable release: ' + $Path)
    }
    if ($Directory -and -not $taskItem.PSIsContainer) { throw ('Expected a directory: ' + $Path) }
    if (-not $Directory -and $taskItem.PSIsContainer) { throw ('Expected a file: ' + $Path) }
    return $taskItem
}

function Assert-TaskDirectoryChain {
    param([string]$Path, [switch]$MustExist)
    $taskAbsolute = [System.IO.Path]::GetFullPath($Path)
    if ($MustExist -and -not (Test-Path -LiteralPath $taskAbsolute -PathType Container)) { throw ('Required directory is missing: ' + $Path) }
    $taskCurrent = $taskAbsolute
    while (-not [string]::IsNullOrEmpty($taskCurrent)) {
        $taskItem = $null
        try { $taskItem = Get-Item -LiteralPath $taskCurrent -Force -ErrorAction Stop }
        catch [System.Management.Automation.ItemNotFoundException] { }
        if ($null -ne $taskItem) {
            if (-not $taskItem.PSIsContainer -or ($taskItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw ('The output directory chain must contain only real directories, without links/junctions: ' + $taskCurrent)
            }
        }
        $taskParent = [System.IO.Path]::GetDirectoryName($taskCurrent)
        if ($taskParent -eq $taskCurrent) { break }
        $taskCurrent = $taskParent
    }
}

function Get-TaskPEArchitecture {
    param([string]$Path)
    $null = Assert-TaskPlainItem -Path $Path
    $taskStream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    $taskReader = New-Object System.IO.BinaryReader($taskStream)
    try {
        if ($taskStream.Length -lt 64 -or $taskReader.ReadUInt16() -ne 0x5A4D) { throw ('Invalid Windows executable: ' + $Path) }
        $taskStream.Position = 0x3C
        $taskPEOffset = $taskReader.ReadUInt32()
        if ($taskPEOffset -lt 64 -or $taskPEOffset -gt $taskStream.Length - 24) { throw ('Invalid PE header: ' + $Path) }
        $taskStream.Position = $taskPEOffset
        if ($taskReader.ReadUInt32() -ne 0x00004550) { throw ('Invalid PE signature: ' + $Path) }
        $taskMachine = $taskReader.ReadUInt16()
        if ($taskMachine -eq 0x8664) { return 'x64' }
        if ($taskMachine -eq 0xAA64) { return 'arm64' }
        throw ('Unsupported executable architecture: ' + $Path)
    } finally { $taskReader.Dispose(); $taskStream.Dispose() }
}

function Copy-TaskFile {
    param([string]$Source, [string]$Destination)
    $null = Assert-TaskPlainItem -Path $Source
    $taskDestinationPath = Assert-TaskDescendant -Path $Destination -Parent $taskStage
    $taskDestinationParent = [System.IO.Path]::GetDirectoryName($taskDestinationPath)
    $null = [System.IO.Directory]::CreateDirectory($taskDestinationParent)
    Copy-Item -LiteralPath $Source -Destination $taskDestinationPath -ErrorAction Stop
}

function Copy-TaskTree {
    param([string]$Source, [string]$Destination)
    $null = Assert-TaskPlainItem -Path $Source -Directory
    $taskDestinationPath = Assert-TaskDescendant -Path $Destination -Parent $taskStage
    $null = [System.IO.Directory]::CreateDirectory($taskDestinationPath)
    foreach ($taskChild in Get-ChildItem -LiteralPath $Source -Force) {
        if (($taskChild.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Linked release content is not supported: ' + $taskChild.FullName) }
        $taskChildDestination = Join-Path $taskDestinationPath $taskChild.Name
        if ($taskChild.PSIsContainer) { Copy-TaskTree -Source $taskChild.FullName -Destination $taskChildDestination }
        else { Copy-TaskFile -Source $taskChild.FullName -Destination $taskChildDestination }
    }
}

function Get-TaskRelativePath {
    param([string]$Path, [string]$Parent)
    $taskAbsolute = Assert-TaskDescendant -Path $Path -Parent $Parent
    return $taskAbsolute.Substring([System.IO.Path]::GetFullPath($Parent).TrimEnd([char[]]'\/').Length + 1).Replace('\', '/')
}

function Add-TaskReleaseSourceRecord {
    param(
        [string]$Path,
        [string]$SourcePath,
        [string]$LicenseExpression,
        [string]$Scope,
        [string[]]$NoticePaths,
        [string[]]$AdditionalPermissionPaths = @(),
        [string]$ComponentId = 'propernotes-original',
        [string[]]$GeneratedFrom = @()
    )
    $taskRecord = Get-TaskFileRecord -Path (Join-Path $taskStage $Path.Replace('/', '\')) -Root $taskStage
    $taskRecord['componentId'] = $ComponentId
    $taskRecord['licenseExpression'] = $LicenseExpression
    $taskRecord['scope'] = $Scope
    $taskRecord['noticePaths'] = @($NoticePaths)
    if ($AdditionalPermissionPaths.Count) {
        $taskRecord['additionalPermissionPaths'] = @($AdditionalPermissionPaths)
        $taskRecord['baseLicense'] = $LicenseExpression
    }
    if ($SourcePath) {
        $taskOriginal = Get-TaskFileRecord -Path (Join-Path $taskRoot $SourcePath.Replace('/', '\')) -Root $taskRoot
        if ($taskOriginal.sha256 -cne $taskRecord.sha256 -or $taskOriginal.size -ne $taskRecord.size) { throw ('A public source copy differs from its input: ' + $Path) }
        $taskRecord['sourcePath'] = $SourcePath
        $taskRecord['kind'] = 'copied-source'
    } else {
        $taskRecord['sourcePath'] = $null
        $taskRecord['kind'] = 'generated-original-program-output'
        $taskRecord['generatedFrom'] = @($GeneratedFrom)
    }
    $taskReleaseSourceRecords.Add($taskRecord)
}

function Get-TaskSourcePaths {
    param([string]$Root)
    $taskSourceRoot = Join-Path $Root 'windows-host'
    $null = Assert-TaskPlainItem -Path $taskSourceRoot -Directory
    $taskPaths = New-Object System.Collections.Generic.List[string]
    function Visit-TaskSourceDirectory {
        param([string]$Directory)
        foreach ($taskItem in Get-ChildItem -LiteralPath $Directory -Force) {
            if (($taskItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Linked host source is not supported: ' + $taskItem.FullName) }
            if ($taskItem.PSIsContainer) {
                if ($taskItem.Name -notin @('bin', 'obj', '.git')) { Visit-TaskSourceDirectory -Directory $taskItem.FullName }
            } elseif ($taskItem.Extension.ToLowerInvariant() -in @('.cs', '.csproj', '.manifest', '.json')) {
                $taskPaths.Add((Get-TaskRelativePath -Path $taskItem.FullName -Parent $Root))
            }
        }
    }
    Visit-TaskSourceDirectory -Directory $taskSourceRoot
    [string[]]$taskSortedPaths = $taskPaths.ToArray()
    [Array]::Sort($taskSortedPaths, [StringComparer]::Ordinal)
    return $taskSortedPaths
}

function Get-TaskSourceHash {
    param([string]$Root, [string[]]$Paths)
    $taskSHA = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($taskRelative in $Paths) {
            $taskNameBytes = [System.Text.Encoding]::UTF8.GetBytes($taskRelative + [char]0)
            $null = $taskSHA.TransformBlock($taskNameBytes, 0, $taskNameBytes.Length, $taskNameBytes, 0)
            $taskBytes = [System.IO.File]::ReadAllBytes((Join-Path $Root $taskRelative.Replace('/', '\')))
            if ($taskBytes.Length) { $null = $taskSHA.TransformBlock($taskBytes, 0, $taskBytes.Length, $taskBytes, 0) }
        }
        $null = $taskSHA.TransformFinalBlock([byte[]]@(), 0, 0)
        return ([BitConverter]::ToString($taskSHA.Hash)).Replace('-', '').ToLowerInvariant()
    } finally { $taskSHA.Dispose() }
}

function Get-TaskFileRecord {
    param([string]$Path, [string]$Root)
    $taskItem = Assert-TaskPlainItem -Path $Path
    return [ordered]@{ path = Get-TaskRelativePath -Path $taskItem.FullName -Parent $Root; sha256 = (Get-FileHash -LiteralPath $taskItem.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); size = $taskItem.Length }
}

function Copy-TaskHostNotices {
    param([string]$PackageCache)
    $taskPackageNames = @('microsoft.web.webview2', ('microsoft.netcore.app.runtime.' + $RuntimeIdentifier), ('microsoft.windowsdesktop.app.runtime.' + $RuntimeIdentifier))
    $taskWebViewNoticeFound = $false
    foreach ($taskPackageName in $taskPackageNames) {
        $taskPackageRoot = Join-Path $PackageCache $taskPackageName
        if (-not (Test-Path -LiteralPath $taskPackageRoot -PathType Container)) { continue }
        foreach ($taskVersion in Get-ChildItem -LiteralPath $taskPackageRoot -Directory) {
            foreach ($taskNotice in Get-ChildItem -LiteralPath $taskVersion.FullName -File) {
                if ($taskNotice.Name -notmatch '^(LICENSE|NOTICE|THIRD[-_ ]?PARTY|ThirdPartyNotices)') { continue }
                $taskNoticeDestination = Join-Path $taskStage ('licenses\host-runtime\' + $taskPackageName + '\' + $taskVersion.Name + '\' + $taskNotice.Name)
                Copy-TaskFile -Source $taskNotice.FullName -Destination $taskNoticeDestination
                if ($taskPackageName -eq 'microsoft.web.webview2' -and $taskVersion.Name -eq '1.0.4314.35') { $taskWebViewNoticeFound = $true }
            }
        }
    }
    if (-not $taskWebViewNoticeFound) { throw 'The pinned WebView2 SDK license/notice was not found in the build package cache. The portable folder was not published.' }
}

function Resolve-TaskExistingLocalDirectory {
    param([string]$Path, [string]$Description)
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path -match '^[A-Za-z][A-Za-z0-9+.-]*://' -or $Path.StartsWith('\\', [StringComparison]::Ordinal)) {
        throw ($Description + ' must be an existing local directory, not a URL or UNC path.')
    }
    $taskLocal = if ([System.IO.Path]::IsPathRooted($Path)) { [System.IO.Path]::GetFullPath($Path) } else { [System.IO.Path]::GetFullPath((Join-Path $taskRoot $Path)) }
    if ($taskLocal.StartsWith('\\', [StringComparison]::Ordinal) -or ([System.IO.DriveInfo]::new([System.IO.Path]::GetPathRoot($taskLocal))).DriveType -eq [System.IO.DriveType]::Network) {
        throw ($Description + ' must not use a UNC path or mapped network drive.')
    }
    Assert-TaskDirectoryChain -Path $taskLocal -MustExist
    return (Assert-TaskPlainItem -Path $taskLocal -Directory).FullName
}

function Set-TaskProcessEnvironment {
    param([string]$Name, [string]$Value)
    if (-not $taskEnvironmentBackup.ContainsKey($Name)) { $taskEnvironmentBackup[$Name] = [Environment]::GetEnvironmentVariable($Name, [EnvironmentVariableTarget]::Process) }
    [Environment]::SetEnvironmentVariable($Name, $Value, [EnvironmentVariableTarget]::Process)
}

function Write-TaskOfflineNuGetConfig {
    param([string]$Path, [string]$Cache, [string]$Feed)
    # An explicit --configfile uses only this configuration. In particular, no
    # user/machine NuGet HTTP sources or credentials are inherited.
    $taskXML = [System.Xml.XmlDocument]::new()
    [void]$taskXML.AppendChild($taskXML.CreateXmlDeclaration('1.0', 'utf-8', $null))
    $taskConfiguration = $taskXML.CreateElement('configuration'); [void]$taskXML.AppendChild($taskConfiguration)
    $taskSources = $taskXML.CreateElement('packageSources'); [void]$taskConfiguration.AppendChild($taskSources)
    [void]$taskSources.AppendChild($taskXML.CreateElement('clear'))
    if (-not [string]::IsNullOrEmpty($Feed)) {
        $taskSource = $taskXML.CreateElement('add'); $taskSource.SetAttribute('key', 'ProperNotesLocal'); $taskSource.SetAttribute('value', $Feed); [void]$taskSources.AppendChild($taskSource)
    }
    $taskAudit = $taskXML.CreateElement('auditSources'); [void]$taskConfiguration.AppendChild($taskAudit); [void]$taskAudit.AppendChild($taskXML.CreateElement('clear'))
    $taskConfig = $taskXML.CreateElement('config'); [void]$taskConfiguration.AppendChild($taskConfig)
    $taskPackages = $taskXML.CreateElement('add'); $taskPackages.SetAttribute('key', 'globalPackagesFolder'); $taskPackages.SetAttribute('value', $Cache); [void]$taskConfig.AppendChild($taskPackages)
    $taskWriterSettings = [System.Xml.XmlWriterSettings]::new(); $taskWriterSettings.Encoding = [System.Text.UTF8Encoding]::new($false); $taskWriterSettings.Indent = $true
    $taskWriter = [System.Xml.XmlWriter]::Create($Path, $taskWriterSettings)
    try { $taskXML.Save($taskWriter) } finally { $taskWriter.Dispose() }
}

function Get-TaskArtifactFilePath {
    param([string]$Root, $Relative, [ValidateSet('payload','source')][string]$Kind)
    if ($Relative -isnot [string] -or $Relative.Length -gt 1024 -or $Relative -match '[\\<>:"|?*\x00-\x1f]') { throw 'An artifact file path is invalid.' }
    $taskParts = $Relative.Split('/')
    $taskAllowedPrefix = if ($Kind -eq 'source') { @('windows-host') } else { @('host','licenses') }
    if ($taskParts.Length -lt 2 -or $taskParts[0] -cnotin $taskAllowedPrefix) { throw 'An artifact file is outside its allowed source/payload directory.' }
    foreach ($taskPart in $taskParts) {
        if ([string]::IsNullOrWhiteSpace($taskPart) -or $taskPart -in @('.','..') -or $taskPart.EndsWith('.') -or $taskPart.EndsWith(' ') -or
            $taskPart -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'An artifact path contains an unsafe or non-canonical component.' }
    }
    return (Assert-TaskDescendant -Path (Join-Path $Root $Relative.Replace('/','\')) -Parent $Root)
}

function Assert-TaskArtifactRecord {
    param([string]$Root, $Record, [ValidateSet('payload','source')][string]$Kind)
    if ($null -eq $Record -or $Record -isnot [System.Management.Automation.PSCustomObject] -or
        $Record.sha256 -isnot [string] -or $Record.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
        ($Record.size -isnot [int] -and $Record.size -isnot [long]) -or $Record.size -lt 0 -or
        $Record.size -gt $(if ($Kind -eq 'source') { 4MB } else { 512MB })) { throw 'An artifact size or checksum record is invalid.' }
    $taskFile = Get-TaskArtifactFilePath -Root $Root -Relative $Record.path -Kind $Kind
    Assert-TaskDirectoryChain -Path ([System.IO.Path]::GetDirectoryName($taskFile)) -MustExist
    $taskItem = Assert-TaskPlainItem -Path $taskFile
    if ($taskItem.Length -ne $Record.size -or (Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Record.sha256) {
        throw ('An artifact file does not match its size/checksum: ' + $Record.path)
    }
}

function Read-TaskHostArtifact {
    param([string]$Root, [string[]]$SourcePaths, [string]$SourceHash)
    $taskManifestFile = Join-Path $Root 'host-artifact.json'
    $taskManifestItem = Assert-TaskPlainItem -Path $taskManifestFile
    if ($taskManifestItem.Length -gt 8MB) { throw 'The host artifact manifest is too large.' }
    $taskManifest = Get-Content -LiteralPath $taskManifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($taskManifest -isnot [System.Management.Automation.PSCustomObject] -or $taskManifest.schemaVersion -ne 1 -or
        $taskManifest.app -cne 'propernotes-windows-host' -or $taskManifest.rid -cne $RuntimeIdentifier -or
        $taskManifest.webView2SdkVersion -cne '1.0.4314.35' -or $taskManifest.sourceHash -cne $SourceHash -or
        $taskManifest.sources -isnot [System.Array] -or $taskManifest.files -isnot [System.Array]) { throw 'The host artifact identity, architecture or source revision does not match.' }
    if ($taskManifest.sources.Count -lt 1 -or $taskManifest.sources.Count -gt 256 -or $taskManifest.sources.Count -ne $SourcePaths.Length -or
        $taskManifest.files.Count -lt 1 -or $taskManifest.files.Count -gt 4096) { throw 'The host artifact inventory exceeds its allowed range or differs from local sources.' }
    $taskExpectedSources = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($taskRelative in $SourcePaths) { [void]$taskExpectedSources.Add($taskRelative) }
    $taskSeenSources = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($taskRecord in $taskManifest.sources) {
        if (-not $taskExpectedSources.Contains([string]$taskRecord.path) -or -not $taskSeenSources.Add([string]$taskRecord.path)) { throw 'The host artifact source inventory is incomplete or duplicated.' }
        Assert-TaskArtifactRecord -Root $taskRoot -Record $taskRecord -Kind source
    }
    $taskExpectedFiles = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $taskPayloadBytes = [long]0
    foreach ($taskRecord in $taskManifest.files) {
        $null = Get-TaskArtifactFilePath -Root $Root -Relative $taskRecord.path -Kind payload
        if ($taskExpectedFiles.ContainsKey($taskRecord.path)) { throw 'The host artifact contains duplicate payload paths.' }
        Assert-TaskArtifactRecord -Root $Root -Record $taskRecord -Kind payload
        $taskExpectedFiles.Add($taskRecord.path, $taskRecord)
        $taskPayloadBytes += [long]$taskRecord.size
        if ($taskPayloadBytes -gt 2GB) { throw 'The host artifact payload exceeds 2 GiB.' }
    }
    $taskActualFiles = [System.Collections.Generic.List[string]]::new()
    function Visit-TaskArtifactDirectory {
        param([string]$Directory, [string]$Relative)
        foreach ($taskItem in Get-ChildItem -LiteralPath $Directory -Force) {
            if (($taskItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Host artifacts must not contain links or junctions.' }
            $taskRelative = if ($Relative) { $Relative + '/' + $taskItem.Name } else { $taskItem.Name }
            if ($taskItem.PSIsContainer) {
                if (-not $Relative -and $taskItem.Name -cnotin @('host','licenses')) { throw 'The host artifact contains an unexpected top-level directory.' }
                Visit-TaskArtifactDirectory -Directory $taskItem.FullName -Relative $taskRelative
            } elseif ($taskRelative -ceq 'host-artifact.json') { continue }
            else {
                if ($taskActualFiles.Count -ge 4096 -or -not $taskExpectedFiles.ContainsKey($taskRelative) -or $taskExpectedFiles[$taskRelative].path -cne $taskRelative) { throw 'The host artifact has an unexpected or non-canonical payload file.' }
                $taskActualFiles.Add($taskRelative)
            }
        }
    }
    Visit-TaskArtifactDirectory -Directory $Root -Relative ''
    if ($taskActualFiles.Count -ne $taskExpectedFiles.Count) { throw 'The host artifact payload inventory is incomplete.' }
    foreach ($taskRequired in @('host/ProperNotes.Host.exe','host/ProperNotes.Host.dll','host/ProperNotes.Host.deps.json','host/ProperNotes.Host.runtimeconfig.json','host/Microsoft.Web.WebView2.Core.dll','host/Microsoft.Web.WebView2.WinForms.dll','host/coreclr.dll')) {
        if (-not $taskExpectedFiles.ContainsKey($taskRequired)) { throw ('The host artifact is missing a required runtime file: ' + $taskRequired) }
    }
    foreach ($taskFamily in @('microsoft.web.webview2/1.0.4314.35',('microsoft.netcore.app.runtime.' + $RuntimeIdentifier),('microsoft.windowsdesktop.app.runtime.' + $RuntimeIdentifier))) {
        $taskPrefix = 'licenses/host-runtime/' + $taskFamily + '/'
        $taskFoundNotice = @($taskActualFiles | Where-Object { $_.StartsWith($taskPrefix, [StringComparison]::Ordinal) -and [System.IO.Path]::GetFileName($_) -match '^(LICENSE|NOTICE|THIRD[-_ ]?PARTY|ThirdPartyNotices)' })
        if (-not $taskFoundNotice.Count) { throw ('The host artifact is missing package license/notice records: ' + $taskFamily) }
    }
    return [pscustomobject]@{ Root = $Root; Files = @($taskManifest.files); Manifest = $taskManifest }
}

try {
$taskStage = Assert-TaskDescendant -Path $taskStage -Parent $taskOutputRoot
$taskFinal = Assert-TaskDescendant -Path $taskFinal -Parent $taskOutputRoot
if ($env:OS -ne 'Windows_NT' -or $taskRoot.StartsWith('\\', [StringComparison]::Ordinal)) { throw 'Build the portable package from a local Windows folder.' }
$taskReleaseDocuments = @(
    @{ Source = 'release-docs\README.md'; Destination = 'README.md' },
    @{ Source = 'release-docs\PORTABLE.md'; Destination = 'PORTABLE.md' },
    @{ Source = 'release-docs\COPYRIGHT.md'; Destination = 'COPYRIGHT.md' },
    @{ Source = 'release-docs\THIRD_PARTY_NOTICES.md'; Destination = 'THIRD_PARTY_NOTICES.md' },
    @{ Source = 'release-docs\third-party-inventory.json'; Destination = 'third-party-inventory.json' },
    @{ Source = 'release-docs\RELEASE-METADATA.md'; Destination = 'RELEASE-METADATA.md' },
    @{ Source = 'LICENSE'; Destination = 'LICENSE' },
    @{ Source = 'LICENSE-DOCS'; Destination = 'LICENSE-DOCS' },
    @{ Source = 'LICENSE-WEBVIEW2-EXCEPTION'; Destination = 'LICENSE-WEBVIEW2-EXCEPTION' }
)
foreach ($taskDocument in $taskReleaseDocuments) {
    $taskDocumentSource = Join-Path $taskRoot $taskDocument.Source
    if (-not (Test-Path -LiteralPath $taskDocumentSource -PathType Leaf)) { throw ('Required public release document is missing: ' + $taskDocument.Source) }
    $null = Assert-TaskPlainItem -Path $taskDocumentSource
}
$null = Assert-TaskDescendant -Path $taskOutputRoot -Parent $taskRoot
Assert-TaskDirectoryChain -Path $taskRoot -MustExist
Assert-TaskDirectoryChain -Path $taskOutputRoot
if (Test-Path -LiteralPath $taskStage) { throw 'The staging directory already exists; no files were replaced.' }
if (Test-Path -LiteralPath $taskFinal) { throw 'The output directory already exists; no files were replaced.' }
$taskUseHostArtifact = -not [string]::IsNullOrWhiteSpace($HostArtifactPath)
$taskArtifact = $null
if ($taskUseHostArtifact -and ($NoRestore -or -not [string]::IsNullOrWhiteSpace($PackageCachePath) -or -not [string]::IsNullOrWhiteSpace($LocalFeedPath))) {
    throw '-HostArtifactPath skips SDK/restore/publish and cannot be combined with cache/feed/-NoRestore options.'
}
if ($taskUseHostArtifact) {
    $taskArtifactRoot = Resolve-TaskExistingLocalDirectory -Path $HostArtifactPath -Description 'HostArtifactPath'
} else {
$taskBuildRoot = Assert-TaskDescendant -Path (Join-Path $taskRoot '.build') -Parent $taskRoot
Assert-TaskDirectoryChain -Path $taskBuildRoot
$taskCacheIsDefault = [string]::IsNullOrWhiteSpace($PackageCachePath)
if ($taskCacheIsDefault) {
    $taskCache = Assert-TaskDescendant -Path (Join-Path $taskBuildRoot 'NuGetPackages') -Parent $taskBuildRoot
    Assert-TaskDirectoryChain -Path $taskCache
} else { $taskCache = Resolve-TaskExistingLocalDirectory -Path $PackageCachePath -Description 'PackageCachePath' }
$taskLocalFeed = if ([string]::IsNullOrWhiteSpace($LocalFeedPath)) { $null } else { Resolve-TaskExistingLocalDirectory -Path $LocalFeedPath -Description 'LocalFeedPath' }
$taskNuGetConfig = Assert-TaskDescendant -Path (Join-Path $taskBuildRoot ('NuGet-offline-' + $taskBuildId + '.Config')) -Parent $taskBuildRoot
Assert-TaskDirectoryChain -Path (Join-Path $taskBuildRoot 'DotNetHome')
Assert-TaskDirectoryChain -Path (Join-Path $taskBuildRoot 'NuGetHttpCache')
# These overrides apply to this build process and its children only, then are
# restored even when SDK preflight fails. Do not consult ambient NuGet sources.
Set-TaskProcessEnvironment -Name 'DOTNET_CLI_HOME' -Value (Join-Path $taskBuildRoot 'DotNetHome')
Set-TaskProcessEnvironment -Name 'DOTNET_CLI_TELEMETRY_OPTOUT' -Value '1'
Set-TaskProcessEnvironment -Name 'DOTNET_NOLOGO' -Value '1'
Set-TaskProcessEnvironment -Name 'DOTNET_SKIP_FIRST_TIME_EXPERIENCE' -Value '1'
Set-TaskProcessEnvironment -Name 'DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE' -Value 'true'
Set-TaskProcessEnvironment -Name 'NUGET_PACKAGES' -Value $taskCache
Set-TaskProcessEnvironment -Name 'NUGET_HTTP_CACHE_PATH' -Value (Join-Path $taskBuildRoot 'NuGetHttpCache')
Set-TaskProcessEnvironment -Name 'NUGET_CERT_REVOCATION_MODE' -Value 'offline'
$taskSDKPinPath = Join-Path $taskRoot 'windows-host\global.json'
$null = Assert-TaskPlainItem -Path $taskSDKPinPath
$taskSDKPin = Get-Content -LiteralPath $taskSDKPinPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskSDKPin.sdk.version -cne '10.0.403' -or $taskSDKPin.sdk.rollForward -cne 'disable' -or
    $taskSDKPin.sdk.allowPrerelease -isnot [bool] -or $taskSDKPin.sdk.allowPrerelease -ne $false) {
    throw 'windows-host/global.json must pin SDK 10.0.403 with rollForward disable and allowPrerelease false. No SDK installation was attempted.'
}
$taskRequiredSDK = $taskSDKPin.sdk.version
$taskDotnet = Get-Command dotnet -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $taskDotnet) { throw ('The existing pinned .NET SDK ' + $taskRequiredSDK + ' is required. No SDK installation was attempted.') }
$taskSDKs = & $taskDotnet.Source --list-sdks
if ($LASTEXITCODE -ne 0 -or -not ($taskSDKs | Where-Object { $_ -cmatch ('^' + [regex]::Escape($taskRequiredSDK) + '\s+\[') })) {
    throw ('The exact pinned .NET SDK ' + $taskRequiredSDK + ' must already be installed. Other .NET 10 SDK versions are not substituted. No SDK installation was attempted.')
}
}
if ([string]::IsNullOrWhiteSpace($NodePath)) { $NodePath = Join-Path $taskRoot 'runtime\node.exe' }
$taskNode = (Assert-TaskPlainItem -Path $NodePath).FullName
if ((Get-TaskPEArchitecture -Path $taskNode) -ne $taskExpectedArchitecture) { throw ('The selected Node executable does not match ' + $RuntimeIdentifier + '. Provide -NodePath for a matching already available runtime.') }
if ([string]::IsNullOrWhiteSpace($BuildNodePath)) { $BuildNodePath = Join-Path $taskRoot 'runtime\node.exe' }
$taskBuildNode = (Assert-TaskPlainItem -Path $BuildNodePath).FullName
$null = Get-TaskPEArchitecture -Path $taskBuildNode
if (-not [string]::IsNullOrWhiteSpace($FixedRuntimePath)) {
    $taskFixed = (Assert-TaskPlainItem -Path $FixedRuntimePath -Directory).FullName
    if ($taskFixed.StartsWith('\\', [StringComparison]::Ordinal)) { throw 'WebView2 Fixed Version is not supported from a UNC path.' }
    if ((Get-TaskPEArchitecture -Path (Join-Path $taskFixed 'msedgewebview2.exe')) -ne $taskExpectedArchitecture) { throw 'The supplied WebView2 Fixed Version does not match the requested architecture.' }
} else { $taskFixed = $null }

$taskHostSourcePaths = @(Get-TaskSourcePaths -Root $taskRoot)
if (-not $taskHostSourcePaths.Count) { throw 'Windows host sources are missing.' }
$taskHostSourceHash = Get-TaskSourceHash -Root $taskRoot -Paths $taskHostSourcePaths
if ($taskUseHostArtifact) {
    $taskArtifact = Read-TaskHostArtifact -Root $taskArtifactRoot -SourcePaths $taskHostSourcePaths -SourceHash $taskHostSourceHash
} else {
if ($NoRestore -and -not (Test-Path -LiteralPath (Join-Path $taskRoot 'windows-host\obj\project.assets.json') -PathType Leaf)) {
    throw '-NoRestore requires existing project.assets.json restored for this RID and package cache. No online restore was attempted.'
}
Assert-TaskDirectoryChain -Path $taskBuildRoot
$null = [System.IO.Directory]::CreateDirectory($taskBuildRoot)
Assert-TaskDirectoryChain -Path $taskBuildRoot -MustExist
if ($taskCacheIsDefault) { $null = [System.IO.Directory]::CreateDirectory($taskCache) }
Assert-TaskDirectoryChain -Path $taskCache -MustExist
if (Test-Path -LiteralPath $taskNuGetConfig) { throw 'The unique offline NuGet configuration path already exists; no file was replaced.' }
Write-TaskOfflineNuGetConfig -Path $taskNuGetConfig -Cache $taskCache -Feed $taskLocalFeed
}
Assert-TaskDirectoryChain -Path $taskOutputRoot
$null = [System.IO.Directory]::CreateDirectory($taskStage)
Assert-TaskDirectoryChain -Path $taskOutputRoot -MustExist
Assert-TaskDirectoryChain -Path $taskStage -MustExist
Write-Host ('Building a fresh portable folder: ' + $taskFinal)
Write-Host 'This maintainer command publishes source; it does not open the application or install a runtime.'
if ($taskUseHostArtifact) { Write-Host ('Using verified host artifact; SDK lookup/restore/publish are skipped: ' + $taskArtifact.Root) }
else {
    Write-Host ('Offline NuGet configuration: ' + $taskNuGetConfig)
    Write-Host ('Existing package cache: ' + $taskCache)
    if ($taskLocalFeed) { Write-Host ('Existing local package feed: ' + $taskLocalFeed) }
}

try {
    $taskHostOutput = Join-Path $taskStage 'host'
    if ($taskUseHostArtifact) {
        foreach ($taskRecord in $taskArtifact.Files) {
            Copy-TaskFile -Source (Get-TaskArtifactFilePath -Root $taskArtifact.Root -Relative $taskRecord.path -Kind payload) -Destination (Join-Path $taskStage $taskRecord.path.Replace('/','\'))
            Assert-TaskArtifactRecord -Root $taskStage -Record $taskRecord -Kind payload
        }
    } else {
    $taskProject = Join-Path $taskRoot 'windows-host\ProperNotes.Host.csproj'
    $taskPublishArgs = @('publish', $taskProject, '--no-restore', '--configuration', 'Release', '--runtime', $RuntimeIdentifier, '--self-contained', 'true', '--output', $taskHostOutput, '-p:PublishSingleFile=false', '-p:PublishTrimmed=false', '-p:NuGetAudit=false', ('-p:RestorePackagesPath=' + $taskCache), ('-p:RestoreConfigFile=' + $taskNuGetConfig))
    Push-Location -LiteralPath (Join-Path $taskRoot 'windows-host')
    try {
        if (-not $NoRestore) {
            $taskRestoreArgs = @('restore', $taskProject, '--runtime', $RuntimeIdentifier, '--packages', $taskCache, '--configfile', $taskNuGetConfig, '-p:SelfContained=true', '-p:NuGetAudit=false')
            & $taskDotnet.Source @taskRestoreArgs
            if ($LASTEXITCODE -ne 0) { throw 'Offline restore could not resolve the required existing packages. Supply an already available cache/local feed; no HTTP source or dependency installation was used.' }
        }
        & $taskDotnet.Source @taskPublishArgs; $taskPublishExit = $LASTEXITCODE
    }
    finally { Pop-Location }
    if ($taskPublishExit -ne 0) { throw 'Publishing the Windows host failed. No release folder was published.' }
    }
    if ((Get-TaskSourceHash -Root $taskRoot -Paths @(Get-TaskSourcePaths -Root $taskRoot)) -ne $taskHostSourceHash) { throw 'Host sources changed while building. No release folder was published; retry from stable sources.' }
    $taskHostExe = Join-Path $taskHostOutput 'ProperNotes.Host.exe'
    if ((Get-TaskPEArchitecture -Path $taskHostExe) -ne $taskExpectedArchitecture) { throw 'The published host architecture does not match the requested package.' }
    foreach ($taskRequiredHost in @('ProperNotes.Host.dll', 'ProperNotes.Host.deps.json', 'ProperNotes.Host.runtimeconfig.json', 'Microsoft.Web.WebView2.Core.dll', 'Microsoft.Web.WebView2.WinForms.dll', 'coreclr.dll')) {
        $null = Assert-TaskPlainItem -Path (Join-Path $taskHostOutput $taskRequiredHost)
    }
    $taskLoaderFound = $false
    foreach ($taskLoader in Get-ChildItem -LiteralPath $taskHostOutput -Filter 'WebView2Loader.dll' -File -Recurse) {
        try { if ((Get-TaskPEArchitecture -Path $taskLoader.FullName) -eq $taskExpectedArchitecture) { $taskLoaderFound = $true } } catch { }
    }
    if (-not $taskLoaderFound) { throw 'The matching native WebView2Loader.dll is missing from the publish output.' }

    $taskSourceFiles = @('annotation-geometry.js', 'annotation-menu.js', 'app-main.js', 'backup-client.js', 'backup-store.cjs', 'browser-workspace.js', 'document-view.js', 'document-workspace.js', 'favourite-tools.js', 'file-api.js', 'host-window.js', 'ink-engine.js', 'input-controls.js', 'input-preferences.js', 'keyboard-shortcuts.js', 'native-files.cjs', 'pdf-bookmarks.js', 'pdf-storage.js', 'pdf-text-search.js', 'preferences.js', 'prepare.cjs', 'render-page.js', 'Select-ProperNotesDocument.ps1', 'server.cjs', 'text-appearance.js', 'text-selection.js', 'ui.template.html', 'workspace-store.cjs', 'Start-ProperNotes.cmd', 'Start-ProperNotes.ps1', 'Start-ProperNotesNative.ps1', 'Build-ProperNotesPortable.cmd', 'Build-ProperNotesPortable.ps1', 'New-ProperNotesReleaseMetadata.ps1', 'Export-ProperNotesSource.ps1')
    foreach ($taskFile in $taskSourceFiles) { Copy-TaskFile -Source (Join-Path $taskRoot $taskFile) -Destination (Join-Path $taskStage $taskFile) }
    foreach ($taskDocument in $taskReleaseDocuments) {
        Copy-TaskFile -Source (Join-Path $taskRoot $taskDocument.Source) -Destination (Join-Path $taskStage $taskDocument.Destination)
        if ($taskDocument.Source.StartsWith('release-docs\', [StringComparison]::Ordinal)) {
            Copy-TaskFile -Source (Join-Path $taskRoot $taskDocument.Source) -Destination (Join-Path $taskStage $taskDocument.Source)
        }
    }
    $taskOptionalSourceFiles = @('Start-InkPDF.cmd', 'Start-InkPDF.ps1', 'Start-ProperNotesBrowser.cmd', 'Start-ProperNotesBrowser.ps1')
    foreach ($taskOptionalFile in $taskOptionalSourceFiles) {
        if (Test-Path -LiteralPath (Join-Path $taskRoot $taskOptionalFile) -PathType Leaf) { Copy-TaskFile -Source (Join-Path $taskRoot $taskOptionalFile) -Destination (Join-Path $taskStage $taskOptionalFile) }
    }
    $taskCloudFiles = @('Build-HostArtifact.ps1','windows-portable.yml','CLOUD-BUILD.md','source-bytes.gitattributes')
    foreach ($taskCloudFile in $taskCloudFiles) {
        $taskCloudRelative = 'cloud-build\' + $taskCloudFile
        if (Test-Path -LiteralPath (Join-Path $taskRoot $taskCloudRelative) -PathType Leaf) { Copy-TaskFile -Source (Join-Path $taskRoot $taskCloudRelative) -Destination (Join-Path $taskStage $taskCloudRelative) }
    }
    foreach ($taskSourceFile in $taskHostSourcePaths) { Copy-TaskFile -Source (Join-Path $taskRoot $taskSourceFile.Replace('/', '\')) -Destination (Join-Path $taskStage $taskSourceFile.Replace('/', '\')) }
    if (Test-Path -LiteralPath (Join-Path $taskRoot 'windows-host\README.md') -PathType Leaf) { Copy-TaskFile -Source (Join-Path $taskRoot 'windows-host\README.md') -Destination (Join-Path $taskStage 'windows-host\README.md') }
    Copy-TaskTree -Source (Join-Path $taskRoot 'vendor') -Destination (Join-Path $taskStage 'vendor')
    Copy-TaskTree -Source (Join-Path $taskRoot 'licenses') -Destination (Join-Path $taskStage 'licenses')
    Copy-TaskFile -Source $taskNode -Destination (Join-Path $taskStage 'runtime\node.exe')
    if ($null -ne $taskFixed) { Copy-TaskTree -Source $taskFixed -Destination (Join-Path $taskStage 'runtime\WebView2') }
    if ($taskUseHostArtifact) {
        # A local license tree must not replace any verified cloud payload.
        foreach ($taskRecord in $taskArtifact.Files) { Assert-TaskArtifactRecord -Root $taskStage -Record $taskRecord -Kind payload }
    } else { Copy-TaskHostNotices -PackageCache $taskCache }

    if ((Get-TaskSourceHash -Root $taskStage -Paths @(Get-TaskSourcePaths -Root $taskStage)) -ne $taskHostSourceHash) { throw 'Copied host sources do not match the published host source marker.' }
    # Source assembly is a build step only. It never starts the HTTP service/UI.
    & $taskBuildNode (Join-Path $taskStage 'prepare.cjs') '--ui-only'
    if ($LASTEXITCODE -ne 0) { throw 'Preparing the release UI failed. No release folder was published.' }

    $taskAllFiles = @(Get-ChildItem -LiteralPath $taskStage -File -Recurse | ForEach-Object { Get-TaskFileRecord -Path $_.FullName -Root $taskStage })
    $taskHostFiles = @($taskAllFiles | Where-Object { $_.path.StartsWith('host/', [StringComparison]::Ordinal) })
    $taskSourceRecords = @($taskHostSourcePaths | ForEach-Object { Get-TaskFileRecord -Path (Join-Path $taskStage $_.Replace('/', '\')) -Root $taskStage })
    $taskManifest = [ordered]@{
        schemaVersion = 1
        app = 'propernotes-windows'
        rid = $RuntimeIdentifier
        builtAtUtc = [DateTime]::UtcNow.ToString('o')
        host = [ordered]@{ path = 'host/ProperNotes.Host.exe'; sourceHash = $taskHostSourceHash; sources = $taskSourceRecords; files = $taskHostFiles }
        node = Get-TaskFileRecord -Path (Join-Path $taskStage 'runtime\node.exe') -Root $taskStage
        webview2 = [ordered]@{ mode = $(if ($null -eq $taskFixed) { 'evergreen' } else { 'fixed' }); path = $(if ($null -eq $taskFixed) { $null } else { 'runtime/WebView2' }) }
        files = $taskAllFiles
    }
    $taskManifest.node['architecture'] = $taskExpectedArchitecture
    [System.IO.File]::WriteAllText((Join-Path $taskStage 'propernotes-portable.json'), ($taskManifest | ConvertTo-Json -Depth 10), (New-Object System.Text.UTF8Encoding($false)))
    # Only explicit, reviewed lists establish original-project licensing. Other
    # package files are mapped by third-party inventory rules, never by suffix.
    $taskReleaseSourceRecords = New-Object System.Collections.Generic.List[object]
    $taskCodeScope = 'Original ProperNotes code, configuration and program assets only, to the extent contributors hold the relevant rights. Third-party material retains its own terms.'
    $taskDocScope = 'Original explanatory wording only. Third-party quotations, license texts, component notices and factual records retain their own rights or carry no new copyright claim.'
    $taskCodeNotices = @('LICENSE', 'COPYRIGHT.md')
    $taskDocNotices = @('LICENSE-DOCS', 'COPYRIGHT.md', 'licenses/CC-BY-SA-4.0.txt')
    foreach ($taskFile in $taskSourceFiles) { Add-TaskReleaseSourceRecord -Path $taskFile -SourcePath $taskFile -LicenseExpression 'GPL-3.0-only' -Scope $taskCodeScope -NoticePaths $taskCodeNotices }
    foreach ($taskFile in $taskOptionalSourceFiles) {
        if (Test-Path -LiteralPath (Join-Path $taskStage $taskFile) -PathType Leaf) { Add-TaskReleaseSourceRecord -Path $taskFile -SourcePath $taskFile -LicenseExpression 'GPL-3.0-only' -Scope $taskCodeScope -NoticePaths $taskCodeNotices }
    }
    foreach ($taskDocument in $taskReleaseDocuments) {
        $taskLicense = 'CC-BY-SA-4.0'
        $taskScope = $taskDocScope
        $taskNotices = $taskDocNotices
        $taskComponentId = 'propernotes-original'
        if ($taskDocument.Source -ceq 'LICENSE') {
            $taskLicense = 'LicenseRef-GNU-GPL-3.0-License-Text'
            $taskScope = 'Verbatim GNU license text. Its own copying conditions and copyright notice apply; it is not relicensed as ProperNotes code or documentation.'
            $taskNotices = @('LICENSE')
            $taskComponentId = 'gnu-license-text'
        } elseif ($taskDocument.Source -ceq 'LICENSE-WEBVIEW2-EXCEPTION') {
            $taskLicense = 'LicenseRef-ProperNotes-WebView2-Additional-Permission'
            $taskScope = 'License grant text for the explicitly stated original Windows host scope only. This is not a project-wide exception or a relicensing of Microsoft components.'
            $taskNotices = @('LICENSE', 'LICENSE-WEBVIEW2-EXCEPTION', 'COPYRIGHT.md')
            $taskComponentId = 'propernotes-host-permission-text'
        }
        $taskSourceRelative = $taskDocument.Source.Replace('\', '/')
        Add-TaskReleaseSourceRecord -Path $taskDocument.Destination -SourcePath $taskSourceRelative -LicenseExpression $taskLicense -Scope $taskScope -NoticePaths $taskNotices -ComponentId $taskComponentId
        if ($taskDocument.Source.StartsWith('release-docs\', [StringComparison]::Ordinal)) { Add-TaskReleaseSourceRecord -Path $taskSourceRelative -SourcePath $taskSourceRelative -LicenseExpression $taskLicense -Scope $taskScope -NoticePaths $taskNotices }
    }
    foreach ($taskFile in $taskCloudFiles) {
        $taskRelative = 'cloud-build/' + $taskFile
        if (Test-Path -LiteralPath (Join-Path $taskStage $taskRelative.Replace('/', '\')) -PathType Leaf) {
            $taskIsDoc = $taskFile -ceq 'CLOUD-BUILD.md'
            Add-TaskReleaseSourceRecord -Path $taskRelative -SourcePath $taskRelative -LicenseExpression $(if ($taskIsDoc) { 'CC-BY-SA-4.0' } else { 'GPL-3.0-only' }) -Scope $(if ($taskIsDoc) { $taskDocScope } else { $taskCodeScope }) -NoticePaths $(if ($taskIsDoc) { $taskDocNotices } else { $taskCodeNotices })
        }
    }
    foreach ($taskRelative in $taskHostSourcePaths) { Add-TaskReleaseSourceRecord -Path $taskRelative -SourcePath $taskRelative -LicenseExpression 'GPL-3.0-only' -Scope $taskCodeScope -NoticePaths $taskCodeNotices -AdditionalPermissionPaths @('LICENSE-WEBVIEW2-EXCEPTION') }
    if (Test-Path -LiteralPath (Join-Path $taskStage 'windows-host\README.md') -PathType Leaf) { Add-TaskReleaseSourceRecord -Path 'windows-host/README.md' -SourcePath 'windows-host/README.md' -LicenseExpression 'CC-BY-SA-4.0' -Scope $taskDocScope -NoticePaths $taskDocNotices }
    foreach ($taskRelative in @('app.js', 'index.html', 'styles.css', 'build-info.json', 'propernotes-portable.json')) {
        Add-TaskReleaseSourceRecord -Path $taskRelative -LicenseExpression 'GPL-3.0-only' -Scope $taskCodeScope -NoticePaths $taskCodeNotices -GeneratedFrom @($taskSourceFiles + $taskHostSourcePaths)
    }
    $taskReleaseSourceInput = [ordered]@{
        schemaVersion = 1; recordType = 'propernotes-explicit-source-records'; pathBase = 'package_root'
        meaning = 'Explicit original source/document mappings and generated outputs selected by the builder. Hashes identify the actual copied bytes. This is not a blanket license for unmapped files, nor a claim that a separate source archive was published.'
        files = @($taskReleaseSourceRecords.ToArray())
    }
    [System.IO.File]::WriteAllText((Join-Path $taskStage 'source-records.json'), ($taskReleaseSourceInput | ConvertTo-Json -Depth 12), (New-Object System.Text.UTF8Encoding($false)))
    $taskMetadataArgs = @{
        PackageDirectory = $taskStage; SourceRepositoryUrl = $SourceRepositoryUrl; SourceCommit = $SourceCommit
        HostBuildMode = $(if ($taskUseHostArtifact) { 'artifact-import' } else { 'offline-publish' })
    }
    if ($taskUseHostArtifact) { $taskMetadataArgs['HostArtifactManifestPath'] = Join-Path $taskArtifact.Root 'host-artifact.json' }
    & (Join-Path $taskStage 'New-ProperNotesReleaseMetadata.ps1') @taskMetadataArgs
    # Both fully resolved targets were checked inside output before this move.
    # The final name is new: no old package/user data is overwritten or removed.
    $null = Assert-TaskDescendant -Path $taskStage -Parent $taskOutputRoot
    $null = Assert-TaskDescendant -Path $taskFinal -Parent $taskOutputRoot
    $null = Assert-TaskDescendant -Path $taskOutputRoot -Parent $taskRoot
    Assert-TaskDirectoryChain -Path $taskRoot -MustExist
    Assert-TaskDirectoryChain -Path $taskOutputRoot -MustExist
    Assert-TaskDirectoryChain -Path $taskStage -MustExist
    Assert-TaskDirectoryChain -Path ([System.IO.Path]::GetDirectoryName($taskFinal)) -MustExist
    if (Test-Path -LiteralPath $taskFinal) { throw 'The final directory appeared during the build; no replacement was attempted.' }
    Move-Item -LiteralPath $taskStage -Destination $taskFinal
    Write-Host ('Portable folder created: ' + $taskFinal)
    Write-Host 'No application was launched and no runtime/SDK was installed. Runtime and device acceptance remains separate.'
} catch {
    Write-Host ('Build did not publish a portable release. Staging was retained for inspection: ' + $taskStage)
    throw
}
} finally {
    foreach ($taskName in $taskEnvironmentBackup.Keys) { [Environment]::SetEnvironmentVariable($taskName, $taskEnvironmentBackup[$taskName], [EnvironmentVariableTarget]::Process) }
}
