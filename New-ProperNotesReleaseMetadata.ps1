[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [string]$SourceRecordsPath,
    [string]$ReleaseName = 'ProperNotes for Windows',
    [string]$SourceRepositoryUrl,
    [string]$SourceCommit,
    [string]$HostArtifactManifestPath,
    [ValidateSet('artifact-import', 'offline-publish', 'unspecified')]
    [string]$HostBuildMode = 'unspecified'
)

# This maintainer command only reads package inputs and writes release records.
# It does not install dependencies, start the application, or run tests.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$taskPackageRoot = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd([char[]]'\/')
if ($env:OS -ne 'Windows_NT' -or $taskPackageRoot.StartsWith('\\', [StringComparison]::Ordinal) -or
    $taskPackageRoot.Length -le [IO.Path]::GetPathRoot($taskPackageRoot).TrimEnd([char[]]'\/').Length) {
    throw 'Use a dedicated package directory on a local Windows drive, not the drive root.'
}
$taskBoundary = $taskPackageRoot + [IO.Path]::DirectorySeparatorChar
$taskUtf8 = New-Object System.Text.UTF8Encoding($false)
[string[]]$taskOutputNames = @('file-license-index.json', 'provenance.json', 'validation-range.json', 'package-manifest.json', 'SHA256SUMS')

function Assert-TaskRelative {
    param([string]$Relative)
    if ([string]::IsNullOrWhiteSpace($Relative) -or $Relative.Contains('\') -or $Relative -match '[\x00-\x1f:\*?"<>|]' -or $Relative.StartsWith('/')) {
        throw 'Release paths must be canonical relative paths with forward slashes.'
    }
    foreach ($taskPart in $Relative.Split('/')) {
        if ([string]::IsNullOrEmpty($taskPart) -or $taskPart -in @('.', '..') -or $taskPart.EndsWith('.') -or $taskPart.EndsWith(' ') -or
            $taskPart -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'A release path contains an unsafe component.' }
    }
    return $Relative
}

function Get-TaskPackagePath {
    param([string]$Relative)
    $null = Assert-TaskRelative $Relative
    $taskPath = [IO.Path]::GetFullPath((Join-Path $taskPackageRoot $Relative.Replace('/', '\')))
    if (-not $taskPath.StartsWith($taskBoundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'A release path is outside the package.' }
    return $taskPath
}

function Assert-TaskPlain {
    param([string]$Path, [switch]$Directory)
    $taskItem = Get-Item -LiteralPath $Path -Force
    if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $taskItem.PSIsContainer -ne [bool]$Directory) {
        throw 'Release inputs must be real files/directories without links or junctions.'
    }
    return $taskItem
}

function Get-TaskOptionalProperty {
    param($Object, [string]$Name, $Default = $null)
    if ($null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]) { return $Object.$Name }
    return $Default
}

function Get-TaskRecord {
    param([string]$Relative)
    $taskPath = Get-TaskPackagePath $Relative
    $taskItem = Assert-TaskPlain $taskPath
    return [ordered]@{ path = $Relative; sha256 = (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant(); size = $taskItem.Length }
}

function Write-TaskJson {
    param([string]$Relative, $Value)
    $taskPath = Get-TaskPackagePath $Relative
    if (Test-Path -LiteralPath $taskPath) { $null = Assert-TaskPlain $taskPath }
    $taskJson = ($Value | ConvertTo-Json -Depth 24) + [Environment]::NewLine
    Assert-TaskPublicText $taskJson
    [IO.File]::WriteAllText($taskPath, $taskJson, $taskUtf8)
}

function Assert-TaskPublicText {
    param([string]$Value)
    if ($Value -match '(?i)[a-z]:[\\/]Users[\\/]|/(?:Users|home)/[^/\s]+/') {
        throw 'Personal home-directory paths are not allowed in public release text.'
    }
}

function Get-TaskPackageFiles {
    $taskPaths = New-Object System.Collections.Generic.List[string]
    $taskSeen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    function Visit-TaskDirectory {
        param([string]$Directory, [string]$Prefix)
        foreach ($taskItem in Get-ChildItem -LiteralPath $Directory -Force) {
            if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked package content cannot be inventoried.' }
            $taskRelative = if ($Prefix) { $Prefix + '/' + $taskItem.Name } else { $taskItem.Name }
            $null = Assert-TaskRelative $taskRelative
            if (-not $taskSeen.Add($taskRelative)) { throw 'Duplicate or case-colliding package paths are not supported.' }
            if ($taskItem.Name -match '^(\.git|\.codex|\.build|node_modules|Backups|UserData|Logs|release-inputs|output)$' -or
                $taskItem.Name -match '^(BUILD-STATUS|AGENTS)\.md$' -or $taskItem.Extension -eq '.pdf') {
                throw ('Private/runtime data or an internal document was found in the release: ' + $taskRelative)
            }
            if ($taskItem.PSIsContainer) { Visit-TaskDirectory $taskItem.FullName $taskRelative }
            else { [void]$taskPaths.Add($taskRelative) }
        }
    }
    Visit-TaskDirectory $taskPackageRoot ''
    [string[]]$taskSorted = $taskPaths.ToArray()
    [Array]::Sort($taskSorted, [StringComparer]::Ordinal)
    return $taskSorted
}

function Assert-TaskNoticePaths {
    param($Paths)
    if ($null -eq $Paths) { return }
    foreach ($taskNotice in @($Paths)) {
        if ($taskNotice -isnot [string]) { throw 'Notice references must be package-relative strings.' }
        $null = Assert-TaskPlain (Get-TaskPackagePath $taskNotice)
    }
}

$null = Assert-TaskPlain $taskPackageRoot -Directory
# Refuse a linked ancestor as well as linked descendants.
$taskAncestor = $taskPackageRoot
while ($taskAncestor) {
    $null = Assert-TaskPlain $taskAncestor -Directory
    $taskParent = [IO.Path]::GetDirectoryName($taskAncestor)
    if ($taskParent -eq $taskAncestor) { break }
    $taskAncestor = $taskParent
}
if ([string]::IsNullOrWhiteSpace($SourceRecordsPath)) { $SourceRecordsPath = Get-TaskPackagePath 'source-records.json' }
$SourceRecordsPath = [IO.Path]::GetFullPath($SourceRecordsPath)
if ($SourceRecordsPath -cne (Get-TaskPackagePath 'source-records.json')) { throw 'Use the package-root source-records.json so its exact input is retained.' }
$null = Assert-TaskPlain $SourceRecordsPath
$taskSourceText = [IO.File]::ReadAllText($SourceRecordsPath)
Assert-TaskPublicText $taskSourceText
$taskSources = $taskSourceText | ConvertFrom-Json
if ($taskSources.schemaVersion -ne 1 -or $taskSources.recordType -cne 'propernotes-explicit-source-records' -or $taskSources.files -isnot [Array]) { throw 'The explicit source-records input is invalid.' }
$taskInventoryPath = Get-TaskPackagePath 'third-party-inventory.json'
$null = Assert-TaskPlain $taskInventoryPath
$taskInventoryText = [IO.File]::ReadAllText($taskInventoryPath)
Assert-TaskPublicText $taskInventoryText
$taskInventory = $taskInventoryText | ConvertFrom-Json
$taskRules = @(Get-TaskOptionalProperty $taskInventory 'fileRules' @())
if (-not $taskRules.Count) { throw 'The third-party inventory needs explicit fileRules before a package can be recorded.' }

if (-not [string]::IsNullOrWhiteSpace($SourceRepositoryUrl)) {
    $taskUri = $null
    if (-not [Uri]::TryCreate($SourceRepositoryUrl, [UriKind]::Absolute, [ref]$taskUri) -or $taskUri.Scheme -cne 'https' -or $taskUri.UserInfo -or $taskUri.Query -or $taskUri.Fragment) {
        throw 'SourceRepositoryUrl must be an HTTPS repository URL without credentials, query, or fragment.'
    }
}
if ($SourceCommit -and $SourceCommit -cnotmatch '^(?:[a-f0-9]{40}|[a-f0-9]{64})$') { throw 'SourceCommit must be the complete lowercase commit hash, not an inferred revision.' }
Assert-TaskPublicText $ReleaseName

$taskSourceMap = [System.Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
foreach ($taskSource in $taskSources.files) {
    $taskRelative = Assert-TaskRelative $taskSource.path
    if ($taskSourceMap.ContainsKey($taskRelative)) { throw 'Explicit source records contain a duplicate path.' }
    if ([string]::IsNullOrWhiteSpace($taskSource.licenseExpression) -or [string]::IsNullOrWhiteSpace($taskSource.scope)) { throw 'Source records need an explicit license and rights scope.' }
    $taskActual = Get-TaskRecord $taskRelative
    if ($taskActual.sha256 -cne $taskSource.sha256 -or $taskActual.size -ne $taskSource.size) { throw ('A packaged source differs from its recorded bytes: ' + $taskRelative) }
    $taskSourcePath = Get-TaskOptionalProperty $taskSource 'sourcePath'
    if ($taskSourcePath) { $null = Assert-TaskRelative $taskSourcePath }
    Assert-TaskNoticePaths $taskSource.noticePaths
    Assert-TaskNoticePaths (Get-TaskOptionalProperty $taskSource 'additionalPermissionPaths' @())
    if ([IO.Path]::GetExtension($taskRelative).ToLowerInvariant() -in @('.js', '.cjs', '.cs', '.csproj', '.manifest', '.json', '.ps1', '.cmd', '.md', '.html', '.css', '.yml', '.gitattributes')) {
        Assert-TaskPublicText ([IO.File]::ReadAllText((Get-TaskPackagePath $taskRelative)))
    }
    $taskSourceMap.Add($taskRelative, $taskSource)
}

$taskValidatedRules = New-Object System.Collections.Generic.List[object]
foreach ($taskRule in $taskRules) {
    $taskExact = Get-TaskOptionalProperty $taskRule 'path'
    $taskPrefix = Get-TaskOptionalProperty $taskRule 'prefix'
    if (([bool]$taskExact) -eq ([bool]$taskPrefix)) { throw 'A third-party fileRule needs exactly one path or prefix.' }
    if ($taskExact) { $null = Assert-TaskRelative $taskExact }
    else {
        if ([string]::IsNullOrWhiteSpace($taskPrefix) -or $taskPrefix.Contains('//')) { throw 'A third-party path prefix must not be empty or contain empty components.' }
        if ($taskPrefix.EndsWith('/')) { $null = Assert-TaskRelative $taskPrefix.TrimEnd('/') }
        else {
            $taskLastPrefixPart = $taskPrefix.Substring($taskPrefix.LastIndexOf('/') + 1)
            if ($taskLastPrefixPart -in @('.', '..') -or $taskLastPrefixPart.EndsWith(' ') -or
                $taskLastPrefixPart -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'A filename-family prefix contains an unsafe final component.' }
            # A prefix is never an I/O path. The sentinel permits a family such
            # as Microsoft.Web.WebView2. while full file paths stay strict.
            $null = Assert-TaskRelative ($taskPrefix + '__family_member__')
        }
    }
    if ([string]::IsNullOrWhiteSpace($taskRule.componentId) -or [string]::IsNullOrWhiteSpace($taskRule.licenseExpression) -or [string]::IsNullOrWhiteSpace($taskRule.scope)) {
        throw 'A third-party fileRule needs componentId, licenseExpression, and scope.'
    }
    Assert-TaskNoticePaths $taskRule.noticePaths
    Assert-TaskNoticePaths (Get-TaskOptionalProperty $taskRule 'additionalPermissionPaths' @())
    $taskValidatedRules.Add($taskRule)
}

[string[]]$taskPaths = @(Get-TaskPackageFiles | Where-Object { $_ -cnotin $taskOutputNames })
# PowerShell pipelines and array concatenation otherwise produce Object[].
# StringComparer requires unwrapped strings at the Array.Sort boundary.
[string[]]$taskIndexedPaths = @($taskPaths + $taskOutputNames)
[Array]::Sort($taskIndexedPaths, [StringComparer]::Ordinal)
$taskLicenseFiles = New-Object System.Collections.Generic.List[object]
$taskGaps = New-Object System.Collections.Generic.List[string]
foreach ($taskRelative in $taskIndexedPaths) {
    $taskMatch = $null
    $taskMatchKind = $null
    if ($taskSourceMap.ContainsKey($taskRelative)) { $taskMatch = $taskSourceMap[$taskRelative]; $taskMatchKind = 'explicit-source-record' }
    elseif ($taskRelative -ceq 'source-records.json' -or $taskRelative -cin $taskOutputNames) {
        $taskMatch = [pscustomobject]@{ componentId = 'propernotes-release-records'; licenseExpression = 'CC-BY-SA-4.0'; noticePaths = @('LICENSE-DOCS', 'COPYRIGHT.md'); scope = 'Original explanatory wording only. File names, measurements, hashes, and other facts carry no new copyright claim. Referenced third-party terms remain unchanged.' }
        $taskMatchKind = 'generated-release-record'
    } else {
        $taskBestLength = -1
        foreach ($taskRule in $taskValidatedRules) {
            $taskExact = Get-TaskOptionalProperty $taskRule 'path'
            $taskPrefix = Get-TaskOptionalProperty $taskRule 'prefix'
            $taskLength = -1
            if ($taskExact -and $taskRelative -ceq $taskExact) { $taskLength = [int]::MaxValue }
            elseif ($taskPrefix -and $taskRelative.StartsWith($taskPrefix, [StringComparison]::Ordinal)) { $taskLength = $taskPrefix.Length }
            if ($taskLength -ge 0 -and $taskLength -eq $taskBestLength) { throw ('Ambiguous third-party fileRules for: ' + $taskRelative) }
            if ($taskLength -gt $taskBestLength) { $taskBestLength = $taskLength; $taskMatch = $taskRule; $taskMatchKind = 'third-party-file-rule' }
        }
    }
    if ($null -eq $taskMatch) {
        if ($taskRelative -cnotmatch '^(vendor|runtime|host|licenses)/') { throw ('An unlisted original/internal file cannot enter the public package: ' + $taskRelative) }
        $taskMatch = [pscustomobject]@{ componentId = 'unresolved'; licenseExpression = 'NOASSERTION'; noticePaths = @(); scope = 'Not mapped by the explicit source records or third-party fileRules. No ProperNotes license is applied.' }
        $taskMatchKind = 'unresolved'
    }
    if ($taskMatch.licenseExpression -ceq 'NOASSERTION') { $taskGaps.Add($taskRelative) }
    $taskLicenseRecord = [ordered]@{ path = $taskRelative; componentId = $taskMatch.componentId; licenseExpression = $taskMatch.licenseExpression; scope = $taskMatch.scope; noticePaths = @($taskMatch.noticePaths); mapping = $taskMatchKind }
    $taskAdditionalPermissions = @(Get-TaskOptionalProperty $taskMatch 'additionalPermissionPaths' @())
    if ($taskAdditionalPermissions.Count) { $taskLicenseRecord['additionalPermissionPaths'] = $taskAdditionalPermissions }
    $taskBaseLicense = Get-TaskOptionalProperty $taskMatch 'baseLicense'
    if ($taskBaseLicense) { $taskLicenseRecord['baseLicense'] = $taskBaseLicense }
    $taskLicenseFiles.Add($taskLicenseRecord)
}
$taskRecordedAt = [DateTime]::UtcNow.ToString('o')
Write-TaskJson 'file-license-index.json' ([ordered]@{
    schemaVersion = 1; recordType = 'propernotes-file-license-index'; recordedAtUtc = $taskRecordedAt
    pathBase = 'package_root'; licenseCoverageComplete = ($taskGaps.Count -eq 0); unresolvedPaths = @($taskGaps.ToArray())
    interpretation = 'Per-file rights scopes and preserved notices apply. LicenseRef identifiers are local descriptions, not a claim of SPDX approval. Unknown files never inherit the main project license.'
    thirdPartyLicenseReferences = 'third-party-inventory.json#/licenseReferences'
    localLicenseReferences = @(
        [ordered]@{ id = 'LicenseRef-GNU-GPL-3.0-License-Text'; noticePaths = @('LICENSE'); scope = 'The verbatim GNU license text has its own copyright and copying notice.' },
        [ordered]@{ id = 'LicenseRef-ProperNotes-WebView2-Additional-Permission'; noticePaths = @('LICENSE-WEBVIEW2-EXCEPTION'); scope = 'The stated additional permission text for original Windows host code. Only entries with explicit additionalPermissionPaths carry that permission; Microsoft components keep their original terms.' }
    )
    files = @($taskLicenseFiles.ToArray())
})

$taskHostEvidence = $null
if ($HostArtifactManifestPath) {
    $taskHostManifestItem = Assert-TaskPlain ([IO.Path]::GetFullPath($HostArtifactManifestPath))
    if ($taskHostManifestItem.Length -gt 8MB) { throw 'The host artifact manifest is too large.' }
    $taskHostManifest = [IO.File]::ReadAllText($taskHostManifestItem.FullName) | ConvertFrom-Json
    $taskHostCommit = Get-TaskOptionalProperty $taskHostManifest 'sourceCommit'
    if ($taskHostCommit -and $taskHostCommit -cnotmatch '^(?:[a-f0-9]{40}|[a-f0-9]{64})$') { throw 'The host artifact source commit is invalid.' }
    $taskHostEvidence = [ordered]@{
        recordType = Get-TaskOptionalProperty $taskHostManifest 'app'; rid = Get-TaskOptionalProperty $taskHostManifest 'rid'
        sourceHash = Get-TaskOptionalProperty $taskHostManifest 'sourceHash'; sourceCommit = $taskHostCommit
        sdkVersion = Get-TaskOptionalProperty $taskHostManifest 'sdkVersion'; webView2SdkVersion = Get-TaskOptionalProperty $taskHostManifest 'webView2SdkVersion'
        builtAtUtc = Get-TaskOptionalProperty $taskHostManifest 'builtAtUtc'
        manifestSha256 = (Get-FileHash -LiteralPath $taskHostManifestItem.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        meaning = 'Artifact identity checked by the portable builder against the included native source bytes and host payload. This commit identifies the host CI input; it may differ from the full application source commit.'
    }
}
$taskSourceCommitStatus = if ($SourceCommit) { 'explicit-caller-record' } else { 'not-supplied' }
Write-TaskJson 'validation-range.json' ([ordered]@{
    schemaVersion = 1; recordType = 'propernotes-validation-range'; recordedAtUtc = $taskRecordedAt
    testsRun = $false; applicationLaunched = $false; functionalAcceptancePassed = $false; deviceAcceptancePassed = $false
    executionScope = 'This metadata-generation and package-build process only. Later user sessions and independent verification are not recorded here.'
    hostBuildMode = $HostBuildMode; hostCompilationEvidence = $(if ($HostBuildMode -eq 'artifact-import') { 'Imported artifact checked by builder; no local compilation claimed.' } elseif ($HostBuildMode -eq 'offline-publish') { 'Builder completed local offline publish before metadata generation.' } else { 'Not asserted by metadata generator.' })
    completed = @('Enumerated the actual package without links or case-colliding paths.', 'Checked explicit source byte records.', 'Applied explicit per-file rights rules; retained unresolved coverage gaps.', 'Scanned recorded original source text for personal home-directory paths.', 'Generated byte counts and SHA-256 fingerprints.')
    notPerformed = @('Application or HTTP service startup.', 'Automated tests or interactive acceptance.', 'PDF open/save/reopen, backup recovery, printing, and data-loss acceptance.', 'Surface pen, external tablet, touch, mouse, and keyboard device acceptance.', 'Binary-wide privacy inspection, malware analysis, or independent license/legal approval.')
    unverifiedReferenceBehavior = @('Drawboard Mouse and Touch / Pen switching.', 'Drawboard Tool Lock.')
    pendingAcceptanceAreas = @('Compatibility on the target Windows hardware, including Surface and external pen tablets.', 'Measured handwriting responsiveness and PDF reading/saving performance.')
    releaseAcceptance = 'Not accepted. Compilation, packaging and hashing do not establish working application behavior.'
})
Write-TaskJson 'provenance.json' ([ordered]@{
    schemaVersion = 1; recordType = 'propernotes-package-provenance'; releaseName = $ReleaseName; recordedAtUtc = $taskRecordedAt
    source = [ordered]@{ repositoryUrl = $SourceRepositoryUrl; commit = $SourceCommit; commitStatus = $taskSourceCommitStatus; explicitRecords = Get-TaskRecord 'source-records.json'; sourceArchiveStatus = 'Not asserted by this package-local record. The release maintainer must separately prepare and check a complete corresponding source archive before binary distribution.' }
    inputs = @((Get-TaskRecord 'third-party-inventory.json'), (Get-TaskRecord 'propernotes-portable.json'))
    nativeHostBuild = $taskHostEvidence; hostBuildMode = $HostBuildMode
    inventoryReadiness = Get-TaskOptionalProperty $taskInventory 'releaseReadiness' 'not-stated'; unresolvedLicensePaths = @($taskGaps.ToArray())
    integrityMeaning = 'Hashes identify these local bytes. They do not by themselves authenticate an upstream publisher, prove a clean build, or establish license compatibility.'
    publicationStatus = 'Not asserted. These records describe a locally assembled package.'
})

$taskManifestFiles = @(Get-TaskPackageFiles | Where-Object { $_ -cnotin @('package-manifest.json', 'SHA256SUMS') } | ForEach-Object { Get-TaskRecord $_ })
Write-TaskJson 'package-manifest.json' ([ordered]@{
    schemaVersion = 1; recordType = 'propernotes-package-manifest'; releaseName = $ReleaseName; recordedAtUtc = $taskRecordedAt
    pathBase = 'package_root'; hashAlgorithm = 'SHA-256'; fileCount = $taskManifestFiles.Count
    excludedFromOwnFileRecords = @('package-manifest.json', 'SHA256SUMS')
    completeChecksumList = 'SHA256SUMS covers every regular file except itself, including this manifest. This avoids recursive self-hashes.'
    files = $taskManifestFiles
})
$taskChecksumLines = @(Get-TaskPackageFiles | Where-Object { $_ -cne 'SHA256SUMS' } | ForEach-Object { $taskRecord = Get-TaskRecord $_; $taskRecord.sha256 + '  ' + $taskRecord.path })
$taskChecksumPath = Get-TaskPackagePath 'SHA256SUMS'
if (Test-Path -LiteralPath $taskChecksumPath) { $null = Assert-TaskPlain $taskChecksumPath }
[IO.File]::WriteAllText($taskChecksumPath, (($taskChecksumLines -join "`n") + "`n"), $taskUtf8)
Write-Host ('Release records created for ' + $taskChecksumLines.Count + ' files. Unresolved license paths: ' + $taskGaps.Count)
Write-Host 'The application and tests were not started. This package still requires runtime and device acceptance.'
