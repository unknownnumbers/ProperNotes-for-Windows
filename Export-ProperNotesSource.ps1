[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$NodePath,
    [Parameter(Mandatory = $true)][string]$NodeLicensePath
)

# Maintainer-only source export. No program, application, test, dependency
# installer, or network command is invoked. Inputs are never changed.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$taskSourceRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd([char[]]'\/')
$taskExportRoot = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd([char[]]'\/')
$taskExportFiles = New-Object System.Collections.Generic.List[object]
$taskDestinations = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

function Assert-TaskExportRelative {
    param([string]$Relative)
    if ([string]::IsNullOrWhiteSpace($Relative) -or $Relative.Contains('\') -or $Relative.StartsWith('/') -or $Relative -match '[\x00-\x1f:\*?"<>|]') {
        throw 'Source export paths must be canonical forward-slash relative paths.'
    }
    foreach ($taskPart in $Relative.Split('/')) {
        if ([string]::IsNullOrEmpty($taskPart) -or $taskPart -in @('.', '..') -or $taskPart.EndsWith('.') -or $taskPart.EndsWith(' ') -or
            $taskPart -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'A source export path contains an unsafe component.' }
    }
    return $Relative
}

function Assert-TaskExportPlain {
    param([string]$Path, [switch]$Directory)
    $taskItem = Get-Item -LiteralPath $Path -Force
    if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $taskItem.PSIsContainer -ne [bool]$Directory) {
        throw 'Source exports accept only real files/directories, without links or junctions.'
    }
    return $taskItem
}

function Assert-TaskExportDirectoryChain {
    param([string]$Path)
    $taskCurrent = [IO.Path]::GetFullPath($Path)
    while ($taskCurrent) {
        $taskItem = $null
        try { $taskItem = Get-Item -LiteralPath $taskCurrent -Force -ErrorAction Stop }
        catch [System.Management.Automation.ItemNotFoundException] { }
        if ($null -ne $taskItem -and (-not $taskItem.PSIsContainer -or ($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw 'Source export directory chains must not contain files, links or junctions.'
        }
        $taskParent = [IO.Path]::GetDirectoryName($taskCurrent)
        if ($taskParent -eq $taskCurrent) { break }
        $taskCurrent = $taskParent
    }
}

function Get-TaskExportInput {
    param([string]$Relative)
    $null = Assert-TaskExportRelative $Relative
    $taskAbsolute = [IO.Path]::GetFullPath((Join-Path $taskSourceRoot $Relative.Replace('/', '\')))
    if (-not $taskAbsolute.StartsWith(($taskSourceRoot + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) { throw 'A source input is outside the explicit source root.' }
    Assert-TaskExportDirectoryChain ([IO.Path]::GetDirectoryName($taskAbsolute))
    $null = Assert-TaskExportPlain $taskAbsolute
    return $taskAbsolute
}

function Add-TaskExportFile {
    param([string]$Source, [string]$Destination, [string]$ExpectedSha256)
    $null = Assert-TaskExportRelative $Destination
    if (-not $taskDestinations.Add($Destination)) { throw ('Duplicate source export destination: ' + $Destination) }
    $taskInput = Assert-TaskExportPlain ([IO.Path]::GetFullPath($Source))
    Assert-TaskExportDirectoryChain $taskInput.DirectoryName
    $taskSha = (Get-FileHash -LiteralPath $taskInput.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ExpectedSha256 -and ($ExpectedSha256 -cnotmatch '^[a-f0-9]{64}$' -or $taskSha -cne $ExpectedSha256)) { throw ('An approved source input has different bytes: ' + $Destination) }
    $taskExportFiles.Add([pscustomobject]@{ source = $taskInput.FullName; destination = $Destination; size = $taskInput.Length; sha256 = $taskSha })
}

function Add-TaskExportRelativeFile {
    param([string]$Source, [string]$Destination, [string]$ExpectedSha256)
    Add-TaskExportFile -Source (Get-TaskExportInput $Source) -Destination $Destination -ExpectedSha256 $ExpectedSha256
}

function Add-TaskExportHostDirectory {
    param([string]$Relative)
    $taskDirectory = Join-Path $taskSourceRoot $Relative.Replace('/', '\')
    $null = Assert-TaskExportPlain $taskDirectory -Directory
    Assert-TaskExportDirectoryChain $taskDirectory
    foreach ($taskItem in Get-ChildItem -LiteralPath $taskDirectory -Force) {
        if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked native host sources cannot be exported.' }
        $taskChild = $Relative + '/' + $taskItem.Name
        if ($taskItem.PSIsContainer) {
            if ($taskItem.Name -notin @('bin', 'obj', '.git', 'output', 'research', 'tests', 'qa', 'UserData', 'Backups', 'Logs', 'runtime')) { Add-TaskExportHostDirectory $taskChild }
        } elseif ($taskItem.Extension.ToLowerInvariant() -in @('.cs', '.csproj', '.manifest', '.json')) {
            Add-TaskExportRelativeFile -Source $taskChild -Destination $taskChild
        }
    }
}

function Add-TaskExportLicenseDirectory {
    param([string]$Relative)
    $taskDirectory = Join-Path $taskSourceRoot $Relative.Replace('/', '\')
    $null = Assert-TaskExportPlain $taskDirectory -Directory
    Assert-TaskExportDirectoryChain $taskDirectory
    foreach ($taskItem in Get-ChildItem -LiteralPath $taskDirectory -Force) {
        if (($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked license inputs cannot be exported.' }
        $taskChild = $Relative + '/' + $taskItem.Name
        if ($taskItem.Name -match '^(\.git|\.codex|\.build|node_modules|Backups|UserData|Logs|output|research|tests|qa|runtime)$' -or
            $taskItem.Name -match '^(AGENTS|BUILD-STATUS)\.md$' -or $taskItem.Extension -eq '.pdf') { throw 'An internal document or runtime data was found in the approved license directory.' }
        if ($taskChild -ceq 'licenses/node.txt') { continue }
        if ($taskItem.PSIsContainer) { Add-TaskExportLicenseDirectory $taskChild }
        else { Add-TaskExportRelativeFile -Source $taskChild -Destination $taskChild }
    }
}

if ($env:OS -ne 'Windows_NT' -or $taskSourceRoot.StartsWith('\\', [StringComparison]::Ordinal) -or $taskExportRoot.StartsWith('\\', [StringComparison]::Ordinal) -or
    $taskExportRoot.Length -le [IO.Path]::GetPathRoot($taskExportRoot).TrimEnd([char[]]'\/').Length) { throw 'Use local Windows source and output directories, not a drive root.' }
Assert-TaskExportDirectoryChain $taskSourceRoot
Assert-TaskExportDirectoryChain $taskExportRoot
if (Test-Path -LiteralPath $taskExportRoot) { throw 'OutputDirectory already exists. No existing directory is replaced or merged.' }
foreach ($taskProtected in @('vendor', 'licenses', 'windows-host', 'release-docs', 'cloud-build', 'runtime')) {
    $taskProtectedRoot = Join-Path $taskSourceRoot $taskProtected
    if ($taskExportRoot.Equals($taskProtectedRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $taskExportRoot.StartsWith(($taskProtectedRoot + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) { throw 'The output cannot be inside a source input or runtime directory.' }
}

# Keep this literal original-source allowlist aligned with the portable builder.
# Neither script imports or evaluates the other script to discover its inputs.
$taskOriginalSources = @('annotation-geometry.js', 'annotation-menu.js', 'app-main.js', 'backup-client.js', 'backup-store.cjs', 'browser-workspace.js', 'document-view.js', 'document-workspace.js', 'favourite-tools.js', 'file-api.js', 'host-window.js', 'ink-engine.js', 'input-controls.js', 'input-preferences.js', 'keyboard-shortcuts.js', 'native-files.cjs', 'pdf-bookmarks.js', 'pdf-storage.js', 'pdf-text-search.js', 'preferences.js', 'prepare.cjs', 'render-page.js', 'Select-ProperNotesDocument.ps1', 'server.cjs', 'text-appearance.js', 'text-selection.js', 'ui.template.html', 'workspace-store.cjs', 'Start-ProperNotes.cmd', 'Start-ProperNotes.ps1', 'Start-ProperNotesNative.ps1', 'Build-ProperNotesPortable.cmd', 'Build-ProperNotesPortable.ps1', 'New-ProperNotesReleaseMetadata.ps1', 'Export-ProperNotesSource.ps1')
foreach ($taskFile in $taskOriginalSources) { Add-TaskExportRelativeFile -Source $taskFile -Destination $taskFile }
foreach ($taskFile in @('Start-InkPDF.cmd', 'Start-InkPDF.ps1', 'Start-ProperNotesBrowser.cmd', 'Start-ProperNotesBrowser.ps1')) {
    $taskOptionalItem = $null
    try { $taskOptionalItem = Get-Item -LiteralPath (Join-Path $taskSourceRoot $taskFile) -Force -ErrorAction Stop }
    catch [System.Management.Automation.ItemNotFoundException] { }
    if ($null -ne $taskOptionalItem) { Add-TaskExportRelativeFile -Source $taskFile -Destination $taskFile }
}
$taskPublicDocuments = @(
    @{ source = 'release-docs/README.md'; destination = 'README.md' },
    @{ source = 'release-docs/PORTABLE.md'; destination = 'PORTABLE.md' },
    @{ source = 'release-docs/COPYRIGHT.md'; destination = 'COPYRIGHT.md' },
    @{ source = 'release-docs/THIRD_PARTY_NOTICES.md'; destination = 'THIRD_PARTY_NOTICES.md' },
    @{ source = 'release-docs/third-party-inventory.json'; destination = 'third-party-inventory.json' },
    @{ source = 'release-docs/RELEASE-METADATA.md'; destination = 'RELEASE-METADATA.md' }
)
foreach ($taskDocument in $taskPublicDocuments) {
    Add-TaskExportRelativeFile -Source $taskDocument.source -Destination $taskDocument.destination
    Add-TaskExportRelativeFile -Source $taskDocument.source -Destination $taskDocument.source
}
foreach ($taskFile in @('LICENSE', 'LICENSE-DOCS', 'LICENSE-WEBVIEW2-EXCEPTION')) { Add-TaskExportRelativeFile -Source $taskFile -Destination $taskFile }
foreach ($taskFile in @('Build-HostArtifact.ps1', 'windows-portable.yml', 'CLOUD-BUILD.md', 'source-bytes.gitattributes')) {
    Add-TaskExportRelativeFile -Source ('cloud-build/' + $taskFile) -Destination ('cloud-build/' + $taskFile)
}
Add-TaskExportRelativeFile -Source 'cloud-build/source-bytes.gitattributes' -Destination '.gitattributes'
Add-TaskExportRelativeFile -Source 'cloud-build/windows-portable.yml' -Destination '.github/workflows/windows-portable.yml'
Add-TaskExportHostDirectory 'windows-host'
Add-TaskExportRelativeFile -Source 'windows-host/README.md' -Destination 'windows-host/README.md'

# Only the 201 explicitly verified vendor paths enter this export. Other files
# in the vendor tree are not recursively copied or used as approval evidence.
$taskInventory = [IO.File]::ReadAllText((Get-TaskExportInput 'release-docs/third-party-inventory.json')) | ConvertFrom-Json
$taskVendorRecords = @($taskInventory.verifiedExistingVendorFiles | Where-Object { $_.path.StartsWith('vendor/', [StringComparison]::Ordinal) })
if ($taskVendorRecords.Count -ne 201) { throw 'The approved vendor inventory must contain exactly the reviewed 201 vendor files.' }
foreach ($taskRecord in $taskVendorRecords) {
    if ($taskRecord.matched -ne $true -or $taskRecord.sha256 -cnotmatch '^[a-f0-9]{64}$') { throw 'A vendor record lacks its reviewed byte identity.' }
    Add-TaskExportRelativeFile -Source $taskRecord.path -Destination $taskRecord.path -ExpectedSha256 $taskRecord.sha256
}
Add-TaskExportLicenseDirectory 'licenses'

$taskNodeMappings = @($taskInventory.releaseInputMappings | Where-Object { $_.releasePath -ceq 'runtime/node.exe' })
$taskNodeLicenseMappings = @($taskInventory.releaseInputMappings | Where-Object { $_.releasePath -ceq 'licenses/node.txt' })
if ($taskNodeMappings.Count -ne 1 -or $taskNodeLicenseMappings.Count -ne 1) { throw 'The inventory needs one exact approved Node executable and license mapping.' }
if ($taskNodeMappings[0].sha256 -cnotmatch '^[a-f0-9]{64}$' -or $taskNodeLicenseMappings[0].sha256 -cnotmatch '^[a-f0-9]{64}$') { throw 'The approved Node mappings need complete SHA-256 fingerprints.' }
Add-TaskExportFile -Source $NodePath -Destination 'runtime/node.exe' -ExpectedSha256 $taskNodeMappings[0].sha256
Add-TaskExportFile -Source $NodeLicensePath -Destination 'licenses/node.txt' -ExpectedSha256 $taskNodeLicenseMappings[0].sha256

# Preflight is complete before creating the new output. A later failure keeps
# the partial new directory for inspection and never retries by overwriting it.
try {
    Assert-TaskExportDirectoryChain $taskExportRoot
    if (Test-Path -LiteralPath $taskExportRoot) { throw 'The output appeared during preflight. No replacement was attempted.' }
    $null = [IO.Directory]::CreateDirectory($taskExportRoot)
    $null = Assert-TaskExportPlain $taskExportRoot -Directory
    foreach ($taskRecord in $taskExportFiles) {
        $taskDestination = [IO.Path]::GetFullPath((Join-Path $taskExportRoot $taskRecord.destination.Replace('/', '\')))
        if (-not $taskDestination.StartsWith(($taskExportRoot + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) { throw 'A destination is outside the fresh output.' }
        $taskParent = [IO.Path]::GetDirectoryName($taskDestination)
        Assert-TaskExportDirectoryChain $taskParent
        $null = [IO.Directory]::CreateDirectory($taskParent)
        Assert-TaskExportDirectoryChain $taskParent
        $taskInput = Assert-TaskExportPlain $taskRecord.source
        if ($taskInput.Length -ne $taskRecord.size -or (Get-FileHash -LiteralPath $taskInput.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskRecord.sha256) { throw ('An input changed after export preflight: ' + $taskRecord.destination) }
        # File.Copy(..., false) refuses a pre-existing destination.
        [IO.File]::Copy($taskInput.FullName, $taskDestination, $false)
        $taskCopied = Assert-TaskExportPlain $taskDestination
        if ($taskCopied.Length -ne $taskRecord.size -or (Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskRecord.sha256) { throw ('An exported file differs from its selected input: ' + $taskRecord.destination) }
    }
    Write-Host ('Clean source export completed: ' + $taskExportFiles.Count + ' files.')
    Write-Host 'No program, application, installer, or test was run. Original inputs were not changed.'
} catch {
    Write-Host 'Source export failed. Any partial new output was retained; no existing source or runtime was changed.'
    throw
}
