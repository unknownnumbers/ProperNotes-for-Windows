# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDirectory,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$PdfSourceDirectory,
    [Parameter(Mandatory = $true)][string]$VcpkgRoot,
    [Parameter(Mandatory = $true)][string]$InstalledDirectory,
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$QtSourceDirectory,
    [Parameter(Mandatory = $true)][string]$ArtifactDirectory,
    [string]$PackageName = 'ProperNotes-for-Windows-0.3.13-native-preview.1-win-x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'Portable assembly is restricted to a GitHub-hosted Windows runner.'
}
if ($PackageName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]+$') { throw 'Invalid package name.' }

function Invoke-PackTool {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Packaging tool failed ($LASTEXITCODE): $File" }
}
function Write-Utf8 {
    param([string]$Path, [string]$Value)
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false))
}
function Copy-File {
    param([string]$Source, [string]$Destination)
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
}
function Copy-Tree {
    param([string]$Source, [string]$Destination)
    $sourceRoot = (Resolve-Path -LiteralPath $Source).Path
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Force) {
        $relative = [IO.Path]::GetRelativePath($sourceRoot, $item.FullName)
        Copy-File $item.FullName (Join-Path $Destination $relative)
    }
}
function Write-Hashes {
    param([string]$Directory)
    $entries = foreach ($file in Get-ChildItem -LiteralPath $Directory -Recurse -File -Force | Sort-Object FullName) {
        if ($file.FullName -eq (Join-Path $Directory 'SHA256SUMS.txt')) { continue }
        $relative = [IO.Path]::GetRelativePath($Directory, $file.FullName).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    }
    Write-Utf8 (Join-Path $Directory 'SHA256SUMS.txt') (($entries -join "`n") + "`n")
}
function Get-UniqueBuiltFile {
    param([string]$Name)
    $matches = @(Get-ChildItem -LiteralPath $BuildDirectory -Recurse -File -Filter $Name |
        Where-Object { $_.FullName -match '[\\/]Release[\\/]' })
    if ($matches.Count -ne 1) { throw "Expected one Release build of $Name; found $($matches.Count)." }
    return $matches[0].FullName
}

$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$portable = Join-Path $ArtifactDirectory $PackageName
$sourceBundle = Join-Path $ArtifactDirectory "$PackageName-sources"
foreach ($directory in @($portable, $sourceBundle)) {
    if (Test-Path -LiteralPath $directory) { throw 'Packaging destinations must be fresh.' }
    New-Item -ItemType Directory -Path $directory | Out-Null
}
$exeName = $lock.application.executable
Copy-File (Get-UniqueBuiltFile $exeName) (Join-Path $portable $exeName)
Copy-File (Get-UniqueBuiltFile 'Pdf4QtLibCore.dll') (Join-Path $portable 'Pdf4QtLibCore.dll')
$tripletDirectory = Join-Path $InstalledDirectory $lock.vcpkg.triplet
foreach ($dll in Get-ChildItem -LiteralPath (Join-Path $tripletDirectory 'bin') -Filter '*.dll' -File) {
    Copy-File $dll.FullName (Join-Path $portable $dll.Name)
}
Invoke-PackTool -File (Join-Path $QtRoot 'bin/windeployqt.exe') -Arguments @(
    '--release', '--no-compiler-runtime', '--no-translations', '--no-patchqt',
    '--no-opengl-sw', '--no-system-d3d-compiler', '--dir', $portable,
    (Join-Path $portable $exeName), (Join-Path $portable 'Pdf4QtLibCore.dll')
)
# Use app-local release CRT DLLs. Never run vc_redist or copy debug_nonredist files.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) { throw 'Visual Studio C++ build tools were not found on the runner.' }
$redistVersion = (Get-Content -LiteralPath (Join-Path $visualStudio 'VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt') -Raw).Trim()
$crt = @(Get-ChildItem -LiteralPath (Join-Path $visualStudio "VC/Redist/MSVC/$redistVersion/x64") -Directory -Filter 'Microsoft.VC*.CRT')
if ($crt.Count -ne 1) { throw 'Expected one app-local release CRT directory.' }
foreach ($dll in Get-ChildItem -LiteralPath $crt[0].FullName -File -Filter '*.dll') {
    Copy-File $dll.FullName (Join-Path $portable $dll.Name)
}
Write-Utf8 (Join-Path $portable 'qt.conf') "[Paths]`nPrefix=.`nPlugins=.`n"
$launcher = @'
@echo off
setlocal
start "" /D "%~dp0" "%~dp0ProperNotes.Native.exe" %*
exit /b %errorlevel%
'@
[IO.File]::WriteAllText((Join-Path $portable 'ProperNotes.cmd'), $launcher.Replace("`n", "`r`n"), [Text.ASCIIEncoding]::new())
Copy-File (Join-Path $ProjectDirectory 'Start-ProperNotes.cmd') (Join-Path $portable 'Start-ProperNotes.cmd')

$licenses = Join-Path $portable 'licenses'
foreach ($name in @('LICENSE', 'LICENSE-DOCS', 'COPYRIGHT.md')) {
    $inputFile = Join-Path $ProjectDirectory $name
    if (-not (Test-Path -LiteralPath $inputFile)) { throw "Required project notice is missing: $name" }
    Copy-File $inputFile (Join-Path $licenses "project/$name")
    Copy-File $inputFile (Join-Path $portable $name)
}
if (Test-Path -LiteralPath (Join-Path $ProjectDirectory 'licenses')) {
    Copy-Tree (Join-Path $ProjectDirectory 'licenses') (Join-Path $licenses 'project/legal-texts')
    Copy-File (Join-Path $ProjectDirectory 'licenses/CC-BY-SA-4.0.txt') (Join-Path $licenses 'CC-BY-SA-4.0.txt')
}
foreach ($notice in @(
    @{ source = 'licenses/lucide.txt'; target = 'Lucide/LICENSE.txt' },
    @{ source = 'licenses/lucide-source.json'; target = 'Lucide/source.json' }
)) {
    $inputFile = Join-Path $ProjectDirectory $notice.source
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) { throw "Required Lucide notice is missing: $($notice.source)" }
    Copy-File $inputFile (Join-Path $licenses $notice.target)
}
foreach ($name in @('README.md', 'PORTABLE.md', 'KNOWN-LIMITATIONS.md', 'LARGE-DOCUMENTS.md')) {
    if (Test-Path -LiteralPath (Join-Path $ProjectDirectory $name)) {
        Copy-File (Join-Path $ProjectDirectory $name) (Join-Path $portable $name)
    }
}
Copy-File (Join-Path $PdfSourceDirectory 'LICENSE') (Join-Path $licenses 'PDF4QT/LICENSE')
Copy-Tree (Join-Path $PdfSourceDirectory '3rdparty_licenses') (Join-Path $licenses 'PDF4QT/upstream-notice-inventory')
Copy-File (Join-Path $PdfSourceDirectory 'Pdf4QtLibCore/cmaps/LICENSE.txt') (Join-Path $licenses 'PDF4QT/cmaps/LICENSE.txt')
Copy-File (Join-Path $PdfSourceDirectory 'Pdf4QtLibCore/liberation-fonts-ttf/LICENSE') (Join-Path $licenses 'PDF4QT/liberation-fonts/LICENSE')
Copy-File (Join-Path $PdfSourceDirectory 'Pdf4QtLibCore/liberation-fonts-ttf/README.md') (Join-Path $licenses 'PDF4QT/liberation-fonts/README.md')

$qtAttributions = [Collections.Generic.List[object]]::new()
foreach ($module in $lock.qt.sourceArchives) {
    $moduleRoot = Join-Path $QtSourceDirectory $module
    $moduleLicenseDir = Join-Path $moduleRoot 'LICENSES'
    if (-not (Test-Path -LiteralPath $moduleLicenseDir)) { throw "Qt source license directory missing: $module" }
    Copy-Tree $moduleLicenseDir (Join-Path $licenses "Qt/$module/LICENSES")
    foreach ($attribution in Get-ChildItem -LiteralPath $moduleRoot -Recurse -File -Filter 'qt_attribution.json') {
        $relative = [IO.Path]::GetRelativePath($moduleRoot, $attribution.FullName)
        Copy-File $attribution.FullName (Join-Path $licenses "Qt/$module/attributions/$relative")
        $records = @(Get-Content -LiteralPath $attribution.FullName -Raw | ConvertFrom-Json)
        foreach ($record in $records) {
            $qtAttributions.Add([ordered]@{ module = $module; metadata = $relative.Replace('\', '/'); attribution = $record })
            $licenseNames = @()
            if ($record.PSObject.Properties.Name -contains 'LicenseFile') { $licenseNames += @($record.LicenseFile) }
            if ($record.PSObject.Properties.Name -contains 'LicenseFiles') { $licenseNames += @($record.LicenseFiles) }
            if ($record.PSObject.Properties.Name -contains 'CopyrightFile') { $licenseNames += @($record.CopyrightFile) }
            if ($record.PSObject.Properties.Name -contains 'CopyrightFiles') { $licenseNames += @($record.CopyrightFiles) }
            foreach ($licenseName in $licenseNames | Select-Object -Unique) {
                $licenseSource = [IO.Path]::GetFullPath((Join-Path $attribution.DirectoryName $licenseName))
                $modulePrefix = [IO.Path]::GetFullPath($moduleRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
                if (-not $licenseSource.StartsWith($modulePrefix, [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'A Qt attribution license path escaped its source module.'
                }
                if (-not (Test-Path -LiteralPath $licenseSource -PathType Leaf)) { throw "Qt attribution license file missing: $module/$relative -> $licenseName" }
                $licenseRelative = [IO.Path]::GetRelativePath($moduleRoot, $licenseSource)
                Copy-File $licenseSource (Join-Path $licenses "Qt/$module/referenced-licenses/$licenseRelative")
            }
        }
    }
}
Write-Utf8 (Join-Path $licenses 'Qt/attribution-inventory.json') (ConvertTo-Json -InputObject $qtAttributions.ToArray() -Depth 30)
$packageInventory = [Collections.Generic.List[object]]::new()
foreach ($port in Get-ChildItem -LiteralPath (Join-Path $tripletDirectory 'share') -Directory) {
    $packageMetadata = Join-Path $port.FullName 'vcpkg.spdx.json'
    foreach ($notice in Get-ChildItem -LiteralPath $port.FullName -Recurse -File -Force |
        Where-Object { $_.Name -match '(?i)^(copyright|license|copying|notice)|^vcpkg\.(spdx\.)?json$' }) {
        $relative = [IO.Path]::GetRelativePath($port.FullName, $notice.FullName)
        Copy-File $notice.FullName (Join-Path $licenses "vcpkg/$($port.Name)/$relative")
    }
    $packageInventory.Add([ordered]@{
        port = $port.Name
        metadata = if (Test-Path -LiteralPath $packageMetadata) { "licenses/vcpkg/$($port.Name)/vcpkg.spdx.json" } else { $null }
    })
}
Copy-File (Join-Path $InstalledDirectory 'vcpkg/status') (Join-Path $licenses 'vcpkg/installed-status.txt')
Write-Utf8 (Join-Path $licenses 'vcpkg/installed-packages.json') (ConvertTo-Json -InputObject $packageInventory.ToArray() -Depth 8)
Write-Utf8 (Join-Path $licenses 'MSVC/REDISTRIBUTION.md') @"
# Microsoft Visual C++ app-local runtime

Copyright Microsoft Corporation. These release CRT DLLs are unmodified copies from the
GitHub-hosted Visual Studio 2022 image. Runtime directory version: $redistVersion.
They retain Microsoft's terms and are not relicensed as ProperNotes code.
No runtime installer is included or executed.

Official redistribution terms and file list:
https://learn.microsoft.com/visualstudio/releases/2022/redistribution
https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
"@
Write-Utf8 (Join-Path $licenses 'README.md') @'
# Third-party notices

ProperNotes native code is GPL-3.0-only; original documentation is CC-BY-SA-4.0.
Third-party components keep their respective copyright and license terms.
The executable embeds Lucide SVG icons, including retained Feather attribution,
under their upstream combined ISC and MIT notice. Their full copyright and license
text is in Lucide/LICENSE.txt, and Lucide/source.json records the fixed upstream
commit and unmodified source-file hashes. Runtime color substitution does not
relicense those resources as ProperNotes code. The original application's code
license remains GPL-3.0-only, and original explanatory text remains CC-BY-SA-4.0.
PDF4QT is MIT, with separately licensed embedded CMaps and Liberation fonts.
Qt is dynamically deployed; its license texts, third-party attributions, and matching
qtbase/qtsvg source are supplied. No WebView2, Node.js, OCR engine, or OCR model is deployed.

The upstream PDF4QT notice folder and Qt attribution collection are source inventories:
their inclusion does not mean every listed optional component is deployed. The actual
vcpkg package status, SPDX metadata, and portable file hashes accompany this package.
The companion sources archive contains project source, the exact PDF4QT/vcpkg snapshots,
Qt source, and extracted/patched dependency source left by the successful vcpkg build.
'@

# Inspect imports as PE metadata. This does not execute any application binary.
$dumpbin = @(Get-ChildItem -Path (Join-Path $visualStudio 'VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe') -File | Sort-Object FullName -Descending)[0].FullName
$imports = [Collections.Generic.List[object]]::new()
$deployedDlls = @(Get-ChildItem -LiteralPath $portable -Recurse -File -Filter '*.dll' | ForEach-Object { $_.Name.ToLowerInvariant() })
foreach ($binary in Get-ChildItem -LiteralPath $portable -Recurse -File | Where-Object { $_.Extension -in @('.dll', '.exe') }) {
    $listing = (& $dumpbin /nologo /dependents $binary.FullName | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read deployed PE import metadata.' }
    $names = @([regex]::Matches($listing, '(?im)^\s+([A-Za-z0-9_.-]+\.dll)\s*$') | ForEach-Object { $_.Groups[1].Value })
    if ($names -match '(?i)^(VCRUNTIME140D|VCRUNTIME140_1D|MSVCP140D|UCRTBASED)\.dll$') { throw 'A Debug runtime import was found in the portable package.' }
    foreach ($name in $names) {
        if ($name.ToLowerInvariant() -in $deployedDlls -or $name -match '(?i)^(api-ms-|ext-ms-)') { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $env:WINDIR "System32/$name"))) { throw "Undeployed non-system import: $name" }
    }
    $imports.Add([ordered]@{ file = [IO.Path]::GetRelativePath($portable, $binary.FullName).Replace('\', '/'); imports = $names })
}
Write-Utf8 (Join-Path $portable 'build-metadata/pe-imports.json') (ConvertTo-Json -InputObject $imports.ToArray() -Depth 8)

$sourceCommit = (& git -C $ProjectDirectory rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify the project source commit.' }
$repositoryRoot = (& git -C $ProjectDirectory rev-parse --show-toplevel | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $repositoryRoot) { throw 'Cannot identify the project repository root.' }
$provenance = [ordered]@{
    schemaVersion = 1
    name = $PackageName
    sourceRepository = "https://github.com/$($env:GITHUB_REPOSITORY)"
    sourceCommit = $sourceCommit
    workflowRun = "https://github.com/$($env:GITHUB_REPOSITORY)/actions/runs/$($env:GITHUB_RUN_ID)"
    runnerImage = $env:ImageVersion
    architecture = 'windows-x64'
    configuration = 'Release'
    dependencyLock = $lock
    msvcRuntimeDirectoryVersion = $redistVersion
    msvcRuntimeDllVersions = @(Get-ChildItem -LiteralPath $crt[0].FullName -File -Filter '*.dll' | ForEach-Object {
        [ordered]@{ file = $_.Name; fileVersion = $_.VersionInfo.FileVersion }
    })
    validatedDependencySourceCacheRestored = ($env:PROPERNOTES_SOURCE_CACHE_RESTORED -eq 'true')
    compiled = $true
    applicationStarted = $false
    applicationTestsRun = $false
    deviceAcceptancePerformed = $false
    sourceArchive = "$PackageName-sources.zip"
    createdUtc = [DateTime]::UtcNow.ToString('o')
}
Write-Utf8 (Join-Path $portable 'build-metadata/provenance.json') (ConvertTo-Json -InputObject $provenance -Depth 15)
Copy-File (Join-Path $PSScriptRoot 'dependencies.lock.json') (Join-Path $portable 'build-metadata/dependencies.lock.json')
Copy-File (Join-Path $PSScriptRoot 'vcpkg/vcpkg.json') (Join-Path $portable 'build-metadata/vcpkg.json')

# Archive only source inputs, never the runner's downloaded executable build tools.
$sourceInputs = Join-Path $sourceBundle 'source-inputs'
New-Item -ItemType Directory -Path $sourceInputs | Out-Null
Invoke-PackTool -File git -Arguments @('-C', $repositoryRoot, 'archive', '--format=zip', '--prefix=propernotes-native/', "--output=$sourceInputs/ProperNotes-native-source.zip", "${sourceCommit}:propernotes-native")
$expectedProjectFiles = @(& git -C $repositoryRoot ls-tree -r --name-only "${sourceCommit}:propernotes-native")
if ($LASTEXITCODE -ne 0 -or $expectedProjectFiles.Count -eq 0) { throw 'The committed native source tree is empty or unreadable.' }
$projectArchive = [IO.Compression.ZipFile]::OpenRead((Join-Path $sourceInputs 'ProperNotes-native-source.zip'))
try {
    $archivedProjectFiles = @($projectArchive.Entries | Where-Object { $_.Name } | ForEach-Object { $_.FullName -replace '^propernotes-native/', '' })
    if ($archivedProjectFiles.Count -ne $expectedProjectFiles.Count -or
        @(Compare-Object -ReferenceObject $expectedProjectFiles -DifferenceObject $archivedProjectFiles).Count -ne 0) {
        throw 'The native source ZIP does not contain every committed native source file.'
    }
}
finally { $projectArchive.Dispose() }
Invoke-PackTool -File git -Arguments @('-C', $PdfSourceDirectory, 'archive', '--format=zip', '--prefix=PDF4QT/', "--output=$sourceInputs/PDF4QT-$($lock.pdf4qt.version)-source.zip", $lock.pdf4qt.commit)
Invoke-PackTool -File git -Arguments @('-C', $VcpkgRoot, 'archive', '--format=zip', '--prefix=vcpkg/', "--output=$sourceInputs/vcpkg-port-snapshot.zip", $lock.vcpkg.commit)
foreach ($module in $lock.qt.sourceArchives) {
    Copy-Tree (Join-Path $QtSourceDirectory $module) (Join-Path $sourceInputs "Qt/$module")
}
$extractedSources = [Collections.Generic.List[string]]::new()
foreach ($port in Get-ChildItem -LiteralPath (Join-Path $VcpkgRoot 'buildtrees') -Directory) {
    $inputSource = Join-Path $port.FullName 'src'
    if (-not (Test-Path -LiteralPath $inputSource -PathType Container)) { continue }
    Copy-Tree $inputSource (Join-Path $sourceInputs "vcpkg-built-source/$($port.Name)/src")
    $extractedSources.Add($port.Name)
}
if ($extractedSources.Count -eq 0) { throw 'No extracted dependency sources were captured.' }
Copy-Tree $licenses (Join-Path $sourceBundle 'licenses')
Copy-Tree (Join-Path $portable 'build-metadata') (Join-Path $sourceBundle 'build-metadata')
Write-Utf8 (Join-Path $sourceBundle 'SOURCE-INPUTS.json') (ConvertTo-Json -InputObject ([ordered]@{
    dependencyLock = $lock
    projectCommit = $sourceCommit
    extractedVcpkgSourcePorts = $extractedSources.ToArray()
    rebuildInstructions = 'source-inputs/ProperNotes-native-source.zip -> propernotes-native/cloud/README.md'
    note = 'Archive inputs and recorded build metadata; no claim of byte-identical output from a changing hosted compiler image.'
}) -Depth 15)
Write-Utf8 (Join-Path $sourceBundle 'README.md') @'
# Matching source inputs

Extract the project snapshot and read propernotes-native/cloud/README.md. The PDF4QT
and vcpkg ZIPs identify exact upstream commits. Qt source is in source-inputs/Qt;
actual extracted/patched vcpkg dependency source is in source-inputs/vcpkg-built-source.
The vcpkg port snapshot includes source download hashes and patch/build definitions.
licenses/vcpkg includes installed package SPDX records and copyright notices.
This archive does not include proprietary compiler/SDK binaries or build-only tools.
Those tools are prerequisites; their versions/image identity are recorded where available.
The application was compiled and packaged in the cloud, but not started or device-tested.
'@
. (Join-Path $PSScriptRoot 'File-LicenseIndex.ps1')
$indexArguments = @{
    ProjectDirectory = $ProjectDirectory; BuildDirectory = $BuildDirectory; QtRoot = $QtRoot
    PdfSourceDirectory = $PdfSourceDirectory; VcpkgRoot = $VcpkgRoot; InstalledDirectory = $InstalledDirectory
    CrtDirectory = $crt[0].FullName; Lock = $lock
}
Write-FileLicenseIndex -Directory $portable -Kind portable @indexArguments
Write-FileLicenseIndex -Directory $sourceBundle -Kind sources @indexArguments
Write-Hashes $portable
Write-Hashes $sourceBundle
[IO.Compression.ZipFile]::CreateFromDirectory($portable, (Join-Path $ArtifactDirectory "$PackageName.zip"), [IO.Compression.CompressionLevel]::Optimal, $true)
[IO.Compression.ZipFile]::CreateFromDirectory($sourceBundle, (Join-Path $ArtifactDirectory "$PackageName-sources.zip"), [IO.Compression.CompressionLevel]::Optimal, $true)
$archiveHashes = foreach ($archive in Get-ChildItem -LiteralPath $ArtifactDirectory -File -Filter '*.zip' | Sort-Object Name) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $archive.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $archive.Name
}
Write-Utf8 (Join-Path $ArtifactDirectory 'ARTIFACT-SHA256SUMS.txt') (($archiveHashes -join "`n") + "`n")
Write-Host 'Portable and matching source archives assembled. Application launch and tests were not performed.'
