<#
.SYNOPSIS
    Configure, build, deploy, run, or clean QGroundControl on Windows.

.EXAMPLE
    .\tools\build-qgc-windows.ps1

.EXAMPLE
    .\tools\build-qgc-windows.ps1 -Action all

.EXAMPLE
    .\tools\build-qgc-windows.ps1 -Action run
#>

[CmdletBinding()]
param(
    [ValidateSet('configure', 'build', 'deploy', 'run', 'clean', 'rebuild', 'all')]
    [string]$Action = 'build',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Debug',

    [string]$AppName = 'AeroFollow',

    [string]$QtRoot = $env:QT_ROOT_DIR,

    [string]$QtVersion = $(if ($env:QT_VERSION) { $env:QT_VERSION } else { '6.8.3' }),

    [string[]]$QtInstallRoot = @(
        $(if ($env:QT_PATH) { $env:QT_PATH } else { $null }),
        $(if ($env:QTDIR) { $env:QTDIR } else { $null }),
        $(Join-Path $env:SystemDrive 'Qt'),
        'D:\QtWin',
        $(Join-Path $env:USERPROFILE 'Qt')
    ),

    [string]$BuildDir,

    [string]$SitlPackage = $env:AEROFOLLOW_SITL_PACKAGE,

    [switch]$EnableGStreamer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot 'build\qgc-v5.0.8-ui'
}

function Resolve-FullPath {
    param([Parameter(Mandatory=$true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Resolve-Tool {
    param([Parameter(Mandatory=$true)][string]$Name, [string[]]$Candidates = @())
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($candidate in $Candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-FullPath $candidate)
        }
    }
    throw "$Name was not found."
}

function Resolve-AsciiSourceRoot {
    if ($RepoRoot -notmatch '[^\x00-\x7F]') {
        return $RepoRoot
    }
    $mappingTarget = Split-Path -Parent $RepoRoot
    $repoName = Split-Path -Leaf $RepoRoot
    $existingMappings = (& subst.exe) -join "`n"
    foreach ($letter in @('Q', 'R', 'S', 'T')) {
        $drive = "${letter}:"
        $mappedRepo = "$drive\$repoName"
        $sourceMarker = Join-Path $RepoRoot 'CMakeLists.txt'
        $mappedMarker = if (Test-Path -LiteralPath "$drive\" -PathType Container) {
            Join-Path $mappedRepo 'CMakeLists.txt'
        }
        if ($mappedMarker -and (Test-Path -LiteralPath $mappedMarker -PathType Leaf) -and
            (Get-FileHash -LiteralPath $mappedMarker -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $sourceMarker -Algorithm SHA256).Hash) {
            return $mappedRepo
        }
        if ($existingMappings -notmatch "(?im)^$([regex]::Escape($drive))\\:") {
            & subst.exe $drive $mappingTarget
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to map $mappingTarget to $drive for Qt path compatibility."
            }
            Write-Host "ASCII source mapping: $drive => $mappingTarget"
            return "$drive\$repoName"
        }
    }
    throw 'Qt tools require an ASCII source path, but Q:, R:, S:, and T: are already in use.'
}

function Resolve-SitlPackage {
    if ([string]::IsNullOrWhiteSpace($SitlPackage)) {
        return $null
    }
    $resolved = Resolve-FullPath $SitlPackage
    foreach ($relativePath in @('manifest.json', 'bin\arduplane.exe', 'bin\cygwin1.dll',
                                 'params\quadplane.parm', 'params\aerofollow.parm')) {
        if (-not (Test-Path -LiteralPath (Join-Path $resolved $relativePath) -PathType Leaf)) {
            throw "Invalid SITL runtime package; missing $relativePath in $resolved"
        }
    }
    return $resolved
}

function Copy-SitlPackageToBuild {
    $source = Resolve-SitlPackage
    if (-not $source) {
        Write-Warning 'No -SitlPackage was supplied. The one-click simulator will report a missing runtime package.'
        return
    }
    $destination = Join-Path $BuildDir "$Config\simulator"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
    Write-Host "SITL runtime staged: $destination"
}

function Resolve-QtRoot {
    param([string]$Candidate)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
        $candidates += $Candidate
    }

    foreach ($root in $QtInstallRoot | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }

        $rootToolchain = Join-Path $root 'lib\cmake\Qt6\qt.toolchain.cmake'
        if (Test-Path -LiteralPath $rootToolchain) {
            $candidates += $root
            continue
        }

        $versionDir = Join-Path $root $QtVersion
        if (Test-Path -LiteralPath $versionDir) {
            $candidates += Get-ChildItem -LiteralPath $versionDir -Directory |
                Where-Object { $_.Name -match 'msvc|mingw' } |
                ForEach-Object { $_.FullName }
        }
    }

    foreach ($path in $candidates | Select-Object -Unique) {
        $toolchain = Join-Path $path 'lib\cmake\Qt6\qt.toolchain.cmake'
        if (Test-Path -LiteralPath $toolchain) {
            return (Resolve-FullPath $path)
        }
    }

    throw "Qt $QtVersion toolchain was not found. Set QT_ROOT_DIR or pass -QtRoot."
}

function Resolve-VsDevShell {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installPath)) {
            $candidate = Join-Path $installPath 'Common7\Tools\Launch-VsDevShell.ps1'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    )

    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }

        $candidate = Get-ChildItem -LiteralPath $root -Recurse -Filter Launch-VsDevShell.ps1 -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($candidate) {
            return $candidate
        }
    }

    throw 'Visual Studio Developer PowerShell was not found. Install MSVC C++ tools.'
}

function Initialize-BuildEnvironment {
    $script:QtRootResolved = Resolve-QtRoot $QtRoot
    $script:SourceRootResolved = Resolve-AsciiSourceRoot
    if ($BuildDir -match '[^\x00-\x7F]') {
        $mappingTarget = Split-Path -Parent $RepoRoot
        $fullBuildDir = Resolve-FullPath $BuildDir
        if (-not $fullBuildDir.StartsWith($mappingTarget, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'The build directory contains non-ASCII characters and is outside the mapped source tree. Pass an ASCII -BuildDir.'
        }
        $mappedDrive = Split-Path -Qualifier $script:SourceRootResolved
        $relativeBuildDir = $fullBuildDir.Substring($mappingTarget.Length).TrimStart('\')
        $script:BuildDir = Join-Path $mappedDrive $relativeBuildDir
        Write-Host "ASCII build mapping: $fullBuildDir => $script:BuildDir"
    }

    $env:QT_ROOT_DIR = $script:QtRootResolved
    $qtBin = Join-Path $script:QtRootResolved 'bin'
    if (-not ($env:Path -split ';' | Where-Object { $_ -ieq $qtBin })) {
        $env:Path = "$qtBin;$env:Path"
    }

    if ((Split-Path -Leaf $script:QtRootResolved) -match 'mingw') {
        $qtInstallRoot = Split-Path -Parent (Split-Path -Parent $script:QtRootResolved)
        $mingwBin = Get-ChildItem -LiteralPath (Join-Path $qtInstallRoot 'Tools') -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'bin' } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'g++.exe') } |
            Select-Object -First 1
        if (-not $mingwBin) {
            throw "Qt MinGW compiler was not found under $qtInstallRoot\Tools."
        }
        if (-not ($env:Path -split ';' | Where-Object { $_ -ieq $mingwBin })) {
            $env:Path = "$mingwBin;$env:Path"
        }
    } else {
        $script:VsDevShell = Resolve-VsDevShell
        & $script:VsDevShell -Arch amd64 -HostArch amd64
    }

    $pythonScripts = Join-Path $env:APPDATA 'Python'
    $cmakeCandidates = @()
    $ninjaCandidates = @()
    if (Test-Path -LiteralPath $pythonScripts) {
        $cmakeCandidates += Get-ChildItem -LiteralPath $pythonScripts -Filter cmake.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
        $ninjaCandidates += Get-ChildItem -LiteralPath $pythonScripts -Filter ninja.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
    }
    $script:Cmake = Resolve-Tool 'cmake.exe' $cmakeCandidates
    $script:Ninja = Resolve-Tool 'ninja.exe' $ninjaCandidates
}

function Invoke-Configure {
    Initialize-BuildEnvironment

    $toolchain = Join-Path $script:QtRootResolved 'lib\cmake\Qt6\qt.toolchain.cmake'
    $gstValue = if ($EnableGStreamer) { 'ON' } else { 'OFF' }

    Write-Host "Source:    $script:SourceRootResolved"
    Write-Host "Build:     $BuildDir"
    Write-Host "Qt:        $script:QtRootResolved"
    Write-Host "Config:    $Config"
    Write-Host "GStreamer: $gstValue"

    $sitlPackageResolved = Resolve-SitlPackage
    $sitlCmakeValue = if ($sitlPackageResolved) { $sitlPackageResolved.Replace('\', '/') } else { '' }

    & $script:Cmake -S $script:SourceRootResolved -B $BuildDir -G 'Ninja Multi-Config' `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DCMAKE_MAKE_PROGRAM=$script:Ninja" `
        "-DCMAKE_CONFIGURATION_TYPES=Debug;Release" `
        "-DQGC_APP_NAME=$AppName" `
        "-DQGC_ENABLE_GST_VIDEOSTREAMING=$gstValue" `
        "-DQGC_WINDOWS_SITL_PACKAGE=$sitlCmakeValue"
}

function Invoke-Build {
    Invoke-Configure
    & $script:Cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "QGC build failed with exit code $LASTEXITCODE."
    }
    Copy-SitlPackageToBuild
}

function Invoke-Deploy {
    Initialize-BuildEnvironment

    $exe = Join-Path $BuildDir "$Config\$AppName.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "$AppName.exe was not found at $exe. Build first."
    }

    $deployMode = if ($Config -eq 'Debug') { '--debug' } else { '--release' }
    & (Join-Path $script:QtRootResolved 'bin\windeployqt.exe') `
        $deployMode `
        --compiler-runtime `
        --qmldir $RepoRoot `
        $exe
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE."
    }
    Copy-SitlPackageToBuild
}

function Invoke-Run {
    Initialize-BuildEnvironment

    $exe = Join-Path $BuildDir "$Config\$AppName.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "$AppName.exe was not found at $exe. Build first."
    }

    Push-Location (Split-Path -Parent $exe)
    try {
        & $exe
    } finally {
        Pop-Location
    }
}

function Invoke-Clean {
    $buildRoot = Resolve-FullPath (Join-Path $RepoRoot 'build')
    $target = Resolve-FullPath $BuildDir

    if (-not $target.StartsWith($buildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove build directory outside repository build root: $target"
    }

    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
        Write-Host "Removed $target"
    } else {
        Write-Host "Build directory does not exist: $target"
    }
}

switch ($Action) {
    'configure' { Invoke-Configure }
    'build'     { Invoke-Build }
    'deploy'    { Invoke-Deploy }
    'run'       { Invoke-Run }
    'clean'     { Invoke-Clean }
    'rebuild'   { Invoke-Clean; Invoke-Build; Invoke-Deploy }
    'all'       { Invoke-Build; Invoke-Deploy }
}
