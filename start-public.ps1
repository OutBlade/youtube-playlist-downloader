# Starts the downloader with its public tunnel and tells the website where to find it.
# The tunnel address changes whenever the tunnel restarts; -Watch keeps it published.
param([switch]$Watch)
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

function Test-Docker { & docker info *> $null; return $LASTEXITCODE -eq 0 }

function Get-TunnelAddress {
    $log = & docker compose --profile public logs --no-log-prefix tunnel 2>&1 | Out-String
    $found = [regex]::Matches($log, 'https://[a-z0-9-]+\.trycloudflare\.com')
    if ($found.Count) { return $found[$found.Count - 1].Value }
}

function Test-Address($address) {
    try { return (Invoke-RestMethod "$address/api/health" -TimeoutSec 15).ready -eq $true } catch { return $false }
}

function Publish-Address($address) {
    $file = Join-Path $env:TEMP 'blade-backend.json'
    $index = Join-Path $env:TEMP 'blade-backend.index'
    [IO.File]::WriteAllText($file, "{`"url`":`"$address`"}`n")
    Remove-Item $index -ErrorAction SilentlyContinue
    $blob = & git -c core.autocrlf=false hash-object -w $file
    $env:GIT_INDEX_FILE = $index
    try {
        & git update-index --add --cacheinfo "100644,$blob,backend.json"
        $tree = & git write-tree
    } finally { Remove-Item Env:GIT_INDEX_FILE; Remove-Item $index -ErrorAction SilentlyContinue }
    $commit = & git commit-tree $tree -m 'Publish server address'
    & git push --quiet --force origin "${commit}:refs/heads/backend"
    if ($LASTEXITCODE) { throw 'Could not publish the server address.' }
}

if (-not (Test-Docker)) {
    $desktop = Join-Path $env:LOCALAPPDATA 'Programs\DockerDesktop\Docker Desktop.exe'
    if (-not (Test-Path $desktop)) { $desktop = Join-Path $env:ProgramFiles 'Docker\Docker\Docker Desktop.exe' }
    Start-Process $desktop
    foreach ($attempt in 1..60) { if (Test-Docker) { break }; Start-Sleep 5 }
    if (-not (Test-Docker)) { throw 'Docker did not start.' }
}
& docker compose --profile public up -d
if ($LASTEXITCODE) { throw 'Could not start the downloader.' }

$published = $null
do {
    $address = Get-TunnelAddress
    if ($address -and $address -ne $published -and (Test-Address $address)) {
        Publish-Address $address
        $published = $address
        Write-Host "Live at https://outblade.github.io/youtube-playlist-downloader/ (server: $address)"
    } elseif (-not $published) {
        Write-Host 'Waiting for the tunnel...'
    }
    if ($Watch -or -not $published) { Start-Sleep $(if ($published) { 60 } else { 5 }) }
} while ($Watch -or -not $published)
