#ifndef Payload
  #error Payload directory is required
#endif
#ifndef OutputPath
  #error OutputPath is required
#endif
#ifndef SetupTest
  #define SetupTest "0"
#endif
#ifndef AppRelease
  #define AppRelease "1.0.4"
#endif

[Setup]
#if SetupTest == "1"
AppId={{716CCB26-F499-4D16-A14D-55A690B3EAF4}
AppName=VLC Waveform Installer Test
DefaultDirName={localappdata}\Temp\opencode\waveform-setup-test
OutputBaseFilename=VLC-Waveform-Setup-Test
DefaultGroupName=VLC Waveform Installer Test
#else
AppId={{E04FDC92-B1D7-4941-8A4B-C73EC9594A21}
AppName=VLC Waveform
DefaultDirName={localappdata}\VLCWaveform
OutputBaseFilename=VLC-Waveform-Setup-{#AppRelease}-x64
DefaultGroupName=VLC Waveform
#endif
AppVersion={#AppRelease}
AppVerName=VLC Waveform {#AppRelease}
AppPublisher=VLC Waveform
VersionInfoDescription=VLC Waveform - natives Plugin fuer VLC 3 x64
VersionInfoProductName=VLC Waveform
VersionInfoVersion={#AppRelease}.0
SetupIconFile=waveform.ico
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
WizardStyle=modern
LicenseFile={#Payload}\COPYING.txt
OutputDir={#OutputPath}
Compression=lzma2/normal
SolidCompression=yes
CloseApplications=no
RestartApplications=no
ChangesEnvironment=yes
UninstallDisplayName=VLC Waveform
UninstallDisplayIcon={app}\waveform.ico
SetupLogging=yes
UninstallLogging=yes
UsePreviousAppDir=no
UsePreviousGroup=no

[Languages]
Name: german; MessagesFile: compiler:Languages\German.isl

[Messages]
german.WelcomeLabel2=Dieser Assistent installiert die native Wellenform fuer VLC 3 (64 Bit).%n%nKein Browserfenster, kein separates Overlay, kein Hintergrunddienst.%n%nBitte VLC vor der Installation schliessen.
german.FinishedLabel=VLC Waveform wurde eingerichtet.%n%nVLC normal starten und eine Audiodatei oeffnen. In die Wellenform klicken, danach mit Space abhoeren.%n%nBereits laufende Programme, aus denen VLC gestartet wird, gegebenenfalls neu starten.

[Files]
Source: "setup-bridge.ps1"; DestName: "preflight.ps1"; Flags: dontcopy
Source: "{#Payload}\*"; DestDir: "{tmp}\VLCWaveformPayload"; Flags: ignoreversion recursesubdirs createallsubdirs deleteafterinstall
Source: "{#Payload}\source\*"; DestDir: "{app}\source"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\COPYING.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\WEITERGABE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\AENDERUNGEN.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\release-manifest.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\decoder\*"; DestDir: "{app}\decoder"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "ffmpeg.exe"
Source: "LIESMICH-Setup.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "setup-bridge.ps1"; DestDir: "{app}\setup-support"; Flags: ignoreversion; AfterInstall: ConfigurePlugin
Source: "Deinstallieren.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "waveform.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\VLC Waveform - Anleitung"; Filename: "{app}\LIESMICH-Setup.txt"
Name: "{group}\VLC Waveform - Quellen und Lizenzen"; Filename: "{app}\WEITERGABE.txt"
Name: "{group}\VLC Waveform entfernen"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: files; Name: "{app}\plugins\libwaveform_plugin.dll"
Type: files; Name: "{app}\decoder\ffmpeg.exe"
Type: files; Name: "{app}\decoder\COPYING.GPLv3.txt"
Type: files; Name: "{app}\decoder\ffmpeg-version.txt"
Type: files; Name: "{app}\Deinstallieren.ps1"
Type: files; Name: "{app}\LIESMICH.txt"
Type: files; Name: "{app}\setup-support\last-install.log"
Type: dirifempty; Name: "{app}\plugins"
Type: dirifempty; Name: "{app}\decoder"
Type: dirifempty; Name: "{app}\setup-support"

[Code]
var
  VlcPage: TInputDirWizardPage;

function VlcIsRunning: Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name="vlc.exe"');
    Result := Processes.Count > 0;
  except
    { PowerShell preflight repeats the check before any payload is changed. }
    Log('WMI process check unavailable; native preflight will check VLC.');
  end;
end;

function DefaultVlcDirectory: String;
begin
  if RegQueryStringValue(HKCU64, 'Software\VideoLAN\VLC', 'InstallDir', Result) and
     FileExists(AddBackslash(Result) + 'vlc.exe') then exit;
  if RegQueryStringValue(HKLM64, 'Software\VideoLAN\VLC', 'InstallDir', Result) and
     FileExists(AddBackslash(Result) + 'vlc.exe') then exit;
  Result := ExpandConstant('{pf64}\VideoLAN\VLC');
end;

procedure InitializeWizard;
begin
  VlcPage := CreateInputDirPage(wpWelcome, 'VLC auswaehlen',
    'Wo ist VLC 3 (64 Bit) installiert?',
    'Der Ordner muss die Datei vlc.exe enthalten. VLC selbst wird nicht ersetzt.', False, '');
  VlcPage.Add('VLC-Programmordner:');
  VlcPage.Values[0] := ExpandConstant('{param:VLCDIR|' + DefaultVlcDirectory + '}');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = VlcPage.ID) and
     not FileExists(AddBackslash(VlcPage.Values[0]) + 'vlc.exe') then begin
    SuppressibleMsgBox('Im ausgewaehlten Ordner wurde vlc.exe nicht gefunden.', mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function RunBridge(const Script, Action, LogPath: String): Boolean;
var
  Parameters: String;
  ExitCode: Integer;
begin
  Parameters := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File ' + AddQuotes(Script) +
    ' -Action ' + Action + ' -ApplicationRoot ' + AddQuotes(ExpandConstant('{app}')) +
    ' -LogPath ' + AddQuotes(LogPath);
  if (Action = 'Check') or (Action = 'Install') then
    Parameters := Parameters + ' -VlcDirectory ' + AddQuotes(VlcPage.Values[0]);
  if Action = 'Install' then
    Parameters := Parameters + ' -PayloadDirectory ' + AddQuotes(ExpandConstant('{tmp}\VLCWaveformPayload'));
#if SetupTest == "1"
  Parameters := Parameters + ' -SandboxRoot ' + AddQuotes(ExpandConstant('{app}\test-profile'));
#endif
  Log('Native configuration action: ' + Action);
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Parameters,
    '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  Result := Result and (ExitCode = 0);
  Log('Native configuration exit code: ' + IntToStr(ExitCode));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if VlcIsRunning then begin
    Result := 'Bitte VLC vollstaendig schliessen und dann auf Installieren klicken. Setup beendet keine laufenden Programme.';
    exit;
  end;
  ExtractTemporaryFile('preflight.ps1');
  if not RunBridge(ExpandConstant('{tmp}\preflight.ps1'), 'Check', ExpandConstant('{tmp}\vlc-waveform-preflight.log')) then
    Result := 'VLC 3 x64 konnte nicht bestaetigt werden. Bitte den VLC-Ordner pruefen und VLC schliessen.';
end;

procedure ConfigurePlugin;
var
  LogPath: String;
begin
  LogPath := ExpandConstant('{app}\setup-support\last-install.log');
  if not RunBridge(ExpandConstant('{app}\setup-support\setup-bridge.ps1'), 'Install', LogPath) then
    RaiseException('Die Einrichtung ist fehlgeschlagen. Details: ' + LogPath);
end;

function InitializeUninstall: Boolean;
var
  LogPath: String;
begin
  Result := not VlcIsRunning;
  if not Result then begin
    SuppressibleMsgBox('Bitte VLC vor der Deinstallation schliessen. Laufende Fenster werden nicht beendet.', mbError, MB_OK, IDOK);
    exit;
  end;
  { Check PowerShell and VLC again before the uninstaller can delete files. }
  LogPath := ExpandConstant('{tmp}\vlc-waveform-uninstall-check.log');
  Result := RunBridge(ExpandConstant('{app}\setup-support\setup-bridge.ps1'), 'CheckUninstall', LogPath);
  if not Result then
    SuppressibleMsgBox('Die Deinstallation kann noch nicht gestartet werden. Bitte VLC schliessen. Details: ' + LogPath, mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  LogPath: String;
begin
  if CurUninstallStep = usUninstall then begin
    LogPath := ExpandConstant('{tmp}\vlc-waveform-uninstall.log');
    if not RunBridge(ExpandConstant('{app}\setup-support\setup-bridge.ps1'), 'Uninstall', LogPath) then
      RaiseException('VLC-Einstellungen konnten nicht zurueckgesetzt werden. Details: ' + LogPath);
  end;
end;
