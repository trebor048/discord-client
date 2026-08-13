[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('configure', 'build', 'test', 'deploy', 'clean', 'all')]
    [string]$Action = 'all',
    [switch]$Clean
)

# Acheron - single Windows build entry point (one-shot setup/build/install).
#
#   .\build.ps1               # download deps, configure, build, test, deploy
#   .\build.ps1 -Action build # just build (assumes configured)
#   .\build.ps1 -Action clean # remove build/ (use -Action all -Clean)
#   .\build.ps1 -Clean        # clean then configure+build+test+deploy
#
# On first run this downloads everything it needs (Qt 6 via aqtinstall, vcpkg,
# curl-impersonate) and produces a self-contained deployable copy of the app
# under build\deploy\RelWithDebInfo.
#
# Prerequisites (auto-detected):
#   - Visual Studio 2022 with the Desktop C++ workload and a Windows SDK
#   - git (used to clone vcpkg when missing)
#   - Python 3.7+ (only used to auto-download Qt when no kit is installed)

$ErrorActionPreference = 'Stop'

# build.ps1 lives at the repository root, so $PSScriptRoot is already the
# project root. Do NOT take Split-Path -Parent here (that climbs into the
# parent directory and breaks preset/vcpkg/curl resolution).
$ProjectRoot = $PSScriptRoot
$Preset = 'windows'
$BuildDir = Join-Path $ProjectRoot 'build'
$Configuration = 'RelWithDebInfo'

# curl-impersonate: prebuilt release, extracted into a cache dir under tools/.
$CurlImpersonateVersion = 'v2.0.0'
$CurlImpersonateArchive = "libcurl-impersonate-$CurlImpersonateVersion.x86_64-win32.tar.gz"
$CurlImpersonateBaseUrl = "https://github.com/lexiforest/curl-impersonate/releases/download/$CurlImpersonateVersion"
$CurlCache = Join-Path $ProjectRoot "tools/.cache/curl-impersonate/$CurlImpersonateVersion"
$CurlRoot = Join-Path $CurlCache 'curl-impersonate'
$CurlIncludeDir = Join-Path $CurlRoot 'include'
$CurlLibrary = Join-Path $CurlRoot 'lib\libcurl-impersonate_imp.lib'

# Qt 6: downloaded via aqtinstall on first run into tools/.cache when no
# installed kit (C:\Qt, QTDIR, or Qt6_DIR) provides a complete MSVC 2022 kit.
$QtVersion = '6.10.3'
$QtArch = 'win64_msvc2022_64'
$QtModules = @('qtimageformats', 'qtmultimedia')
$QtCache = Join-Path $ProjectRoot 'tools/.cache/Qt'

function Fail-Prerequisite([string]$Message) {
    throw "Build prerequisite failed: $Message"
}

function Find-VisualStudioDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $installPath) {
            $devCmd = Join-Path $installPath.Trim() 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $devCmd) { return $devCmd }
        }
    }
    Fail-Prerequisite 'Install Visual Studio 2022 with the Desktop development with C++ workload and a Windows 10/11 SDK.'
}

function Get-Qt6KitMissingFiles([string]$ConfigDir) {
    $kitRoot = (Resolve-Path -LiteralPath (Join-Path $ConfigDir '..\..\..')).Path
    return @(
        (Join-Path $ConfigDir '..\Qt6Multimedia\Qt6MultimediaConfig.cmake'),
        (Join-Path $kitRoot 'bin\windeployqt.exe'),
        (Join-Path $kitRoot 'bin\Qt6Multimedia.dll'),
        (Join-Path $kitRoot 'plugins\platforms\qwindows.dll'),
        (Join-Path $kitRoot 'plugins\platforms\qoffscreen.dll')
    ) | Where-Object { -not (Test-Path -LiteralPath $_) }
}

function Resolve-Qt6ConfigDirectory([string]$ConfigDir, [string]$SourceLabel, [switch]$Required) {
    $configPath = Join-Path $ConfigDir 'Qt6Config.cmake'
    if (-not (Test-Path -LiteralPath $configPath)) {
        if ($Required) { Fail-Prerequisite "$SourceLabel does not contain Qt6Config.cmake: $configPath" }
        return $null
    }
    $resolvedConfigDir = (Resolve-Path -LiteralPath $ConfigDir).Path
    $missing = Get-Qt6KitMissingFiles $resolvedConfigDir
    if ($missing) {
        if ($Required) { Fail-Prerequisite "$SourceLabel points to an incomplete Qt kit: $resolvedConfigDir. Missing: $($missing -join '; ')" }
        return $null
    }
    return $resolvedConfigDir
}

function Get-Qt6ConfigCandidatesFromRoot([string]$Root) {
    if (-not $Root -or -not (Test-Path -LiteralPath $Root)) { return @() }
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $candidates = @(
        (Join-Path $resolvedRoot 'lib\cmake\Qt6'),
        (Join-Path $resolvedRoot 'msvc2022_64\lib\cmake\Qt6')
    )
    $versionChildren = Get-ChildItem -Path $resolvedRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'msvc2022_64\lib\cmake\Qt6' }
    return @($candidates + $versionChildren) | Select-Object -Unique
}

function Find-Qt6Config {
    if ($env:Qt6_DIR) {
        return Resolve-Qt6ConfigDirectory $env:Qt6_DIR 'Qt6_DIR' -Required
    }
    $qtRoots = @()
    if ($env:QTDIR) { $qtRoots += $env:QTDIR }
    $qtRoots += 'C:\Qt'
    $qtRoots += $QtCache
    foreach ($qtRoot in $qtRoots | Where-Object { $_ } | Select-Object -Unique) {
        foreach ($configDir in Get-Qt6ConfigCandidatesFromRoot $qtRoot) {
            $resolved = Resolve-Qt6ConfigDirectory $configDir "Qt root '$qtRoot'"
            if ($resolved) { return $resolved }
        }
    }
    return $null
}

function Install-Qt6Kit {
    # Download a Qt kit with aqtinstall into tools/.cache/Qt so a machine with
    # only Visual Studio + git + Python can build without pre-installing Qt.
    $python = Get-Command python -CommandType Application -ErrorAction SilentlyContinue
    if (-not $python) {
        Fail-Prerequisite 'Python 3.7+ is required to auto-download Qt. Install Python (https://www.python.org/downloads/) or set Qt6_DIR to an existing kit.'
    }
    if ($PSCmdlet.ShouldProcess($QtCache, "Download Qt $QtVersion ($QtArch) via aqtinstall")) {
        Write-Host "Downloading Qt $QtVersion ($QtArch) to $QtCache (first run only)..."
        & $python.Source -m pip install --upgrade --quiet aqtinstall
        if ($LASTEXITCODE -ne 0) { Fail-Prerequisite 'Failed to install aqtinstall via pip.' }
        & $python.Source -m aqt install-qt windows desktop $QtVersion $QtArch -m $QtModules -O $QtCache
        if ($LASTEXITCODE -ne 0) { Fail-Prerequisite 'Qt download via aqtinstall failed.' }
    }
    foreach ($configDir in Get-Qt6ConfigCandidatesFromRoot $QtCache) {
        $resolved = Resolve-Qt6ConfigDirectory $configDir "downloaded Qt at $QtCache"
        if ($resolved) { return $resolved }
    }
    Fail-Prerequisite "Qt $QtVersion was downloaded but no complete MSVC 2022 kit was found under $QtCache."
}

function Ensure-Qt6Config {
    $qt6Dir = Find-Qt6Config
    if ($qt6Dir) { return $qt6Dir }
    Write-Host "No installed Qt 6 kit found; downloading one (first run only)..."
    return Install-Qt6Kit
}

function Ensure-Vcpkg {
    $vcpkgRoot = $env:VCPKG_ROOT
    if (-not $vcpkgRoot) { $vcpkgRoot = 'C:\vcpkg' }
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
        Write-Host "vcpkg not found at $vcpkgRoot; cloning and bootstrapping..."
        if ($PSCmdlet.ShouldProcess($vcpkgRoot, 'Clone and bootstrap vcpkg')) {
            # Full clone (NOT --depth 1): vcpkg.json pins a builtin-baseline, which
            # requires the historical commit to exist locally. A shallow clone cannot
            # check out that baseline and fails with "cloned as a shallow repository".
            git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
            & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
            if ($LASTEXITCODE -ne 0) { Fail-Prerequisite 'vcpkg bootstrap failed.' }
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
        Fail-Prerequisite "vcpkg toolchain missing under $vcpkgRoot; set VCPKG_ROOT to a valid vcpkg checkout."
    }
    $env:VCPKG_ROOT = $vcpkgRoot
    Write-Host "vcpkg: $vcpkgRoot"
    return $vcpkgRoot
}

function Ensure-CurlImpersonate {
    if ((Test-Path -LiteralPath $CurlIncludeDir) -and (Test-Path -LiteralPath $CurlLibrary)) {
        Write-Host "curl-impersonate ready: $CurlRoot"
        return $CurlRoot
    }
    if (-not $PSCmdlet.ShouldProcess($CurlCache, "Download and extract curl-impersonate $CurlImpersonateVersion")) {
        return $null
    }
    New-Item -ItemType Directory -Path $CurlCache -Force | Out-Null
    $tarball = Join-Path $CurlCache $CurlImpersonateArchive
    Invoke-WebRequest -Uri "$CurlImpersonateBaseUrl/$CurlImpersonateArchive" -OutFile $tarball
    if (-not ((Test-Path -LiteralPath $CurlIncludeDir) -and (Test-Path -LiteralPath $CurlLibrary))) {
        if (Test-Path -LiteralPath $CurlRoot) { Remove-Item -LiteralPath $CurlRoot -Recurse -Force }
        New-Item -ItemType Directory -Path $CurlRoot -Force | Out-Null
        tar -xzf $tarball -C $CurlRoot
    }
    if (-not ((Test-Path -LiteralPath $CurlIncludeDir) -and (Test-Path -LiteralPath $CurlLibrary))) {
        Fail-Prerequisite "curl-impersonate archive did not contain include/curl and lib/libcurl-impersonate_imp.lib under $CurlRoot"
    }
    Write-Host "curl-impersonate ready: $CurlRoot"
    return $CurlRoot
}

function Invoke-DeveloperCommand([string]$DevCmd, [string]$Command) {
    $cmdLine = "call `"$DevCmd`" -arch=x64 -host_arch=x64 >nul && cd /d `"$ProjectRoot`" && $Command"
    if ($WhatIfPreference) {
        Write-Host "What if: cmd.exe /d /s /c $cmdLine"
        return
    }
    & cmd.exe /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code ${LASTEXITCODE}: $Command" }
}

# Run a command in a CLEAN environment (no VsDevCmd sourced). vcpkg install
# (triggered by cmake configure) fails with "Unable to find a valid Visual
# Studio instance" when it detects it is running inside a VS developer shell,
# so configure must NOT source VsDevCmd. cmake is on the system PATH here.
function Invoke-CleanCommand([string]$Command) {
    $cmdLine = "cd /d `"$ProjectRoot`" && $Command"
    if ($WhatIfPreference) {
        Write-Host "What if: cmd.exe /d /s /c $cmdLine"
        return
    }
    & cmd.exe /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code ${LASTEXITCODE}: $Command" }
}

function Test-DeveloperPrerequisites([string]$DevCmd) {
    Invoke-DeveloperCommand $DevCmd 'where cl >nul && where rc >nul && where mt >nul && where cmake >nul'
}

function Invoke-Action([string]$DevCmd, [string]$qt6Dir) {
    # Explicitly pass the vcpkg toolchain so CMake never falls back to the
    # (older) vcpkg bundled inside Visual Studio, which cannot handle newer
    # MSVC toolsets. VCPKG_ROOT is set by Ensure-Vcpkg above.
    $vcpkgToolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    $configure = "cmake --preset $Preset -DQt6_DIR=`"$qt6Dir`" -DCURL_INCLUDE_DIR=`"$CurlIncludeDir`" -DCURL_LIBRARY=`"$CurlLibrary`" -DCMAKE_TOOLCHAIN_FILE=`"$vcpkgToolchain`""
    $build = "cmake --build --preset $Preset --config $Configuration --parallel"
    $test = "ctest --preset $Preset -C $Configuration --output-on-failure"

    # Qt bin directory (full DLL set) for runtime copying.
    $kitRoot = (Resolve-Path -LiteralPath (Join-Path $qt6Dir '..\..\..')).Path
    $qtBinDir = Join-Path $kitRoot 'bin'

    switch ($Action) {
        'configure' { Invoke-CleanCommand $configure }
        'build' { Invoke-DeveloperCommand $DevCmd $build }
        'test' { Invoke-DeveloperCommand $DevCmd $test }
        'deploy' {
            Invoke-CleanCommand $configure
            Invoke-DeveloperCommand $DevCmd $build
            Invoke-DeveloperCommand $DevCmd "cmake --build --preset $Preset --config $Configuration --target acheron_deploy"
            Copy-RuntimeToConfigDir
        }
        'all' {
            if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
                if ($PSCmdlet.ShouldProcess($BuildDir, 'Remove build directory')) {
                    Remove-Item -LiteralPath $BuildDir -Recurse -Force
                    Write-Host "Removed $BuildDir"
                }
            }
            Invoke-CleanCommand $configure
            Invoke-DeveloperCommand $DevCmd $build
            # Deploy + copy the Qt runtime into build\<Config> BEFORE running the
            # tests: the unit-test executables there need the Qt DLLs to start.
            Invoke-DeveloperCommand $DevCmd "cmake --build --preset $Preset --config $Configuration --target acheron_deploy"
            Copy-RuntimeToConfigDir $qtBinDir
            Invoke-DeveloperCommand $DevCmd $test
        }
    }
}

# Copy the deploy folder (exe + Qt DLLs + plugins + assets) into the plain
# build\RelWithDebInfo directory so the linked acheron.exe there is directly
# runnable without a separate deploy step.
function Copy-RuntimeToConfigDir {
    param([string]$QtBinDir)
    $deployDir = Join-Path $BuildDir "deploy\$Configuration"
    if (-not (Test-Path -LiteralPath $deployDir)) {
        Write-Warning "Deploy folder not found: $deployDir (nothing to copy)"
        return
    }

    # windeployqt does not discover libcurl-impersonate.dll (not a Qt dep).
    # Copy it into the deploy folder so it reaches the config dir too.
    $curlDll = Join-Path $CurlRoot 'lib\libcurl-impersonate.dll'
    if (Test-Path -LiteralPath $curlDll) {
        Copy-Item -LiteralPath $curlDll -Destination $deployDir -Force
    } else {
        Write-Warning "curl-impersonate DLL not found: $curlDll"
    }

    $target = Join-Path $BuildDir $Configuration
    Write-Host "Copying Qt runtime into $target ..."
    robocopy $deployDir $target /E /NFL /NDL /NJH | Out-Null
    if ($LASTEXITCODE -gt 7) {
        throw "Failed to copy Qt runtime into $target (robocopy exit $LASTEXITCODE)."
    }

    # windeployqt only deploys what acheron.exe links. Copy the full Qt bin
    # directory so the unit-test executables (which link Qt6Test etc.) also
    # find every Qt DLL, and so the folder has all Qt DLLs.
    if ($QtBinDir -and (Test-Path -LiteralPath $QtBinDir)) {
        Write-Host "Copying all Qt DLLs from $QtBinDir ..."
        Copy-Item -Path (Join-Path $QtBinDir '*.dll') -Destination $target -Force -ErrorAction SilentlyContinue
    } else {
        Write-Warning "Qt bin directory not found: $QtBinDir"
    }

    Write-Host "Runtime copied. Runnable app: $target\acheron.exe"
}

Push-Location $ProjectRoot
try {
    if ($Action -eq 'clean') {
        if (Test-Path -LiteralPath $BuildDir) {
            Remove-Item -LiteralPath $BuildDir -Recurse -Force
            Write-Host "Removed $BuildDir"
        }
        return
    }

    $devCmd = Find-VisualStudioDevCmd
    Test-DeveloperPrerequisites $devCmd
    $qt6Dir = Ensure-Qt6Config
    Ensure-Vcpkg | Out-Null
    $curlRoot = Ensure-CurlImpersonate
    if (-not $curlRoot) { return }

    Write-Host "Visual Studio developer environment: $devCmd"
    Write-Host "Qt 6 CMake package: $qt6Dir"
    Write-Host "Build type: $Configuration"

    Invoke-Action $devCmd $qt6Dir

    if ($Action -in 'all', 'deploy') {
        Write-Host ""
        Write-Host "Deployable application: $(Join-Path $BuildDir "deploy\$Configuration\acheron.exe")"
    }
}
finally {
    Pop-Location
}
