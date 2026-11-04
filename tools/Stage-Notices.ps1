#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$notices = Join-Path $root 'dist/licenses'
$sourceOutput = Join-Path $root 'third-party-sources'
$working = Join-Path $root 'third_party/license-sources'
New-Item -ItemType Directory -Force $notices, $sourceOutput, $working | Out-Null
Copy-Item (Join-Path $root 'THIRD_PARTY.md') $notices

# Preserve exact-version source archives separately from the installer so users
# can inspect/rebuild/replace the dynamically linked open-source libraries.
foreach ($module in @('qtbase', 'qtsvg', 'qtimageformats', 'qttranslations')) {
    $archive = Join-Path $sourceOutput "$module-6.8.3.zip"
    $url = "https://github.com/qt/$module/archive/refs/tags/v6.8.3.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $archive
    $destination = Join-Path $working $module
    Expand-Archive -Force $archive $destination
    $source = Join-Path $destination "$module-6.8.3"
    if (!(Test-Path (Join-Path $source 'LICENSES'))) { throw "Missing $module licenses." }
    $moduleNotices = Join-Path $notices $module
    New-Item -ItemType Directory -Force $moduleNotices | Out-Null
    Copy-Item -Recurse -Force (Join-Path $source 'LICENSES') $moduleNotices
    Get-ChildItem $source -Recurse -File | Where-Object {
        $_.Name -match '(?i)^(LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE|AUTHORS|qt_attribution)' -and
        $_.FullName -notlike '*\LICENSES\*'
    } | ForEach-Object {
        $relative = $_.FullName.Substring($source.Length).TrimStart('\','/')
        $target = Join-Path $moduleNotices $relative
        New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
        Copy-Item $_.FullName $target
    }
}
$vlcSource = Join-Path $sourceOutput 'vlc-3.0.23.tar.xz'
Invoke-WebRequest -UseBasicParsing -Uri 'https://download.videolan.org/pub/videolan/vlc/3.0.23/vlc-3.0.23.tar.xz' -OutFile $vlcSource
$vlcNotices = Join-Path $notices 'vlc'
New-Item -ItemType Directory -Force $vlcNotices | Out-Null
Get-ChildItem (Join-Path $root 'third_party/vlc') -File | Where-Object {
    $_.Name -match '^(COPYING|AUTHORS|THANKS|README)'
} | Copy-Item -Destination $vlcNotices
$webNotices = Join-Path $notices 'WebView2'
New-Item -ItemType Directory -Force $webNotices | Out-Null
Get-ChildItem (Join-Path $root 'third_party/WebView2') -File -Recurse | Where-Object {
    $_.Name -match '(?i)^(LICENSE|NOTICE)' -and $_.Extension -match '\.(txt|md)$'
} | Copy-Item -Destination $webNotices
Copy-Item (Join-Path $root 'THIRD_PARTY.md') $sourceOutput
Get-ChildItem $sourceOutput -File | Where-Object {$_.Name -ne 'SHA256SUMS.txt'} | ForEach-Object {
    "$( (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $($_.Name)"
} | Set-Content -Encoding ascii (Join-Path $sourceOutput 'SHA256SUMS.txt')
Write-Host 'Vendor notices staged and exact-version source archives prepared.'
