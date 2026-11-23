#requires -Version 5.0
<#
.SYNOPSIS
    Incrementally builds Clippi and runs the resulting executable.

.DESCRIPTION
    Delegates the incremental CMake build to tools\build.ps1. Only the
    `clippi` target is built, so the test harness is not compiled.

    The executable runs in your current working directory with stdin/stdout
    left attached, so the full-screen TUI gets a real console.

.PARAMETER Config
    Build configuration: Release (default), Debug, RelWithDebInfo, or MinSizeRel.

.PARAMETER BuildDir
    Build directory (default: build), relative to the repo root unless absolute.

.PARAMETER NoBuild
    Skip the build and run the executable already in the build directory.

.PARAMETER ClippiArgs
    Arguments forwarded verbatim to clippi.exe.

    PowerShell binds a leading-dash token to this script's own parameters when
    it is an unambiguous prefix, so `-c` would be read as `-Config`. Put `--`
    before Clippi's own short options to pass them through untouched.

.EXAMPLE
    .\tools\run.ps1
    Build and launch the interactive TUI.

.EXAMPLE
    .\tools\run.ps1 -- -c
    Continue the most recent session for this project.

.EXAMPLE
    .\tools\run.ps1 -Config Debug -- -p "summarize this repo"

.EXAMPLE
    .\tools\run.ps1 -NoBuild --help
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',
    [string]$BuildDir = 'build',
    [switch]$NoBuild,
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$ClippiArgs
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $buildPath 'clippi.exe'

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -BuildDir $BuildDir -Target clippi
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path $exe)) {
    $hint = if ($NoBuild) { "Drop -NoBuild to build it" } else { "Check -BuildDir and -Config" }
    throw "clippi.exe not found at '$exe'. $hint."
}

Write-Host "Running $exe" -ForegroundColor Cyan
if ($ClippiArgs) { & $exe @ClippiArgs } else { & $exe }
exit $LASTEXITCODE
