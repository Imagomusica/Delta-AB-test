[Setup]
AppName=Dune EQ
AppVersion=0.8
AppPublisher=ImagoMusica
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
OutputDir=out
OutputBaseFilename=Dune-Setup
Compression=lzma2
SolidCompression=yes
UninstallFilesDir={commoncf64}\VST3\Dune.vst3\uninst
CreateUninstallRegKey=yes
UninstallDisplayName=Dune EQ (VST3)

[Types]
Name: "full"; Description: "Complete"
[Components]
Name: "vst3"; Description: "Dune VST3"; Types: full; Flags: fixed
Name: "standalone"; Description: "Dune Standalone (Dune.exe)"; Types: full

[Files]
Source: "..\dist\Dune.vst3\*"; DestDir: "{commoncf64}\VST3\Dune.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\dist\Dune.exe"; DestDir: "{autopf}\Dune"; Components: standalone; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Dune"; Filename: "{autopf}\Dune\Dune.exe"; Components: standalone
