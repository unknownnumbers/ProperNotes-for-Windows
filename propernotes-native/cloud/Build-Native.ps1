# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$WorkDirectory,
    [Parameter(Mandatory = $true)][string]$ArtifactDirectory,
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string]$DependencyCacheDirectory = $env:PROPERNOTES_DEPENDENCY_CACHE,
    [string]$PackageName = 'ProperNotes-for-Windows-0.3.11-native-preview.1-win-x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'This dependency-installing build entry point is restricted to a GitHub-hosted Windows runner.'
}
if ($PackageName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]+$') { throw 'Invalid package name.' }

function Invoke-BuildTool {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Build tool failed ($LASTEXITCODE): $File" }
}

function Get-PinnedCheckout {
    param([string]$Repository, [string]$Commit, [string]$Destination)
    if (Test-Path -LiteralPath $Destination) { throw "Checkout destination already exists: $Destination" }
    New-Item -ItemType Directory -Path $Destination | Out-Null
    Invoke-BuildTool -File git -Arguments @('init', '--quiet', $Destination)
    Invoke-BuildTool -File git -Arguments @('-C', $Destination, 'remote', 'add', 'origin', $Repository)
    Invoke-BuildTool -File git -Arguments @('-C', $Destination, 'fetch', '--quiet', '--depth=1', 'origin', $Commit)
    Invoke-BuildTool -File git -Arguments @('-C', $Destination, 'checkout', '--quiet', '--detach', 'FETCH_HEAD')
    $actual = (& git -C $Destination rev-parse HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $actual -cne $Commit) { throw 'Upstream checkout identity mismatch.' }
}

$project = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $PSScriptRoot 'dependencies.lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
if (-not $QtRoot -or -not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin/windeployqt.exe'))) {
    throw 'The pinned Qt binary installation is required. The workflow installs it on the runner.'
}
$qmake = Join-Path $QtRoot 'bin/qmake.exe'
$qtVersion = (& $qmake -query QT_VERSION | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $qtVersion -cne $lock.qt.version) { throw 'Qt version differs from dependencies.lock.json.' }
$qtSource = Join-Path (Split-Path -Parent $QtRoot) 'Src'
foreach ($module in $lock.qt.sourceArchives) {
    if (-not (Test-Path -LiteralPath (Join-Path $qtSource "$module/CMakeLists.txt"))) {
        throw "Missing matching Qt source module: $module"
    }
    $moduleConfiguration = Get-Content -LiteralPath (Join-Path $qtSource "$module/.cmake.conf") -Raw
    $sourceVersion = [regex]::Match($moduleConfiguration, 'set\(QT_REPO_MODULE_VERSION\s+"([^"]+)"\)').Groups[1].Value
    if ($sourceVersion -cne $lock.qt.version) { throw "Qt source version mismatch: $module" }
}
$manifestDirectory = Join-Path $PSScriptRoot 'vcpkg'
$manifest = Get-Content -LiteralPath (Join-Path $manifestDirectory 'vcpkg.json') -Raw | ConvertFrom-Json
if ($manifest.'builtin-baseline' -cne $lock.vcpkg.commit) { throw 'vcpkg baseline differs from the lock.' }
if (@($manifest.dependencies | Where-Object { $_ -match 'qt|tesseract|leptonica|tbb' }).Count -ne 0) {
    throw 'The native core manifest must not install another Qt or the excluded OCR/test dependencies.'
}
if (Test-Path -LiteralPath $WorkDirectory) { throw 'Use a fresh build work directory.' }
New-Item -ItemType Directory -Path $WorkDirectory | Out-Null
if (Test-Path -LiteralPath $ArtifactDirectory) { throw 'Use a fresh artifact directory.' }
New-Item -ItemType Directory -Path $ArtifactDirectory | Out-Null
$work = (Resolve-Path -LiteralPath $WorkDirectory).Path
$artifacts = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$pdfSource = Join-Path $work 'PDF4QT'
$vcpkgRoot = Join-Path $work 'vcpkg'
$installed = Join-Path $work 'installed'
$build = Join-Path $work 'build'
Get-PinnedCheckout -Repository $lock.pdf4qt.repository -Commit $lock.pdf4qt.commit -Destination $pdfSource
Get-PinnedCheckout -Repository $lock.vcpkg.repository -Commit $lock.vcpkg.commit -Destination $vcpkgRoot
$upstreamManifest = Get-Content -LiteralPath (Join-Path $pdfSource 'vcpkg.json') -Raw | ConvertFrom-Json
if ($upstreamManifest.'builtin-baseline' -cne $lock.vcpkg.commit) { throw 'The upstream baseline changed unexpectedly.' }
$env:VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_DOWNLOADS = Join-Path $work 'downloads'
$env:VCPKG_DISABLE_METRICS = '1'
if (-not $DependencyCacheDirectory) { $DependencyCacheDirectory = Join-Path $work 'dependency-cache' }
[IO.Directory]::CreateDirectory((Join-Path $DependencyCacheDirectory 'binary')) | Out-Null
. (Join-Path $PSScriptRoot 'Dependency-SourceCache.ps1')
$inputFingerprint = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash + ':' +
    (Get-FileHash -LiteralPath (Join-Path $manifestDirectory 'vcpkg.json') -Algorithm SHA256).Hash
$restoredSources = Restore-DependencySources -CacheRoot $DependencyCacheDirectory -VcpkgRoot $vcpkgRoot -InputFingerprint $inputFingerprint
$cacheMode = if ($restoredSources) { 'readwrite' } else { 'write' }
$env:VCPKG_BINARY_SOURCES = "clear;files,$DependencyCacheDirectory/binary,$cacheMode"
Write-Host "Dependency source cache verified: $restoredSources. Binary cache mode: $cacheMode."
Invoke-BuildTool -File (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -Arguments @('-disableMetrics')
$overlay = Join-Path $pdfSource $lock.vcpkg.overlay
Invoke-BuildTool -File (Join-Path $vcpkgRoot 'vcpkg.exe') -Arguments @(
    'install', "--x-manifest-root=$manifestDirectory", "--x-install-root=$installed",
    "--triplet=$($lock.vcpkg.triplet)", "--host-triplet=$($lock.vcpkg.triplet)", "--overlay-ports=$overlay"
)
# Do not clean buildtrees: extracted, patched dependency source is included with the source artifact.
$cacheChanged = Save-DependencySources -CacheRoot $DependencyCacheDirectory -VcpkgRoot $vcpkgRoot `
    -InputFingerprint $inputFingerprint -RequiredPorts @($manifest.dependencies)
$env:PROPERNOTES_SOURCE_CACHE_RESTORED = $restoredSources.ToString().ToLowerInvariant()
if ($env:GITHUB_OUTPUT) {
    Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value 'dependencies-ready=true'
    Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "cache-changed=$($cacheChanged.ToString().ToLowerInvariant())"
}
# Dependency cache is now complete, even if the subsequent application compilation fails.
$configure = @(
    '-S', $project, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DPDF4QT_SOURCE_DIR=$pdfSource", "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_INSTALLED_DIR=$installed", "-DVCPKG_TARGET_TRIPLET=$($lock.vcpkg.triplet)",
    "-DVCPKG_HOST_TRIPLET=$($lock.vcpkg.triplet)", '-DVCPKG_MANIFEST_INSTALL=OFF',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL',
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$build/bin", '-DBUILD_TESTING=OFF',
    '-DPDF4QT_BUILD_ONLY_CORE_LIBRARY=ON', '-DPDF4QT_BUILD_TESTS=OFF', '-DPDF4QT_ENABLE_OCR=OFF',
    '-DPDF4QT_OCR_REQUIRED=OFF', '-DPDF4QT_INSTALL_INCLUDE=OFF', '-DPDF4QT_INSTALL_TO_USR=OFF',
    '-DPDF4QT_INSTALL_DEPENDENCIES=OFF', '-DPDF4QT_INSTALL_QT_DEPENDENCIES=OFF',
    '-DPDF4QT_INSTALL_MSVC_REDISTRIBUTABLE=OFF', '-DPDF4QT_INSTALL_PREPARE_WIX_INSTALLER=OFF'
)
# VCPKG_TOOLCHAIN is the toolchain's own initialization guard. Do not predefine it:
# vcpkg.cmake returns early when it is already ON, before registering dependency paths.
Write-Host "CMake toolchain input: $vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
Write-Host "Installed dependency prefix: $installed/$($lock.vcpkg.triplet)"
try {
    Invoke-BuildTool -File cmake -Arguments $configure
}
finally {
    $cacheFile = Join-Path $build 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cacheFile) {
        Write-Host 'CMake dependency path metadata:'
        foreach ($line in Select-String -LiteralPath $cacheFile -Pattern '^(CMAKE_TOOLCHAIN_FILE|VCPKG_INSTALLED_DIR|_VCPKG_INSTALLED_DIR|VCPKG_TARGET_TRIPLET|VCPKG_HOST_TRIPLET|ZLIB_[A-Z_]+|OPENSSL_[A-Z_]+):') {
            Write-Host $line.Line
        }
    }
}
Invoke-BuildTool -File cmake -Arguments @('--build', $build, '--target', $lock.application.target, '--config', 'Release', '--parallel', '4')
# No ctest, executable launch, server, GUI smoke test, or installer invocation occurs here.
& (Join-Path $PSScriptRoot 'Deploy-Portable.ps1') -ProjectDirectory $project -BuildDirectory $build `
    -PdfSourceDirectory $pdfSource -VcpkgRoot $vcpkgRoot -InstalledDirectory $installed `
    -QtRoot $QtRoot -QtSourceDirectory $qtSource -ArtifactDirectory $artifacts -PackageName $PackageName
