; Builds one offline setup wizard from an already compiled and staged app.
; This file is installer source, not an executable installer.
#ifndef StageDir
  #error StageDir must point to the complete CMake install directory.
#endif
#ifndef PrereqDir
  #error PrereqDir must point to the verified WebView2 standalone installer.
#endif
#ifndef OutputPath
  #define OutputPath "output"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.1"
#endif
#if !FileExists(StageDir + "\bin\AuroraWallpaper.exe")
  #error AuroraWallpaper.exe has not been built.
#endif
#if !FileExists(StageDir + "\bin\vcruntime140.dll")
  #error App-local MSVC runtime has not been staged.
#endif
#if !FileExists(PrereqDir + "\MicrosoftEdgeWebView2RuntimeInstallerX64.exe")
  #error Offline WebView2 prerequisite is missing.
#endif

[Setup]
AppId={{B6DD32CB-4685-45DF-A6AC-687AF66B26AE}
AppName=Aurora Wallpaper
AppVersion={#AppVersion}
AppPublisher=MrBoss
DefaultDirName={localappdata}\Programs\AuroraWallpaper
DefaultGroupName=Aurora Wallpaper
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.19045
WizardStyle=modern
SetupIconFile=..\resources\aurora.ico
UninstallDisplayIcon={app}\bin\AuroraWallpaper.exe
UninstallDisplayName=Aurora Wallpaper
AppMutex=Local\MrBoss.AuroraWallpaper
CloseApplications=yes
RestartApplications=no
OutputDir={#OutputPath}
OutputBaseFilename=AuroraWallpaper-Setup-{#AppVersion}-x64
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
Uninstallable=yes
VersionInfoVersion={#AppVersion}.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to Aurora Wallpaper
WelcomeLabel2=Bring your desktop to life with videos, interactive scenes, and animated wallpapers.%n%nSetup will install Aurora Wallpaper and the components it needs. No developer tools or build commands are required.

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "startup"; Description: "Start Aurora when I sign in to Windows"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
; Keep bin/, plugins/, and qt.conf in their deployed relative layout.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,vc_redist*.exe,aurora_policy_tests.exe"
; Extract only when needed. It is removed from the temporary directory by Setup.
Source: "{#PrereqDir}\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"; Flags: dontcopy

[Icons]
Name: "{userprograms}\Aurora Wallpaper"; Filename: "{app}\bin\AuroraWallpaper.exe"; WorkingDir: "{app}\bin"
Name: "{userdesktop}\Aurora Wallpaper"; Filename: "{app}\bin\AuroraWallpaper.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "AuroraWallpaper"; ValueData: """{app}\bin\AuroraWallpaper.exe"" --tray"; Flags: uninsdeletevalue; Tasks: startup
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "AuroraWallpaper"; Flags: deletevalue; Check: RemoveStartupEntry

[Run]
Filename: "{app}\bin\AuroraWallpaper.exe"; Description: "Launch Aurora Wallpaper"; WorkingDir: "{app}\bin"; Flags: nowait postinstall skipifsilent

[Code]
const
  WebViewKey = 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';

function RemoveStartupEntry: Boolean;
begin
  Result := not WizardIsTaskSelected('startup');
end;

function RuntimeInRegistry(Root: HKEY): Boolean;
var
  Version: String;
  Dot: Integer;
begin
  Result := False;
  if not RegQueryStringValue(Root, WebViewKey, 'pv', Version) then Exit;
  Dot := Pos('.', Version);
  if Dot <= 1 then Exit;
  { All native interfaces used by Aurora are present in runtime 100 and later. }
  Result := StrToIntDef(Copy(Version, 1, Dot - 1), 0) >= 100;
end;

function HasWebViewRuntime: Boolean;
begin
  Result := RuntimeInRegistry(HKLM32) or RuntimeInRegistry(HKCU32) or RuntimeInRegistry(HKCU64);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
  Started: Boolean;
begin
  Result := '';
  if HasWebViewRuntime then Exit;
  WizardForm.PreparingLabel.Caption := 'Installing a required component for interactive wallpapers...';
  ExtractTemporaryFile('MicrosoftEdgeWebView2RuntimeInstallerX64.exe');
  { The offline payload is Microsoft-signed and verified by the packaging job.
    Non-elevated execution requests a per-user installation. Do not uninstall
    this shared runtime when Aurora is removed. }
  Started := Exec(ExpandConstant('{tmp}\MicrosoftEdgeWebView2RuntimeInstallerX64.exe'),
    '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  if not Started then begin
    Result := 'Setup could not start the required component installer. Please try again.';
    Exit;
  end;
  Log(Format('WebView2 installer exit code: %d', [ExitCode]));
  if ExitCode = 3010 then NeedsRestart := True;
  if ((ExitCode <> 0) and (ExitCode <> 3010)) or not HasWebViewRuntime then
    Result := 'The required component could not be installed. Please restart Windows and run Setup again. Your existing Aurora installation has not been replaced.';
end;
