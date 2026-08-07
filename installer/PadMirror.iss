#ifndef ReleaseDir
  #error ReleaseDir must be passed to ISCC
#endif
#ifndef DistDir
  #error DistDir must be passed to ISCC
#endif

#define AppName "PadMirror"
#define AppVersion "0.1.3"

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
Name: "desktopicon"; Description: "Tao bieu tuong tren Desktop"; GroupDescription: "Bieu tuong:"

[Files]
Source: "{#ReleaseDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\PadMirror"; Filename: "{app}\PadMirror.exe"
Name: "{autodesktop}\PadMirror"; Filename: "{app}\PadMirror.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\dependencies\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Dang cai Microsoft Visual C++ Runtime..."; Flags: waituntilterminated skipifdoesntexist; Check: not VcRuntimeInstalled
Filename: "{app}\PadMirror.exe"; Description: "Mo PadMirror"; Flags: nowait postinstall skipifsilent

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
