# Dot-sourced by build.ps1; relies on its selected toolchain.
function Write-Changed([string]$Path, [string]$Text) {
    if (-not (Test-Path -LiteralPath $Path) -or [IO.File]::ReadAllText($Path) -cne $Text) {
        [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
    }
}
function Hash-File([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}
function Install-Runtime([string]$Prefix) {
    Invoke-Checked $cmake @('--install', $build, '--config', $Config, '--prefix', $Prefix, '--component', 'runtime')
    if ($Config -ne 'Debug') { Audit-Runtime (Join-Path $Prefix 'bin') }
}

function Audit-Runtime([string]$Bin) {
    $dumpbin = Join-Path $toolBin 'dumpbin.exe'
    $clippi = Join-Path $Bin 'clippi.exe'
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($clippi)
    $seen = @{}
    while ($queue.Count) {
        $binary = $queue.Dequeue()
        if ($seen.ContainsKey($binary)) { continue }
        $seen[$binary] = $true
        $headers = & $dumpbin /nologo /headers $binary
        if ($LASTEXITCODE -ne 0 -or -not ($headers -match '^\s*8664\s')) {
            throw "Package requires an x86_64 PE binary: $binary"
        }
        $imports = & $dumpbin /nologo /dependents $binary
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect dependencies of $binary" }
        foreach ($line in $imports) {
            if ($line -notmatch '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') { continue }
            $name = $Matches[1]
            if ($binary -eq $clippi -and
                $name -match '^(msvcp|vcruntime|concrt|vcomp|msvcr|ucrtbase|api-ms-win-crt-)') {
                throw "Clippi must use the static CRT (/MT): unexpected dependency $name"
            }
            $local = Join-Path $Bin $name
            if (Test-Path -LiteralPath $local) { $queue.Enqueue($local); continue }
            if ($name -match '^(api-ms-|ext-ms-)') { continue }
            if (-not (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$name") -PathType Leaf)) {
                throw "A non-system dependency would escape the archive: $name (from $binary)"
            }
        }
    }
}

function Package-Clippi {
    $version = [IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()
    $name = "clippi-$version-Windows-x86_64"
    $packages = Join-Path $build 'packages'
    New-Item -ItemType Directory -Force $packages | Out-Null
    $stage = Join-Path $packages ("stage-" + [Guid]::NewGuid().ToString('N'))
    $prefix = Join-Path $stage $name
    $archive = Join-Path $packages "$name.zip"
    $temporary = "$archive.$PID.tmp.zip"
    New-Item -ItemType Directory -Force $prefix | Out-Null
    try {
        Install-Runtime $prefix
        Invoke-Checked 'tar.exe' @('-a', '-cf', $temporary, '-C', $stage, $name)
        Move-Item -LiteralPath $temporary -Destination $archive -Force
        Write-Changed "$archive.sha256" ((Hash-File $archive) + "  $name.zip`n")
        Write-Host "Archive: $archive"
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
