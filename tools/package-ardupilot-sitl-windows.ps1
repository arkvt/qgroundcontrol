#requires -Version 5.1
<#
.SYNOPSIS
    Builds ArduPlane SITL in Cygwin and creates a source-free Windows runtime package.

.DESCRIPTION
    This is a developer/CI script. Customer computers only receive the generated
    simulator directory and do not need Cygwin, WSL, Python, Waf, or ArduPilot source.

.EXAMPLE
    .\tools\package-ardupilot-sitl-windows.ps1 -Action all `
        -ArduPilotSource D:\src\ardupilot -CygwinRoot C:\cygwin64

.EXAMPLE
    .\tools\package-ardupilot-sitl-windows.ps1 -Action package `
        -ArduPilotSource D:\src\ardupilot `
        -PlaneExecutable D:\src\ardupilot\build\sitl\bin\arduplane.exe
#>

[CmdletBinding()]
param(
    [ValidateSet('check', 'build', 'package', 'all', 'smoke-test')]
    [string]$Action = 'check',

    [string]$ArduPilotSource = $env:ARDUPILOT_SOURCE,

    [string]$CygwinRoot = $env:CYGWIN_ROOT,

    [string]$PlaneExecutable,

    [string]$QuadplaneParamFile,

    [string]$OutputDir,

    [switch]$KeepExistingOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $RepoRoot 'artifacts\sitl-windows'
}

function Resolve-FullPath {
    param([Parameter(Mandatory=$true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Resolve-CygwinInstallation {
    $candidates = @($CygwinRoot, 'C:\cygwin64', 'C:\cygwin') |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique
    foreach ($candidate in $candidates) {
        $root = Resolve-FullPath $candidate
        $bash = Join-Path $root 'bin\bash.exe'
        $cygpath = Join-Path $root 'bin\cygpath.exe'
        if ((Test-Path -LiteralPath $bash -PathType Leaf) -and
            (Test-Path -LiteralPath $cygpath -PathType Leaf)) {
            return [pscustomobject]@{ Root = $root; Bash = $bash; Cygpath = $cygpath }
        }
    }
    throw 'A complete Cygwin installation was not found. Pass -CygwinRoot on the developer machine.'
}

function Assert-ArduPilotSource {
    if ([string]::IsNullOrWhiteSpace($ArduPilotSource)) {
        throw 'Pass -ArduPilotSource. Source is needed only on the developer/build machine.'
    }
    $script:SourceRoot = Resolve-FullPath $ArduPilotSource
    if (-not (Test-Path -LiteralPath (Join-Path $script:SourceRoot 'waf') -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $script:SourceRoot 'ArduPlane') -PathType Container)) {
        throw "Not an ArduPilot source tree: $script:SourceRoot"
    }
}

function Resolve-AsciiPathForCygwin {
    param([Parameter(Mandatory=$true)][string]$Path)

    $resolved = Resolve-FullPath $Path
    if ($resolved -notmatch '[^\x00-\x7F]') {
        return $resolved
    }

    # Passing a Unicode Windows path as a native Bash argument can be decoded
    # with the wrong code page by Cygwin. Reuse one fixed drive so repeated
    # packaging runs do not leave a collection of subst drives behind.
    $mappingTarget = Split-Path -Parent $resolved
    $existingMappings = (& subst.exe) -join "`n"
    $drive = 'U:'
    $mappedPath = Join-Path $drive (Split-Path -Leaf $resolved)
    if ($existingMappings -match '(?im)^U:\\') {
        if ((Test-Path -LiteralPath (Join-Path $mappedPath 'waf') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $mappedPath 'ArduPlane') -PathType Container)) {
            Write-Host "Reusing Cygwin ASCII source mapping: $drive"
            return $mappedPath
        }
        throw "U: is already mapped to another location. Remove it with 'subst U: /d' and retry."
    }

    & subst.exe $drive $mappingTarget
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to map $mappingTarget to U: for Cygwin path compatibility."
    }

    Write-Host "Cygwin ASCII source mapping: $drive => $mappingTarget"
    return $mappedPath
}

function Convert-ToCygwinPath {
    param([Parameter(Mandatory=$true)][string]$Path)
    $cygwin = Resolve-CygwinInstallation
    $result = & $cygwin.Cygpath -u (Resolve-FullPath $Path)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($result)) {
        throw "cygpath failed for: $Path"
    }
    return $result.Trim()
}

function Invoke-SitlBuild {
    Assert-ArduPilotSource
    $cygwin = Resolve-CygwinInstallation
    $asciiSource = Resolve-AsciiPathForCygwin $script:SourceRoot
    $source = Convert-ToCygwinPath $asciiSource
    $command = 'set -e; cd -- "$1"; ./waf configure --board sitl; ./waf build --target bin/arduplane'
    Write-Host "Building ArduPlane SITL in $($cygwin.Root)"
    & $cygwin.Bash --login -lc $command 'aerofollow-build' $source
    if ($LASTEXITCODE -ne 0) {
        throw "ArduPlane SITL build failed with exit code $LASTEXITCODE."
    }
}

function Resolve-PlaneBinary {
    if (-not [string]::IsNullOrWhiteSpace($PlaneExecutable)) {
        $candidate = Resolve-FullPath $PlaneExecutable
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
        throw "ArduPlane SITL executable was not found: $candidate"
    }
    Assert-ArduPilotSource
    foreach ($name in @('arduplane.exe', 'arduplane')) {
        $candidate = Join-Path $script:SourceRoot "build\sitl\bin\$name"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw 'ArduPlane SITL executable was not found. Run -Action build first.'
}

function Resolve-Objdump {
    $cygwin = Resolve-CygwinInstallation
    foreach ($name in @('objdump.exe', 'x86_64-pc-cygwin-objdump.exe')) {
        $candidate = Join-Path $cygwin.Root "bin\$name"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw 'Cygwin objdump.exe was not found. Install the binutils package on the developer machine.'
}

function Get-PeDependencies {
    param([Parameter(Mandatory=$true)][string]$File)
    $objdump = Resolve-Objdump
    $output = & $objdump -p $File 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $File"
    }
    return @($output | ForEach-Object {
        if ($_ -match '^\s*DLL Name:\s*(\S+)\s*$') { $Matches[1] }
    } | Where-Object { $_ } | Select-Object -Unique)
}

function Get-RuntimeDependencyClosure {
    param([Parameter(Mandatory=$true)][string]$Executable)
    $cygwin = Resolve-CygwinInstallation
    $searchDirectories = @((Split-Path -Parent $Executable), (Join-Path $cygwin.Root 'bin'))
    $systemNames = @(
        'ADVAPI32.dll', 'COMDLG32.dll', 'GDI32.dll', 'KERNEL32.dll', 'NETAPI32.dll',
        'OLE32.dll', 'OLEAUT32.dll', 'PSAPI.DLL', 'SHELL32.dll', 'USER32.dll',
        'USERENV.dll', 'VERSION.dll', 'WINMM.dll', 'WS2_32.dll', 'ntdll.dll'
    )
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $visited = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $runtimeFiles = [System.Collections.Generic.List[string]]::new()
    $queue.Enqueue($Executable)

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if (-not $visited.Add($current)) { continue }
        foreach ($dependency in Get-PeDependencies $current) {
            if ($dependency -like 'api-ms-win-*' -or $dependency -like 'ext-ms-win-*' -or
                $systemNames -icontains $dependency) {
                continue
            }
            if (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$dependency") -PathType Leaf) {
                continue
            }
            $resolved = $null
            foreach ($directory in $searchDirectories) {
                $candidate = Join-Path $directory $dependency
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $resolved = Resolve-FullPath $candidate
                    break
                }
            }
            if (-not $resolved) {
                throw "Unresolved non-system DLL '$dependency' required by '$current'."
            }
            if (-not ($runtimeFiles -icontains $resolved)) {
                $runtimeFiles.Add($resolved)
                $queue.Enqueue($resolved)
            }
        }
    }
    return $runtimeFiles
}

function Initialize-OutputDirectory {
    $target = Resolve-FullPath $OutputDir
    if (Test-Path -LiteralPath $target) {
        if (-not $KeepExistingOutput) {
            $manifestPath = Join-Path $target 'manifest.json'
            if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
                throw "Refusing to replace an unrecognized directory: $target. Use an empty -OutputDir."
            }
            $oldManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            if ($oldManifest.packageType -ne 'AeroFollow ArduPlane SITL Windows Runtime') {
                throw "Refusing to replace an unrecognized package directory: $target"
            }
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target 'bin') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $target 'params') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $target 'licenses') | Out-Null
    return $target
}

function Copy-FirstExistingFile {
    param([string[]]$Candidates, [string]$Destination)
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            Copy-Item -LiteralPath $candidate -Destination $Destination -Force
            return $true
        }
    }
    return $false
}

function New-RuntimePackage {
    Assert-ArduPilotSource
    $cygwin = Resolve-CygwinInstallation
    $plane = Resolve-PlaneBinary
    $dependencies = @(Get-RuntimeDependencyClosure $plane)
    if (-not ($dependencies | Where-Object { (Split-Path -Leaf $_) -ieq 'cygwin1.dll' })) {
        throw 'Dependency scan did not find cygwin1.dll; the executable is not the expected Cygwin build.'
    }
    $target = Initialize-OutputDirectory
    Copy-Item -LiteralPath $plane -Destination (Join-Path $target 'bin\arduplane.exe') -Force
    foreach ($dependency in $dependencies) {
        Copy-Item -LiteralPath $dependency -Destination (Join-Path $target 'bin') -Force
    }

    $quadplane = if ($QuadplaneParamFile) {
        Resolve-FullPath $QuadplaneParamFile
    } else {
        Join-Path $RepoRoot 'tools\simulator-params\x7_quadplane_rtk_follow_simonhw.param'
    }
    if (-not (Test-Path -LiteralPath $quadplane -PathType Leaf)) {
        throw "QuadPlane defaults file was not found: $quadplane"
    }
    Copy-Item -LiteralPath $quadplane -Destination (Join-Path $target 'params\quadplane.parm') -Force
    [System.IO.File]::WriteAllLines(
        (Join-Path $target 'params\aerofollow.parm'),
        @('FOLL_ENABLE 1', 'FT_SYSID 255'),
        [System.Text.Encoding]::ASCII)

    $ardupilotLicenseCopied = Copy-FirstExistingFile @(
        (Join-Path $script:SourceRoot 'COPYING.txt'),
        (Join-Path $script:SourceRoot 'COPYING')
    ) (Join-Path $target 'licenses\ArduPilot-GPLv3.txt')
    if (-not $ardupilotLicenseCopied) {
        throw 'ArduPilot GPL license file was not found in the source tree.'
    }
    $cygwinLicenseCopied = Copy-FirstExistingFile @(
        (Join-Path $cygwin.Root 'usr\share\doc\cygwin\COPYING'),
        (Join-Path $cygwin.Root 'usr\share\doc\Cygwin\cygwin-doc\COPYING'),
        (Join-Path $cygwin.Root 'usr\share\licenses\cygwin\COPYING'),
        (Join-Path $cygwin.Root 'usr\share\doc\common-licenses\GPL-3'),
        (Join-Path $cygwin.Root 'usr\share\common-licenses\GPL-3')
    ) (Join-Path $target 'licenses\Cygwin-GPLv3.txt')
    if (-not $cygwinLicenseCopied) {
        Write-Warning 'Cygwin license file was not found automatically. Add it before customer delivery.'
    }

    $revision = 'unknown'
    try {
        $revisionOutput = & git -C $script:SourceRoot rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0) { $revision = $revisionOutput.Trim() }
    } catch { }
    $readme = @'
AeroFollow ArduPlane SITL Windows Runtime

This directory is launched directly by AeroFollow/QGroundControl.
The customer computer does not need WSL, Cygwin, Python, Waf, a compiler,
or an ArduPilot source checkout. Do not move individual DLL files out of bin.

ArduPilot is GPLv3 software. The distributor must provide the matching source
code or a valid written offer as required by GPLv3. See licenses and the
delivery documentation shipped with the product.
'@
    [System.IO.File]::WriteAllText((Join-Path $target 'README.txt'), $readme, [System.Text.Encoding]::UTF8)

    $fileEntries = @(Get-ChildItem -LiteralPath $target -File -Recurse | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($target.Length + 1).Replace('\', '/')
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    $manifest = [ordered]@{
        packageType = 'AeroFollow ArduPlane SITL Windows Runtime'
        schemaVersion = 1
        createdUtc = [DateTime]::UtcNow.ToString('o')
        ardupilotRevision = $revision
        executable = 'bin/arduplane.exe'
        files = $fileEntries
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $target 'manifest.json') -Encoding UTF8

    $size = (Get-ChildItem -LiteralPath $target -File -Recurse | Measure-Object Length -Sum).Sum
    Write-Host "Runtime package: $target"
    Write-Host ("Files: {0}; size: {1:N1} MiB" -f $fileEntries.Count, ($size / 1MB))
}

function Test-RuntimePackage {
    $target = Resolve-FullPath $OutputDir
    $exe = Join-Path $target 'bin\arduplane.exe'
    foreach ($required in @($exe, (Join-Path $target 'bin\cygwin1.dll'),
                             (Join-Path $target 'params\quadplane.parm'),
                             (Join-Path $target 'params\aerofollow.parm'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Runtime package is incomplete: $required"
        }
    }
    $testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("aerofollow-sitl-test-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $testDirectory | Out-Null
    $stdout = Join-Path $testDirectory 'stdout.log'
    $stderr = Join-Path $testDirectory 'stderr.log'
    $process = $null
    $runtimeProcesses = @()
    $existingProcessIds = @(Get-Process -Name 'arduplane' -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Id)
    try {
        $defaultsArgument = '"' + (Join-Path $target 'params\quadplane.parm') + ',' +
                            (Join-Path $target 'params\aerofollow.parm') + '"'
        $arguments = @(
            '-S', '-w', '--model', 'quadplane', '--speedup', '1', '--sysid', '1', '--slave', '0',
            '--defaults', $defaultsArgument,
            '--sim-address', '127.0.0.1', '-I9', '--home', '31.8511168,117.2292701,50,0',
            '--serial0', 'udpclient:127.0.0.1:14550'
        )
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $testDirectory `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        Start-Sleep -Seconds 5
        $runtimeProcesses = @(Get-Process -Name 'arduplane' -ErrorAction SilentlyContinue | Where-Object {
            ($existingProcessIds -notcontains $_.Id) -and
            ($_.Path -eq $exe)
        })
        if ($process.HasExited -and $runtimeProcesses.Count -eq 0) {
            $output = ((Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue) +
                       (Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue)).Trim()
            throw "SITL exited during smoke test (exit code $($process.ExitCode)): $output"
        }
        $output = (Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue)
        if ($output -notmatch "Starting sketch 'ArduPlane'") {
            throw "SITL started but expected ArduPlane output was not observed: $output"
        }
        Write-Host 'SITL smoke test passed.'
    } finally {
        foreach ($runtimeProcess in $runtimeProcesses) {
            Stop-Process -Id $runtimeProcess.Id -Force -ErrorAction SilentlyContinue
        }
        if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $testDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Show-Check {
    Write-Host "Output: $((Resolve-FullPath $OutputDir))"
    try {
        $cygwin = Resolve-CygwinInstallation
        Write-Host "Cygwin: $($cygwin.Root)"
        Write-Host "objdump: $(Resolve-Objdump)"
    } catch {
        Write-Warning $_.Exception.Message
    }
    if (-not [string]::IsNullOrWhiteSpace($ArduPilotSource)) {
        try { Assert-ArduPilotSource; Write-Host "ArduPilot source: $script:SourceRoot" }
        catch { Write-Warning $_.Exception.Message }
    } else {
        Write-Host 'ArduPilot source: not configured (required only for build/package)'
    }
}

switch ($Action) {
    'check'      { Show-Check }
    'build'      { Invoke-SitlBuild }
    'package'    { New-RuntimePackage }
    'all'        { Invoke-SitlBuild; New-RuntimePackage; Test-RuntimePackage }
    'smoke-test' { Test-RuntimePackage }
}
