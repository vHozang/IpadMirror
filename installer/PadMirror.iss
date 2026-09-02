#ifndef ReleaseDir
  #error ReleaseDir must be passed to ISCC
#endif
#ifndef DistDir
  #error DistDir must be passed to ISCC
#endif

#define AppName "PadMirror"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{475CC31D-376B-4C26-A4C1-23DF219178EE}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=PadMirror
DefaultDirName={localappdata}\Programs\PadMirror
DefaultGroupName=PadMirror
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
OutputDir={#DistDir}
OutputBaseFilename=PadMirrorSetup
SetupIconFile=..\assets\icons\PadMirror.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
UninstallDisplayIcon={app}\PadMirror.exe
CloseApplications=force
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "firewall"; Description: "Allow PadMirror through Windows Firewall (Private and Domain networks)"; GroupDescription: "AirPlay discovery:"; Flags: unchecked

[Files]
Source: "{#ReleaseDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\PadMirror"; Filename: "{app}\PadMirror.exe"; IconFilename: "{app}\PadMirror.exe"; IconIndex: 0; AppUserModelID: "PadMirror.Desktop"
Name: "{autodesktop}\PadMirror"; Filename: "{app}\PadMirror.exe"; IconFilename: "{app}\PadMirror.exe"; IconIndex: 0; AppUserModelID: "PadMirror.Desktop"; Tasks: desktopicon

[Run]
Filename: "{app}\dependencies\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated skipifdoesntexist; Check: not VcRuntimeInstalled
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\dependencies\configure-airplay-firewall.ps1"" -ExePath ""{app}\PadMirror.exe"""; Verb: "runas"; StatusMsg: "Requesting permission to configure Windows Firewall..."; Flags: shellexec waituntilterminated skipifsilent; Tasks: firewall
Filename: "{app}\PadMirror.exe"; Description: "Open PadMirror"; Flags: nowait postinstall skipifsilent

[Code]
function VcRuntimeInstalled: Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(
    HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed',
    Installed) and (Installed = 1);
end;
