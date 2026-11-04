#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$dependencyRoot = Join-Path $projectRoot 'third_party'
$cacheRoot = Join-Path $dependencyRoot 'downloads'
New-Item -ItemType Directory -Force $cacheRoot | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Download-File([string]$Url, [string]$Destination) {
    if (!(Test-Path $Destination)) {
        $partial = "$Destination.partial"
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $partial
        Move-Item -Force $partial $Destination
    }
}

# Frozen build inputs, not a claim that these will remain the newest releases.
# Update versions/checksums deliberately and rerun the Windows validation matrix.
$webVersion = '1.0.4314.35'
$webName = "microsoft.web.webview2.$webVersion.nupkg"
$webBase = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$webVersion/$webName"
$webArchive = Join-Path $cacheRoot "WebView2-$webVersion.zip"
$webHashFile = Join-Path $cacheRoot "WebView2-$webVersion.sha512"
Download-File $webBase $webArchive
Download-File "$webBase.sha512" $webHashFile
$sha512 = [System.Security.Cryptography.SHA512]::Create()
try {
    $stream = [System.IO.File]::OpenRead($webArchive)
    try { $webActual = [Convert]::ToBase64String($sha512.ComputeHash($stream)) } finally { $stream.Dispose() }
} finally { $sha512.Dispose() }
$webExpected = (Get-Content -Raw $webHashFile).Trim()
if ($webActual -ne $webExpected) { throw 'WebView2 SHA-512 mismatch; delete third_party/downloads and retry.' }
$webRoot = Join-Path $dependencyRoot 'WebView2'
if (Test-Path $webRoot) { Remove-Item -Recurse -Force $webRoot }
Expand-Archive -Path $webArchive -DestinationPath $webRoot

$vlcVersion = '3.0.23'
$vlcArchive = Join-Path $cacheRoot "vlc-$vlcVersion-win64.zip"
$vlcUrl = "https://download.videolan.org/pub/videolan/vlc/$vlcVersion/win64/vlc-$vlcVersion-win64.zip"
# Published at https://get.videolan.org/vlc/3.0.23/win64/vlc-3.0.23-win64.zip
$vlcSha256 = '992d19dbd0b8a7cde9167d2f7780b1ef6f92acc8a71acfa736101a21f35181e1'
Download-File $vlcUrl $vlcArchive
if ((Get-FileHash $vlcArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $vlcSha256) {
    throw 'VLC SHA-256 mismatch; delete the cached VLC archive and retry.'
}
$extracted = Join-Path $dependencyRoot 'vlc-extract'
if (Test-Path $extracted) { Remove-Item -Recurse -Force $extracted }
Expand-Archive -Path $vlcArchive -DestinationPath $extracted
$source = Join-Path $extracted "vlc-$vlcVersion"
if (!(Test-Path (Join-Path $source 'libvlc.dll'))) { throw 'Unexpected VLC archive layout.' }
$vlcRoot = Join-Path $dependencyRoot 'vlc'
if (Test-Path $vlcRoot) { Remove-Item -Recurse -Force $vlcRoot }
New-Item -ItemType Directory $vlcRoot | Out-Null
Get-ChildItem $source -Filter '*.dll' | Copy-Item -Destination $vlcRoot
Copy-Item -Recurse (Join-Path $source 'plugins') $vlcRoot
# Preserve bundled license / attribution material with the runtime.
Get-ChildItem $source -File | Where-Object { $_.Name -match '^(COPYING|AUTHORS|THANKS|README)' } | Copy-Item -Destination $vlcRoot
Remove-Item -Recurse -Force $extracted
Write-Host "Dependencies ready: WebView2 SDK $webVersion; libVLC $vlcVersion x64."
Write-Host 'Install the Microsoft WebView2 Evergreen Runtime separately if it is not already present.'
