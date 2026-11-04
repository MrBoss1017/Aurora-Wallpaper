#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$StageDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist'),
    [string]$IsccPath
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$StageDir = (Resolve-Path $StageDir).Path
$prereqDir = Join-Path $projectRoot 'third_party/installer'
$outputDir = Join-Path $projectRoot 'installer-output'
New-Item -ItemType Directory -Force $prereqDir, $outputDir | Out-Null

$required = @('bin/AuroraWallpaper.exe', 'bin/Qt6Core.dll', 'bin/Qt6Gui.dll',
    'bin/Qt6Widgets.dll', 'bin/vcruntime140.dll',
    'bin/msvcp140.dll', 'bin/vlc/libvlc.dll', 'bin/vlc/libvlccore.dll',
    'bin/samples/aurora/aurora.hlsl', 'bin/samples/web-orbit/index.html', 'START-HERE.txt')
foreach ($relative in $required) {
    if (!(Test-Path (Join-Path $StageDir $relative) -PathType Leaf)) { throw "Missing staged payload: $relative" }
}
if (!(Get-ChildItem $StageDir -Recurse -Filter qwindows.dll)) { throw 'Qt Windows platform plugin is missing.' }
if (!(Get-ChildItem (Join-Path $StageDir 'bin/vlc/plugins') -Recurse -Filter '*plugin.dll')) { throw 'VLC plugins are missing.' }

if (!$IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    else { $IsccPath = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
}
if (!(Test-Path $IsccPath)) { throw 'The Windows packaging runner needs Inno Setup 6.3 or later.' }

$runtimeFile = Join-Path $prereqDir 'MicrosoftEdgeWebView2RuntimeInstallerX64.exe'
# Microsoft's official Evergreen x64 standalone installer. This is downloaded
# on the packaging runner and embedded; users do not need an online bootstrapper.
$downloadFile = Join-Path $prereqDir 'WebView2Runtime.download.exe'
Invoke-WebRequest -UseBasicParsing -Uri 'https://go.microsoft.com/fwlink/?linkid=2124701' -OutFile $downloadFile
$signature = Get-AuthenticodeSignature $downloadFile
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    Remove-Item $downloadFile -Force
    throw 'The WebView2 installer does not have a valid Microsoft signature.'
}
Move-Item -Force $downloadFile $runtimeFile

$cmake = Get-Content -Raw (Join-Path $projectRoot 'CMakeLists.txt')
if ($cmake -notmatch 'project\(AuroraWallpaper VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { throw 'Application version missing.' }
$version = $Matches[1]
$isccArguments = @("/DStageDir=$StageDir", "/DPrereqDir=$prereqDir", "/DOutputPath=$outputDir", "/DAppVersion=$version",
    (Join-Path $projectRoot 'installer/AuroraWallpaper.iss'))
& $IsccPath @isccArguments
if ($LASTEXITCODE) { throw 'Inno Setup compilation failed.' }
$setup = Join-Path $outputDir "AuroraWallpaper-Setup-$version-x64.exe"
if (!(Test-Path $setup)) { throw 'Compiler returned without the expected setup executable.' }
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $setup -Leaf)" | Set-Content -Encoding ascii "$setup.sha256"
$receipt = [ordered]@{
    applicationVersion = $version
    sourceCommit = $env:GITHUB_SHA
    qtVersion = '6.8.3'
    vlcVersion = '3.0.23'
    webViewSdkVersion = '1.0.4314.35'
    installer = (Split-Path $setup -Leaf)
    installerSha256 = $hash
    webViewRuntimeVersion = (Get-Item $runtimeFile).VersionInfo.ProductVersion
    webViewRuntimeSha256 = (Get-FileHash $runtimeFile -Algorithm SHA256).Hash.ToLowerInvariant()
    webViewSignature = $signature.Status.ToString()
}
$receipt | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $outputDir 'build-receipt.json')
Write-Host "Created installer: $setup"
