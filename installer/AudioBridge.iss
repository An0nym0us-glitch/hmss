; AudioBridge - Inno Setup script (Inno Setup 6.1 or newer)
;
; Build:  ISCC.exe /DSourceDir=<deployment folder> /DAppVersion=1.0.0 installer\AudioBridge.iss
; Output: installer\Output\AudioBridge-Setup-x64.exe
;
; <deployment folder> must contain AudioBridge.exe plus its Qt runtime (produced by
; windeployqt or tools/deploy-mingw.sh).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\deploy\AudioBridge"
#endif

#define AppName "AudioBridge"
#define AppExe "AudioBridge.exe"

[Setup]
; Stable AppId: keeps upgrades and uninstall working across versions. Never change it.
AppId={{6E1B7F4A-3C2D-4E8B-9A51-0D2F7C1A9B38}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=AudioBridge
AppComments=Real-time multi-device audio routing and mixing
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=Output
OutputBaseFilename=AudioBridge-Setup-x64
SetupIconFile=..\src\app\resources\AudioBridge.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Start AudioBridge when I sign in to Windows"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion isreadme
Source: "..\docs\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; DestName: "THIRD_PARTY_NOTICES.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "Real-time audio routing and mixing"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Same value the application writes when "Launch at startup" is enabled in Settings.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "AudioBridge"; ValueData: """{app}\{#AppExe}"" --minimized"; Tasks: startup; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\AudioBridge\AudioBridge\app"; ValueType: string; ValueName: "launchAtStartup"; ValueData: "true"; Tasks: startup

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Make sure AudioBridge is not running while its files are removed.
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "StopAudioBridge"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
// Remove the startup entry on uninstall even if it was created by the app itself,
// and offer to delete the user's settings (devices, routes, preferences).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioBridge');
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    if not UninstallSilent then
      if MsgBox('Also delete your AudioBridge settings (enabled devices, routes and preferences)?',
                mbConfirmation, MB_YESNO) = IDYES then
        RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\AudioBridge');
  end;
end;
