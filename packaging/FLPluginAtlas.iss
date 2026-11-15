#define AppVersion "0.1.0"
#define ProjectRoot AddBackslash(SourcePath) + ".."
#ifndef BuildDir
  #define BuildDir ProjectRoot + "\build\release"
#endif

[Setup]
AppId={{2DA33C55-74C9-439F-836A-2CFCAD1B413A}
AppName=FL Plugin Atlas
AppVersion={#AppVersion}
AppPublisher=RainDp
AppPublisherURL=https://github.com/RainDp/FL-Plugin-Atlas
AppSupportURL=https://github.com/RainDp/FL-Plugin-Atlas/issues
AppUpdatesURL=https://github.com/RainDp/FL-Plugin-Atlas/releases
DefaultDirName={autopf}\FL Plugin Atlas
DefaultGroupName=FL Plugin Atlas
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
LicenseFile={#ProjectRoot}\LICENSE
OutputDir={#ProjectRoot}\build\installer
OutputBaseFilename=FL-Plugin-Atlas-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=FL Plugin Atlas
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.clap
RestartApplications=no
SetupLogging=yes

[Files]
Source: "{#BuildDir}\FL Plugin Atlas.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion
Source: "{#BuildDir}\atlas.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ProjectRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ProjectRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ProjectRoot}\packaging\INSTALL.txt"; DestDir: "{app}"; DestName: "使用说明.txt"; Flags: ignoreversion
Source: "{#ProjectRoot}\packaging\licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\使用说明"; Filename: "{app}\使用说明.txt"
Name: "{group}\项目主页"; Filename: "https://github.com/RainDp/FL-Plugin-Atlas"
Name: "{group}\卸载 FL Plugin Atlas"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\使用说明.txt"; Description: "打开使用说明"; Flags: postinstall shellexec skipifsilent
