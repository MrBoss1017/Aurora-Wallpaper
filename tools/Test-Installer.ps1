#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Installer)
$ErrorActionPreference = 'Stop'
$Installer = (Resolve-Path $Installer).Path
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) "AuroraInstallerTest-$testId"
$logRoot = Join-Path (Split-Path $Installer -Parent) 'test-logs'
New-Item -ItemType Directory -Force $logRoot | Out-Null
$installLog = Join-Path $logRoot 'install.log'
$uninstallLog = Join-Path $logRoot 'uninstall.log'

function Run-Checked([string]$Executable, [string]$Arguments, [int]$TimeoutSeconds) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -PassThru
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Process timed out: $Executable"
    }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Process exited with code $($process.ExitCode): $Executable" }
}

try {
    Run-Checked $Installer "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /TASKS=`"`" /DIR=`"$testRoot`" /LOG=`"$installLog`"" 600
    $application = Join-Path $testRoot 'bin/AuroraWallpaper.exe'
    if (!(Test-Path $application)) { throw 'Installer did not install the application.' }
    Run-Checked $application '--smoke-test' 45
    Write-Host 'Installed app loaded Qt, WebView2, libVLC, and compiled the bundled HLSL sample.'
    $uninstaller = Join-Path $testRoot 'unins000.exe'
    if (!(Test-Path $uninstaller)) { throw 'Uninstaller is missing.' }
    Run-Checked $uninstaller "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$uninstallLog`"" 120
    if (Test-Path $application) { throw 'Uninstaller left the application executable installed.' }
    Write-Host 'Setup/install/startup/uninstall smoke test passed.'
} finally {
    # This script is for a disposable Windows CI runner. Leave failure logs intact.
    if (Test-Path $testRoot) { Remove-Item -Recurse -Force $testRoot -ErrorAction SilentlyContinue }
}
