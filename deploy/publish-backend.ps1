param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^https://[a-zA-Z0-9.-]+(:[0-9]+)?/?$')]
    [string]$Address
)

$Address = $Address.TrimEnd('/')

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

$health = Invoke-RestMethod "$Address/api/health" -TimeoutSec 15
if ($health.ready -ne $true) { throw "The API at $Address is not ready." }

$file = Join-Path $env:TEMP 'blade-backend.json'
$index = Join-Path $env:TEMP 'blade-backend.index'
[IO.File]::WriteAllText($file, "{`"url`":`"$Address`"}`n")
Remove-Item $index -ErrorAction SilentlyContinue
$blob = & git -c core.autocrlf=false hash-object -w $file
if ($LASTEXITCODE) { throw 'Could not prepare backend address.' }
$env:GIT_INDEX_FILE = $index
try {
    & git update-index --add --cacheinfo "100644,$blob,backend.json"
    if ($LASTEXITCODE) { throw 'Could not stage backend address.' }
    $tree = & git write-tree
    if ($LASTEXITCODE) { throw 'Could not write backend tree.' }
} finally {
    Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue
    Remove-Item $index -ErrorAction SilentlyContinue
    Remove-Item $file -ErrorAction SilentlyContinue
}

$commit = & git commit-tree $tree -m 'Publish server address'
if ($LASTEXITCODE) { throw 'Could not create backend address update.' }
& git push --quiet --force origin "${commit}:refs/heads/backend"
if ($LASTEXITCODE) { throw 'Could not publish the server address.' }

Write-Host "Published $Address. Reload the website to connect to the VPS."
