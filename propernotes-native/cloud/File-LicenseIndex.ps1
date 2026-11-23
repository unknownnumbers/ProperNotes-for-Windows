# SPDX-License-Identifier: GPL-3.0-only
# Component/source evidence inventory. It deliberately leaves unproven license conclusions NOASSERTION.
Set-StrictMode -Version Latest

function Get-TextSpdxIdentifier {
    param([string]$Text)
    $match = [regex]::Match($Text, '(?m)SPDX-License-Identifier:\s*([^\r\n]+)')
    if ($match.Success) { return $match.Groups[1].Value.Trim().TrimEnd('*', '/', '-', ' ') }
    return $null
}

function Get-VcpkgPortEvidence {
    param([string]$Port, [string]$VcpkgRoot, [string]$PdfSourceDirectory, [string]$InstalledDirectory, [object]$Lock)
    $overlayFile = Join-Path $PdfSourceDirectory "vcpkg/overlays/general/$Port/vcpkg.json"
    $portFile = Join-Path $VcpkgRoot "ports/$Port/vcpkg.json"
    $sourceUrl = "https://github.com/microsoft/vcpkg/blob/$($Lock.vcpkg.commit)/ports/$Port/vcpkg.json"
    if (Test-Path -LiteralPath $overlayFile) {
        $portFile = $overlayFile
        $sourceUrl = "https://github.com/JakubMelka/PDF4QT/blob/$($Lock.pdf4qt.commit)/vcpkg/overlays/general/$Port/vcpkg.json"
    }
    $declared = 'NOASSERTION'
    $version = $null
    if (Test-Path -LiteralPath $portFile) {
        $metadata = Get-Content -LiteralPath $portFile -Raw | ConvertFrom-Json
        if ($metadata.PSObject.Properties.Name -contains 'license' -and $metadata.license) { $declared = [string]$metadata.license }
        foreach ($field in @('version', 'version-semver', 'version-date', 'version-string')) {
            if ($metadata.PSObject.Properties.Name -contains $field) { $version = [string]$metadata.$field; break }
        }
    }
    return [ordered]@{
        component = "vcpkg:$Port"
        version = $version
        licenseDeclared = $declared
        licenseConcluded = 'NOASSERTION'
        licenseEvidence = @("licenses/vcpkg/$Port", $sourceUrl)
        source = "source-inputs/vcpkg-built-source/$Port/src"
        note = 'Port declaration and actual package notices are evidence; bundled subcomponents may have additional terms.'
    }
}

function Write-FileLicenseIndex {
    param(
        [string]$Directory, [ValidateSet('portable', 'sources')][string]$Kind,
        [string]$ProjectDirectory, [string]$BuildDirectory, [string]$QtRoot,
        [string]$PdfSourceDirectory, [string]$VcpkgRoot, [string]$InstalledDirectory,
        [string]$CrtDirectory, [object]$Lock
    )
    $binaryHashes = @{}
    $qtNames = @{}
    $portOwners = @{}
    $portEvidence = @{}
    $lucideMetadataPath = Join-Path $ProjectDirectory 'licenses/lucide-source.json'
    $lucideLicensePath = Join-Path $ProjectDirectory 'licenses/lucide.txt'
    if (-not (Test-Path -LiteralPath $lucideMetadataPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $lucideLicensePath -PathType Leaf)) {
        throw 'Lucide source provenance and its complete upstream license are required.'
    }
    $lucide = Get-Content -LiteralPath $lucideMetadataPath -Raw | ConvertFrom-Json
    if ($lucide.schemaVersion -ne 1 -or $lucide.repository -cne 'https://github.com/lucide-icons/lucide' -or
        $lucide.commit -cnotmatch '^[0-9a-f]{40}$' -or $lucide.license.path -cne 'licenses/lucide.txt' -or
        $lucide.license.upstreamPath -cne 'LICENSE' -or $lucide.license.licenseExpression -cne 'ISC AND MIT' -or
        $lucide.license.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        (Get-FileHash -LiteralPath $lucideLicensePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $lucide.license.sha256) {
        throw 'Lucide provenance or license identity is invalid.'
    }
    $lucideAssets = @{}
    foreach ($asset in $lucide.assets) {
        if ($asset.path -cnotmatch '^src/icons/lucide/[a-z0-9-]+\.svg$' -or
            $asset.upstreamPath -cnotmatch '^icons/[a-z0-9-]+\.svg$' -or
            $asset.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $lucideAssets.ContainsKey($asset.path)) {
            throw 'Unexpected or duplicate Lucide asset provenance.'
        }
        $source = Join-Path $ProjectDirectory $asset.path
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
            (Get-Item -LiteralPath $source).Length -ne $asset.bytes -or
            (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -cne $asset.sha256) {
            throw "Lucide asset differs from its recorded upstream bytes: $($asset.path)"
        }
        $lucideAssets[$asset.path] = $asset
    }
    if ($lucideAssets.Count -eq 0 -or
        @(Get-ChildItem -LiteralPath (Join-Path $ProjectDirectory 'src/icons/lucide') -File -Filter '*.svg').Count -ne $lucideAssets.Count) {
        throw 'Lucide asset inventory is empty or incomplete.'
    }

    function Get-LucideEvidence {
        param([string]$AssetPath = '')
        $upstream = if ($AssetPath) { $lucideAssets[$AssetPath].upstreamPath } else { $lucide.license.upstreamPath }
        return [ordered]@{
            component = 'Lucide SVG icons and retained Feather attribution'
            licenseDeclared = 'ISC AND MIT'
            licenseConcluded = 'ISC AND MIT'
            licenseEvidence = @('licenses/Lucide/LICENSE.txt', 'licenses/Lucide/source.json')
            source = "$($lucide.repository)/blob/$($lucide.commit)/$upstream"
            note = 'The upstream combined ISC and Feather MIT notice is retained. Individual icon ancestry is not asserted separately; no ProperNotes GPL relicensing is applied to these upstream assets.'
        }
    }
    function Get-LucideMetadataEvidence {
        return [ordered]@{
            component = 'Lucide source provenance measurements'
            licenseConcluded = 'NOASSERTION'
            licenseEvidence = @('licenses/Lucide/LICENSE.txt')
            source = "$($lucide.repository)/tree/$($lucide.commit)"
            note = 'Generated source identities, hashes and upstream references. No new copyright or license assertion over factual metadata.'
        }
    }
    $infoRoot = Join-Path $InstalledDirectory 'vcpkg/info'
    foreach ($list in Get-ChildItem -LiteralPath $infoRoot -File -Filter '*.list') {
        $port = $list.BaseName.Split('_')[0]
        foreach ($line in Get-Content -LiteralPath $list.FullName) {
            if ($line -match '^[^/]+/bin/([^/]+\.dll)$') { $portOwners[$Matches[1].ToLowerInvariant()] = $port }
        }
    }
    foreach ($inputKind in @('application', 'PDF4QT', 'Qt', 'vcpkg', 'MSVC')) {
        $inputRoot = switch ($inputKind) {
            'application' { $BuildDirectory }
            'PDF4QT' { $BuildDirectory }
            'Qt' { $QtRoot }
            'vcpkg' { Join-Path $InstalledDirectory "$($Lock.vcpkg.triplet)/bin" }
            'MSVC' { $CrtDirectory }
        }
        $inputFiles = switch ($inputKind) {
            'application' { @(Get-ChildItem -LiteralPath $inputRoot -File -Recurse -Filter $Lock.application.executable) }
            'PDF4QT' { @(Get-ChildItem -LiteralPath $inputRoot -File -Recurse -Filter 'Pdf4QtLibCore.dll') }
            default { @(Get-ChildItem -LiteralPath $inputRoot -File -Recurse -Filter '*.dll') }
        }
        foreach ($inputFile in $inputFiles) {
            $sourcePath = [IO.Path]::GetRelativePath($inputRoot, $inputFile.FullName).Replace('\', '/')
            $sourceHash = (Get-FileHash -LiteralPath $inputFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $evidence = [ordered]@{ component = $inputKind; source = "$inputKind/$sourcePath"; inputSha256 = $sourceHash; licenseConcluded = 'NOASSERTION'; licenseEvidence = @() }
            switch ($inputKind) {
                'application' {
                    $evidence.licenseConcluded = 'GPL-3.0-only'
                    $evidence.licenseEvidence = @('LICENSE', 'COPYRIGHT.md')
                    $evidence['embeddedResources'] = @((Get-LucideEvidence))
                    $evidence['note'] = 'GPL-3.0-only describes the original application code. Embedded Lucide/Feather SVG resources retain the separately recorded ISC and MIT terms.'
                }
                'PDF4QT' {
                    $evidence.licenseConcluded = 'MIT AND BSD-3-Clause AND OFL-1.1'
                    $evidence.licenseEvidence = @('licenses/PDF4QT/LICENSE', 'licenses/PDF4QT/cmaps/LICENSE.txt', 'licenses/PDF4QT/liberation-fonts/LICENSE')
                }
                'Qt' {
                    $module = if ($inputFile.Name -match '(?i)(Qt6Svg|qsvg)') { 'qtsvg' } else { 'qtbase' }
                    $evidence.component = "Qt:$module"
                    $evidence.licenseEvidence = @("licenses/Qt/$module/LICENSES", 'licenses/Qt/attribution-inventory.json')
                    $evidence['licenseDeclared'] = 'Qt open-source terms and separately licensed third-party components; see supplied texts.'
                    $qtNames[$inputFile.Name.ToLowerInvariant()] = $evidence
                }
                'vcpkg' {
                    $name = $inputFile.Name.ToLowerInvariant()
                    if ($portOwners.ContainsKey($name)) {
                        $port = $portOwners[$name]
                        if (-not $portEvidence.ContainsKey($port)) {
                            $portEvidence[$port] = Get-VcpkgPortEvidence $port $VcpkgRoot $PdfSourceDirectory $InstalledDirectory $Lock
                        }
                        foreach ($key in $portEvidence[$port].Keys) { $evidence[$key] = $portEvidence[$port][$key] }
                        $evidence.source = "vcpkg-installed/$($Lock.vcpkg.triplet)/bin/$($inputFile.Name)"
                    }
                }
                'MSVC' { $evidence['licenseDeclared'] = 'LicenseRef-Microsoft-Visual-Cpp-Redistributable'; $evidence.licenseEvidence = @('licenses/MSVC/REDISTRIBUTION.md') }
            }
            $binaryHashes[$sourceHash] = $evidence
        }
    }

    function Get-PathEvidence {
        param([string]$Relative, [string]$TextPrefix)
        $evidence = [ordered]@{ component = 'unresolved'; licenseConcluded = 'NOASSERTION'; licenseEvidence = @(); source = $null }
        if ($Relative -match '^licenses/(?:Lucide/source\.json|project/legal-texts/lucide-source\.json)$') {
            return Get-LucideMetadataEvidence
        }
        if ($Relative -match '^licenses/(?:Lucide/LICENSE\.txt|project/legal-texts/lucide\.txt)$') {
            return Get-LucideEvidence
        }
        if ($Relative -match '^licenses/Qt/(?<module>[^/]+)/') {
            $evidence.component = "Qt:$($Matches.module) notices"; $evidence.licenseEvidence = @('licenses/Qt/attribution-inventory.json'); return $evidence
        }
        if ($Relative -match '^licenses/vcpkg/(?<port>[^/]+)/') {
            $port = $Matches.port
            if (-not $portEvidence.ContainsKey($port)) { $portEvidence[$port] = Get-VcpkgPortEvidence $port $VcpkgRoot $PdfSourceDirectory $InstalledDirectory $Lock }
            return $portEvidence[$port]
        }
        if ($Relative -match '^licenses/PDF4QT/') { $evidence.component = 'PDF4QT notices'; $evidence.licenseEvidence = @('licenses/PDF4QT/LICENSE'); return $evidence }
        if ($Relative -match '^licenses/MSVC/') { $evidence.component = 'MSVC notices'; $evidence.licenseEvidence = @('licenses/MSVC/REDISTRIBUTION.md'); return $evidence }
        if ($Relative -match '(^|/)CC-BY-SA-4\.0\.txt$') { $evidence.component = 'Creative Commons legal code'; return $evidence }
        if ($Relative -match '^(build-metadata/|SOURCE-INPUTS\.json$|SHA256SUMS\.txt$|file-license-index\.json$)') {
            $evidence.component = 'generated factual metadata'; $evidence['note'] = 'No new copyright or license assertion over measurements and file identities.'; return $evidence
        }
        if ($Relative -match '^source-inputs/Qt/(?<module>[^/]+)/') {
            $evidence.component = "Qt:$($Matches.module) source"
            $evidence.licenseEvidence = @("licenses/Qt/$($Matches.module)/LICENSES", 'licenses/Qt/attribution-inventory.json')
            $identifier = Get-TextSpdxIdentifier $TextPrefix
            if ($identifier) { $evidence.licenseConcluded = $identifier }
            return $evidence
        }
        if ($Relative -match '^source-inputs/vcpkg-built-source/(?<port>[^/]+)/') {
            $port = $Matches.port
            if (-not $portEvidence.ContainsKey($port)) { $portEvidence[$port] = Get-VcpkgPortEvidence $port $VcpkgRoot $PdfSourceDirectory $InstalledDirectory $Lock }
            $evidence = [ordered]@{}
            foreach ($key in $portEvidence[$port].Keys) { $evidence[$key] = $portEvidence[$port][$key] }
            $identifier = Get-TextSpdxIdentifier $TextPrefix
            if ($identifier) { $evidence.licenseConcluded = $identifier }
            return $evidence
        }
        if ($Relative -match '^source-inputs/ProperNotes-native-source\.zip!/(?:propernotes-native/)?(?<inner>.+)$') {
            $inner = $Matches.inner
            if ($inner -eq 'licenses/lucide-source.json') { return Get-LucideMetadataEvidence }
            if ($inner -eq 'licenses/lucide.txt') { return Get-LucideEvidence }
            if ($inner -match '^src/icons/lucide/.+\.svg$') {
                if (-not $lucideAssets.ContainsKey($inner)) { throw "Unrecorded Lucide source asset: $inner" }
                return Get-LucideEvidence $inner
            }
            $evidence.component = 'ProperNotes original source'
            $evidence.licenseEvidence = @('licenses/project/LICENSE', 'licenses/project/LICENSE-DOCS', 'licenses/project/COPYRIGHT.md')
            if ($inner -match '(^|/)(LICENSE|CC-BY-SA-4\.0\.txt)$') { return $evidence }
            if ($inner -match '(?i)\.md$|(^|/)LICENSE-DOCS$') { $evidence.licenseConcluded = 'CC-BY-SA-4.0' }
            else { $evidence.licenseConcluded = 'GPL-3.0-only' }
            return $evidence
        }
        if ($Relative -match '^source-inputs/PDF4QT-[^/]+-source\.zip!/PDF4QT/(?<inner>.+)$') {
            $inner = $Matches.inner
            $evidence.component = 'PDF4QT source'; $evidence.licenseEvidence = @('licenses/PDF4QT/LICENSE')
            if ($inner -match '^Pdf4QtLibCore/cmaps/' -and $inner -notmatch 'LICENSE') { $evidence.licenseConcluded = 'BSD-3-Clause' }
            elseif ($inner -match '^Pdf4QtLibCore/liberation-fonts-ttf/.+\.ttf$') { $evidence.licenseConcluded = 'OFL-1.1' }
            elseif ($TextPrefix -match '(?s)MIT License.+Permission is hereby granted') { $evidence.licenseConcluded = 'MIT' }
            return $evidence
        }
        if ($Relative -match '^source-inputs/vcpkg-port-snapshot\.zip!/') {
            $evidence.component = 'vcpkg ports, versions, and build source'
            $evidence.licenseEvidence = @("https://github.com/microsoft/vcpkg/blob/$($Lock.vcpkg.commit)/LICENSE.txt")
            $identifier = Get-TextSpdxIdentifier $TextPrefix
            if ($identifier) { $evidence.licenseConcluded = $identifier }
            return $evidence
        }
        if ($Relative -match '^source-inputs/.+\.zip$') { $evidence.component = 'mixed-license source archive'; return $evidence }
        if ($Relative -match '(^|/)LICENSE$') { $evidence.component = 'upstream license text'; return $evidence }
        if ($Relative -match '(?i)\.md$|(^|/)LICENSE-DOCS$') {
            $evidence.component = 'ProperNotes original documentation'; $evidence.licenseConcluded = 'CC-BY-SA-4.0'; $evidence.licenseEvidence = @('licenses/project/LICENSE-DOCS'); return $evidence
        }
        if ($Relative -match '(?i)\.cmd$|^qt\.conf$') {
            $evidence.component = 'ProperNotes launcher/configuration'; $evidence.licenseConcluded = 'GPL-3.0-only'; $evidence.licenseEvidence = @('LICENSE'); return $evidence
        }
        return $evidence
    }

    $indexPath = Join-Path $Directory 'file-license-index.json'
    $hashPath = Join-Path $Directory 'SHA256SUMS.txt'
    foreach ($placeholder in @($indexPath, $hashPath)) {
        if (-not (Test-Path -LiteralPath $placeholder)) { [IO.File]::WriteAllText($placeholder, '', [Text.UTF8Encoding]::new($false)) }
    }
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($file in Get-ChildItem -LiteralPath $Directory -File -Recurse -Force | Sort-Object FullName) {
        $relative = [IO.Path]::GetRelativePath($Directory, $file.FullName).Replace('\', '/')
        $selfMetadata = $relative -in @('file-license-index.json', 'SHA256SUMS.txt')
        $hash = if ($selfMetadata) { $null } else { (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        $prefix = ''
        if ($file.Extension -notin @('.zip', '.dll', '.exe', '.ttf', '.png', '.jpg', '.jpeg', '.pdf') -and $file.Length -lt 2097152) {
            $prefix = [IO.File]::ReadAllText($file.FullName)
            if ($prefix.Length -gt 16384) { $prefix = $prefix.Substring(0, 16384) }
        }
        $evidence = Get-PathEvidence $relative $prefix
        if ($file.Extension -in @('.dll', '.exe')) {
            if ($binaryHashes.ContainsKey($hash)) { $evidence = $binaryHashes[$hash] }
            elseif ($qtNames.ContainsKey($file.Name.ToLowerInvariant())) {
                $candidate = $qtNames[$file.Name.ToLowerInvariant()]
                $evidence = [ordered]@{}
                foreach ($key in $candidate.Keys) { $evidence[$key] = $candidate[$key] }
                $evidence['sourceIdentityRequiresReview'] = $true
                $evidence['note'] = 'Name matches a Qt input, but deployed bytes differ. Deployment-tool patching is possible; byte equivalence is not asserted.'
            }
        }
        $record = [ordered]@{ path = $relative; sha256 = $hash; size = if ($selfMetadata) { $null } else { $file.Length }; evidence = $evidence }
        if ($selfMetadata) { $record['hashExclusion'] = 'Avoids index/manifest self-reference; the external archive checksum covers both files.' }
        $entries.Add($record)
        if ($Kind -eq 'sources' -and $file.Extension -eq '.zip') {
            $archive = [IO.Compression.ZipFile]::OpenRead($file.FullName)
            try {
                foreach ($entry in $archive.Entries | Where-Object { $_.Name } | Sort-Object FullName) {
                    $stream = $entry.Open()
                    $hasher = [Security.Cryptography.SHA256]::Create()
                    try { $entryHash = [Convert]::ToHexString($hasher.ComputeHash($stream)).ToLowerInvariant() }
                    finally { $hasher.Dispose(); $stream.Dispose() }
                    $prefix = ''
                    if ($entry.Length -lt 2097152 -and [IO.Path]::GetExtension($entry.FullName) -notin @('.zip', '.dll', '.exe', '.ttf', '.png', '.jpg', '.pdf')) {
                        $stream = $entry.Open(); $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
                        try { $characters = [char[]]::new(16384); $count = $reader.ReadBlock($characters, 0, $characters.Length); $prefix = [string]::new($characters, 0, $count) }
                        finally { $reader.Dispose(); $stream.Dispose() }
                    }
                    $virtualPath = "$relative!/$($entry.FullName)"
                    $entries.Add([ordered]@{ path = $virtualPath; sha256 = $entryHash; size = $entry.Length; archiveEntry = $true; evidence = (Get-PathEvidence $virtualPath $prefix) })
                }
            }
            finally { $archive.Dispose() }
        }
    }
    $unknown = @($entries | Where-Object { $_.evidence.licenseConcluded -eq 'NOASSERTION' }).Count
    $document = [ordered]@{
        schemaVersion = 1; packageKind = $Kind; indexedEntries = $entries.Count; noAssertionEntries = $unknown
        scope = 'Every outer file and, for source ZIP inputs, each nested ZIP entry. License texts retain their own terms; components are not relicensed by their container.'
        meaning = 'NOASSERTION is an explicit unresolved conclusion, not missing coverage or permission. License evidence and source identity are recorded independently.'
        files = $entries.ToArray()
    }
    [IO.File]::WriteAllText($indexPath, (ConvertTo-Json -InputObject $document -Depth 30), [Text.UTF8Encoding]::new($false))
    Write-Host "File/component index ($Kind): $($entries.Count) entries; $unknown license conclusions explicitly NOASSERTION."
}
