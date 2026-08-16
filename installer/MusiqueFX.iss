#ifndef AppName
#define AppName "Musique FX"
#endif
#ifndef AppVersion
#define AppVersion "1.0.0"
#endif
#ifndef RepoSlug
#define RepoSlug "musique-fx"
#endif
#ifndef StandaloneSource
#define StandaloneSource ""
#endif
#ifndef StandaloneExeName
#define StandaloneExeName "plugin.exe"
#endif
#ifndef Vst3Source
#define Vst3Source ""
#endif
#ifndef Vst3DirName
#define Vst3DirName "plugin.vst3"
#endif
#ifndef PresetsSource
#define PresetsSource ""
#endif
#ifndef OutputDir
#define OutputDir "."
#endif
#ifndef OutputBaseFilename
#define OutputBaseFilename "MusiqueFX_Setup"
#endif
[Setup]
AppId=MusiqueFX.{#RepoSlug}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=unicorn who dev / Charli Billabert
DefaultDirName={autopf}\Musique FX\{#AppName}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
[Files]
Source:"{#StandaloneSource}";DestDir:"{app}";Flags:ignoreversion
Source:"{#PresetsSource}\*";DestDir:"{app}\Presets";Flags:ignoreversion recursesubdirs createallsubdirs
Source:"{#Vst3Source}\*";DestDir:"{commoncf}\VST3\{#Vst3DirName}";Flags:ignoreversion recursesubdirs createallsubdirs
Source:"{#PresetsSource}\*";DestDir:"{commoncf}\VST3\{#Vst3DirName}\Presets";Flags:ignoreversion recursesubdirs createallsubdirs
