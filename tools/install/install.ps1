# Installs or upgrades Clippi on Windows from GitHub Releases:
#
#   irm https://raw.githubusercontent.com/rodrigonso/Clippi/main/tools/install/install.ps1 | iex
#
# Settings come from the environment so the piped form can use them too:
#   CLIPPI_INSTALL_VERSION  X.Y.Z to install (default: newest release)
#   CLIPPI_INSTALL_DIR      install prefix (default: %LOCALAPPDATA%\Programs\clippi)
#   CLIPPI_INSTALL_ARCHIVE  local clippi-X.Y.Z-Windows-x86_64.zip with its .sha256
#                            alongside, instead of downloading
#   CLIPPI_NO_MODIFY_PATH   1 to leave the user PATH alone
#
# Everything runs inside one script block so `iex` leaves no variables or
# preference changes behind in the caller's session.
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repo = 'rodrigonso/Clippi'
    $platform = 'Windows-x86_64'
    $installDir = $env:CLIPPI_INSTALL_DIR
    if (-not $installDir) { $installDir = Join-Path $env:LOCALAPPDATA 'Programs\clippi' }
    $installDir = [IO.Path]::GetFullPath($installDir)
    $bin = Join-Path $installDir 'bin'
    $exe = Join-Path $bin 'clippi.exe'
    $version = "$env:CLIPPI_INSTALL_VERSION" -replace '^v', ''
    $archive = $env:CLIPPI_INSTALL_ARCHIVE

    if ($archive) {
        $archive = (Resolve-Path -LiteralPath $archive).Path
        $name = Split-Path -Leaf $archive
        if ($name -notmatch "^clippi-(\d+\.\d+\.\d+)-$platform\.zip$") {
            throw "Unexpected archive name: $name"
        }
        $version = $Matches[1]
    } elseif (-not $version) {
        # Piping enumerates the JSON array; Windows PowerShell returns it as one object.
        $latest = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases?per_page=1" `
            -Headers @{ 'User-Agent' = 'clippi-installer'; Accept = 'application/vnd.github+json' } |
            Select-Object -First 1
        if (-not $latest) { throw 'No Clippi release has been published yet.' }
        $version = $latest.tag_name -replace '^v', ''
    }
    if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid version: $version" }

    if (Test-Path -LiteralPath $exe) {
        $installed = try { (& $exe --version 2>$null) -join '' } catch { '' }
        if ($installed -eq "clippi $version") {
            Write-Host "Clippi $version is already installed in $installDir."
            return
        }
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ('clippi-install-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $name = "clippi-$version-$platform.zip"
        if (-not $archive) {
            $base = "https://github.com/$repo/releases/download/v$version"
            $archive = Join-Path $work $name
            Write-Host "Downloading Clippi $version..."
            Invoke-WebRequest -UseBasicParsing -Uri "$base/$name" -OutFile $archive
            Invoke-WebRequest -UseBasicParsing -Uri "$base/$name.sha256" -OutFile "$archive.sha256"
        }

        $sidecar = (Get-Content -LiteralPath "$archive.sha256" -Raw).Trim()
        if ($sidecar -notmatch '^([0-9a-f]{64})  (\S+)$' -or $Matches[2] -ne $name) {
            throw "Invalid checksum file for $name"
        }
        $expected = $Matches[1]
        # .NET rather than Get-FileHash/Expand-Archive: those live in modules
        # that Windows PowerShell fails to load when it inherits PowerShell 7's
        # PSModulePath.
        $sha = [Security.Cryptography.SHA256]::Create()
        $stream = [IO.File]::OpenRead($archive)
        try { $actual = -join ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($actual -ne $expected) { throw "Checksum mismatch for $name" }

        $extracted = Join-Path $work 'extracted'
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $extracted)
        $staged = Join-Path $extracted "clippi-$version-$platform"
        if (-not (Test-Path -LiteralPath (Join-Path $staged 'bin\clippi.exe') -PathType Leaf)) {
            throw "$name does not contain bin\clippi.exe"
        }

        New-Item -ItemType Directory -Force -Path $bin, (Join-Path $installDir 'share') | Out-Null
        $share = Join-Path $installDir 'share\clippi'
        if (Test-Path -LiteralPath $share) { Remove-Item -LiteralPath $share -Recurse -Force }
        # A running clippi.exe (including the one running `clippi upgrade`)
        # cannot be overwritten, but it can be renamed out of the way. Earlier
        # leftovers are removed once nothing is running them.
        Get-ChildItem -LiteralPath $bin -Filter 'clippi.exe.old-*' |
            Remove-Item -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $exe) {
            Move-Item -LiteralPath $exe -Destination "$exe.old-$([Guid]::NewGuid().ToString('N'))"
        }
        Copy-Item -LiteralPath (Join-Path $staged 'bin\clippi.exe') -Destination $exe
        Copy-Item -LiteralPath (Join-Path $staged 'share\clippi') -Destination $share -Recurse
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }

    if ($env:CLIPPI_NO_MODIFY_PATH -ne '1') {
        # Edit the registry value directly: [Environment]::SetEnvironmentVariable
        # would rewrite PATH as REG_SZ and expand entries such as %USERPROFILE%.
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
        try {
            $userPath = [string]$key.GetValue('Path', '', 'DoNotExpandEnvironmentNames')
            $entries = @($userPath -split ';' | Where-Object { $_ })
            if ($entries -notcontains $bin) {
                $key.SetValue('Path', (($entries + $bin) -join ';'), 'ExpandString')
                # Setting any user variable through .NET broadcasts the change,
                # so terminals opened afterwards see the new PATH.
                [Environment]::SetEnvironmentVariable('CLIPPI_INSTALL_REFRESH', '1', 'User')
                [Environment]::SetEnvironmentVariable('CLIPPI_INSTALL_REFRESH', $null, 'User')
                Write-Host "Added $bin to your user PATH."
            }
        } finally {
            $key.Close()
        }
        if (($env:Path -split ';') -notcontains $bin) { $env:Path = "$env:Path;$bin" }
    }

    Write-Host "Installed Clippi $version to $installDir. Run 'clippi' from your project directory."
}
