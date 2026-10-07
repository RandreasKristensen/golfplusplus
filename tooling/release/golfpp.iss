; golf++'s Windows installer (Inno Setup 6). build_release.ps1 compiles it
; with /DAppVersion=<x.y.z> /DStageDir=<the staged install> /DOutputDir=<dir>.
;
; Per user, so no administrator rights: the game goes to
; %LOCALAPPDATA%\Programs\golf++. Installing over an older version upgrades
; it in place. Uninstalling removes only what was installed: the offline save
; and settings (%APPDATA%\golfplusplus) and the stored login (Windows
; Credential Manager) stay.

#ifndef AppVersion
  #error Build with tooling\release\build_release.ps1
#endif

[Setup]
; Never change the AppId: upgrades and the uninstaller find the game by it.
AppId={{EB6B8B7D-6FC2-4081-8388-AF90C9966893}
AppName=golf++
AppVersion={#AppVersion}
AppVerName=golf++ {#AppVersion}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\golf++
DefaultGroupName=golf++
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=golfpp-setup-{#AppVersion}
SetupIconFile=..\..\assets\icons\golfpp-icon.ico
UninstallDisplayIcon={app}\golf++.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An upgrade replaces the content: nothing an older version had stays behind.
Type: filesandordirs; Name: "{app}\assets"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\golf++"; Filename: "{app}\golf++.exe"
Name: "{autodesktop}\golf++"; Filename: "{app}\golf++.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\golf++.exe"; Description: "{cm:LaunchProgram,golf++}"; Flags: nowait postinstall skipifsilent
