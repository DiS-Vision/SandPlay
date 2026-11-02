; Установщик SandPlay для Windows (Inno Setup 6.3 или новее).
; Версия ниже совпадает с src/AppVersion.h. Не поднимать её, пока такой релиз не опубликован.
; Скрипт кладёт в программу только SandPlay.exe и файлы Qt/runtime рядом с ним
; (каталог out\build\x64-release после windeployqt). Исходники он не трогает и сборку не запускает.
;
; Команда из корня репозитория:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\SandPlay.iss

#define MyAppName "SandPlay"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "DiS-Vision"
#define MyAppURL "https://github.com/DiS-Vision/SandPlay"
#define MyAppExeName "SandPlay.exe"
#define DeployDir "..\out\build\x64-release"

[Setup]
AppId={{B7E2C4A1-9F63-4D18-8A55-6C0E1D4F7A92}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
VersionInfoVersion=0.1.0.0
VersionInfoProductVersion=0.1.0.0
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableWelcomePage=yes
DisableDirPage=no
AllowCancelDuringInstall=yes
OutputDir=..\out\installer
OutputBaseFilename=SandPlay-setup
SetupIconFile=..\src\sandplay.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
MinVersion=10.0

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
russian.CreateStartMenuIcon=Ярлык в меню «Пуск»

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checked
Name: "startmenuicon"; Description: "{cm:CreateStartMenuIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checked

[Files]
Source: "{#DeployDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\sqldrivers\*"; DestDir: "{app}\sqldrivers"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startmenuicon
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
procedure InstallRuntime;
var
  ResultCode: Integer;
  Installed: Cardinal;
  Redist: String;
begin
  Redist := ExpandConstant('{tmp}\vc_redist.x64.exe');
  if not FileExists(Redist) then
    Exit;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then
  begin
    if Installed = 1 then
      Exit;
  end;
  WizardForm.StatusLabel.Caption := 'Устанавливаются компоненты Microsoft…';
  if not Exec(Redist, '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    MsgBox('Не удалось установить компоненты Microsoft. SandPlay может не запуститься.', mbError, MB_OK);
    Exit;
  end;
  { 0 — готово, 1638 — уже стоит другая сборка, 3010 — нужно перезагрузить компьютер. }
  if (ResultCode <> 0) and (ResultCode <> 1638) and (ResultCode <> 3010) then
    MsgBox('Не удалось установить компоненты Microsoft. SandPlay может не запуститься.', mbError, MB_OK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    InstallRuntime;
end;
