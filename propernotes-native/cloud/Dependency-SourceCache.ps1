# SPDX-License-Identifier: GPL-3.0-only
# Functions only. Build-Native.ps1 supplies the hosted-runner guard before calling these.
Set-StrictMode -Version Latest

function Copy-DependencySourceTree {
    param([string]$Source, [string]$Destination)
    foreach ($file in Get-ChildItem -LiteralPath $Source -File -Recurse -Force) {
        $relative = [IO.Path]::GetRelativePath($Source, $file.FullName)
        $target = Join-Path $Destination $relative
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}

function Restore-DependencySources {
    param([string]$CacheRoot, [string]$VcpkgRoot, [string]$InputFingerprint)
    $sourceRoot = Join-Path $CacheRoot 'patched-source'
    $indexPath = Join-Path $sourceRoot 'source-index.json'
    if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) { return $false }
    try { $index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json }
    catch { return $false }
    foreach ($field in @('schemaVersion', 'inputFingerprint', 'files')) {
        if ($index.PSObject.Properties.Name -notcontains $field) { return $false }
    }
    if ($index.schemaVersion -ne 1 -or $index.inputFingerprint -cne $InputFingerprint) { return $false }
    $portsRoot = Join-Path $sourceRoot 'ports'
    if (-not (Test-Path -LiteralPath $portsRoot -PathType Container)) { return $false }
    $cachedFiles = @(Get-ChildItem -LiteralPath $portsRoot -File -Recurse -Force)
    if ($cachedFiles.Count -ne @($index.files).Count -or $cachedFiles.Count -eq 0) { return $false }
    $resolvedPrefix = [IO.Path]::GetFullPath($portsRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    # Verify all source bytes before permitting the corresponding binary cache to be read.
    foreach ($record in $index.files) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $portsRoot $record.path))
        if (-not $candidate.StartsWith($resolvedPrefix, [StringComparison]::OrdinalIgnoreCase)) { return $false }
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $false }
        if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.sha256) { return $false }
    }
    foreach ($port in Get-ChildItem -LiteralPath $portsRoot -Directory) {
        Copy-DependencySourceTree $port.FullName (Join-Path $VcpkgRoot "buildtrees/$($port.Name)")
    }
    return $true
}

function Save-DependencySources {
    param([string]$CacheRoot, [string]$VcpkgRoot, [string]$InputFingerprint, [string[]]$RequiredPorts)
    $sourceRoot = Join-Path $CacheRoot 'patched-source'
    $portsRoot = Join-Path $sourceRoot 'ports'
    [IO.Directory]::CreateDirectory($portsRoot) | Out-Null
    foreach ($port in Get-ChildItem -LiteralPath (Join-Path $VcpkgRoot 'buildtrees') -Directory) {
        $source = Join-Path $port.FullName 'src'
        if (Test-Path -LiteralPath $source -PathType Container) {
            Copy-DependencySourceTree $source (Join-Path $portsRoot "$($port.Name)/src")
        }
    }
    foreach ($portName in $RequiredPorts) {
        $source = Join-Path $portsRoot "$portName/src"
        if (-not (Test-Path -LiteralPath $source -PathType Container) -or
            @(Get-ChildItem -LiteralPath $source -Recurse -File -Force).Count -eq 0) {
            throw "Required dependency source missing; do not cache or distribute binaries without it: $portName"
        }
    }
    $files = @(foreach ($file in Get-ChildItem -LiteralPath $portsRoot -Recurse -File -Force | Sort-Object FullName) {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($portsRoot, $file.FullName).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    $indexPath = Join-Path $sourceRoot 'source-index.json'
    $json = ConvertTo-Json -InputObject ([ordered]@{ schemaVersion = 1; inputFingerprint = $InputFingerprint; files = $files }) -Depth 8
    [IO.File]::WriteAllText($indexPath, $json, [Text.UTF8Encoding]::new($false))
    $binaryEntries = @(Get-ChildItem -LiteralPath (Join-Path $CacheRoot 'binary') -Recurse -File -Filter '*.zip' -ErrorAction SilentlyContinue |
        Sort-Object Name | ForEach-Object { "$($_.Name) $($_.Length)" })
    $stateText = (Get-FileHash -LiteralPath $indexPath -Algorithm SHA256).Hash + "`n" + ($binaryEntries -join "`n")
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $state = [Convert]::ToHexString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($stateText))).ToLowerInvariant() }
    finally { $hasher.Dispose() }
    $statePath = Join-Path $CacheRoot 'cache-state.txt'
    $priorState = if (Test-Path -LiteralPath $statePath) { (Get-Content -LiteralPath $statePath -Raw).Trim() } else { '' }
    [IO.File]::WriteAllText($statePath, $state, [Text.UTF8Encoding]::new($false))
    return $priorState -cne $state
}
