; =============================================================================
; CALT Productivity — Inno Setup installer (Focus UI + native enforcer bundle)
; =============================================================================
;
; Scope:
;   - Start Menu + optional Desktop shortcuts -> run_calt_desktop.bat (web Focus)
;   - Thin launchers; full git clone + .venv still REQUIRED
;   - Optional HKCU Run autostart
;   - Bundles calt_enforcer.exe when present under installer_payload\bin\
;   - Register Enforcer (Task Scheduler, native preferred) + Register Native (Admin)
;   - Option B uninstall protect: Start Menu -> Uninstall bat (password gate)
;
; NOT bundled: Python / venv / frozen Focus EXE (PyInstaller TBD)
;
; Compile:
;   scripts\desktop_tracker\installer\compile_installer.bat
;   OR Inno Setup 6 -> this .iss -> Output\CALTProductivitySetup-*.exe
;
; Prefer per-user: {localappdata}\CALT Productivity (default, no admin).
; =============================================================================

#define MyAppName "CALT Productivity"
#define MyAppSubtitle "Focus + Enforcer"
#define MyAppVersion "0.2.2-protect"
#define MyAppPublisher "CALT (local)"
#define MyAppURL "https://github.com/"
; Default repo hint shown on the custom page (edit before compile if desired).
; Leave empty to force the wizard page default to blank / previous install value.
#define RepoRootDefault ""

[Setup]
AppId={{A7C4D5E1-9B2F-4E8A-9C31-0C1A2B3C4D5E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppSubtitle}
AppPublisher={#MyAppPublisher}
; AppPublisherURL={#MyAppURL}
DefaultDirName={localappdata}\CALT Productivity
; Machine-wide (needs admin): DefaultDirName={autopf}\CALT Productivity
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-user install into LocalAppData — no UAC by default.
PrivilegesRequired=lowest
; PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=CALTProductivitySetup-{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName}
InfoBeforeFile=INSTALLER.md
SetupLogging=yes
; ArchitecturesAllowed=x64compatible
; ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &Desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "autostart"; Description: "Start {#MyAppName} when I log on (HKCU Run)"; GroupDescription: "Startup:"; Flags: unchecked
Name: "enforcer"; Description: "Register CALT Enforcer at logon (Task Scheduler — native C++ if built)"; GroupDescription: "Enforcer:"; Flags: unchecked

[Files]
; Thin launchers + docs (repo + venv NOT copied — see INSTALLER.md).
Source: "installer_payload\CALT Desktop.bat"; DestDir: "{app}"; DestName: "CALT Productivity.bat"; Flags: ignoreversion
Source: "installer_payload\Register Enforcer.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "installer_payload\Register Native Enforcer.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "installer_payload\Uninstall CALT Productivity.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "INSTALLER.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
; Native enforcer binary (copied by build_native_enforcer.bat into payload\bin)
Source: "installer_payload\bin\calt_enforcer.exe"; DestDir: "{app}\bin"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
; Start Menu — WorkingDir = repo root so relative tooling behaves like run_calt_desktop.bat
Name: "{group}\{#MyAppName}"; Filename: "{app}\CALT Productivity.bat"; WorkingDir: "{code:GetRepoRoot}"; Comment: "{#MyAppSubtitle} — web Focus + native enforcer"
Name: "{group}\Register Enforcer (Task Scheduler)"; Filename: "{app}\Register Enforcer.bat"; WorkingDir: "{code:GetRepoRoot}"; Comment: "Logon task — prefers native calt_enforcer.exe"
Name: "{group}\Register Native Enforcer (Admin Service)"; Filename: "{app}\Register Native Enforcer.bat"; WorkingDir: "{code:GetRepoRoot}"; Comment: "Windows Service CALTEnforcer (UAC)"
; Password-gated wrapper (option B) — not raw {uninstallexe}
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{app}\Uninstall CALT Productivity.bat"; WorkingDir: "{app}"; Comment: "Requires Focus unlock if Protect uninstall is on"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\CALT Productivity.bat"; WorkingDir: "{code:GetRepoRoot}"; Comment: "{#MyAppSubtitle}"; Tasks: desktopicon

[Registry]
; Optional logon autostart (per-user). Uninstall removes the value.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "CALT Productivity"; \
  ValueData: """{app}\CALT Productivity.bat"""; Flags: uninsdeletevalue; Tasks: autostart
; Persist repo path for repair / re-reads (also written to repo_root.txt).
Root: HKCU; Subkey: "Software\CALT\Desktop"; ValueType: string; ValueName: "RepoRoot"; \
  ValueData: "{code:GetRepoRoot}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\CALT\Productivity"; ValueType: string; ValueName: "RepoRoot"; \
  ValueData: "{code:GetRepoRoot}"; Flags: uninsdeletekey

[Run]
; Optional: open Start Menu target after install
Filename: "{app}\CALT Productivity.bat"; Description: "Launch {#MyAppName}"; \
  WorkingDir: "{code:GetRepoRoot}"; Flags: nowait postinstall skipifsilent unchecked
; Optional enforcer registration (runs repo PowerShell helper — native preferred)
Filename: "{app}\Register Enforcer.bat"; Description: "Register CALT Enforcer scheduled task"; \
  Flags: nowait postinstall skipifsilent unchecked; Tasks: enforcer

[UninstallDelete]
Type: files; Name: "{app}\repo_root.txt"
Type: files; Name: "{app}\*.log"
Type: filesandordirs; Name: "{app}\bin"

[Code]
var
  RepoPage: TInputDirWizardPage;
  GRepoRoot: string;

function IsWebView2Installed: Boolean;
begin
  { Detect Evergreen WebView2 Runtime (HKLM/HKCU). Used only if bootstrapper [Run] enabled. }
  { Double braces -> single brace after Inno constant expansion. }
  Result :=
    RegKeyExists(HKLM64, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}}') or
    RegKeyExists(HKLM32, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}}') or
    RegKeyExists(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}}');
end;

function GetRepoRoot(Param: string): string;
begin
  Result := Trim(GRepoRoot);
  if Result = '' then
    Result := ExpandConstant('{app}');
end;

function NormalizeRepoPath(const S: string): string;
begin
  Result := Trim(S);
  while (Length(Result) > 0) and (Result[Length(Result)] = '\') do
    SetLength(Result, Length(Result) - 1);
end;

function RepoLooksValid(const Root: string): Boolean;
begin
  Result :=
    (Root <> '') and
    DirExists(Root) and
    (FileExists(Root + '\scripts\desktop_tracker\run\run_calt_desktop.bat') or
     FileExists(Root + '\scripts\desktop_tracker\run_calt_desktop.bat'));
end;

procedure InitializeWizard;
var
  Prev: string;
begin
  { Inno 6: CreateInputDirPage starts with ZERO edits — must Add before Values[0]. }
  RepoPage := CreateInputDirPage(
    wpSelectDir,
    'CALT repository path',
    'This skeleton does not bundle Python - it shortcuts into your existing clone.',
    'Select the Cognitive-Aware Learning Tutor repo root (folder that contains scripts\ and backend\).' + #13#10 +
      'After PyInstaller lands, this page can be removed.',
    False,
    '');
  RepoPage.Add('');

  Prev := '';
  if RegQueryStringValue(HKCU, 'Software\CALT\Productivity', 'RepoRoot', Prev) then
    RepoPage.Values[0] := Prev
  else if RegQueryStringValue(HKCU, 'Software\CALT\Desktop', 'RepoRoot', Prev) then
    RepoPage.Values[0] := Prev
  else if '{#RepoRootDefault}' <> '' then
    RepoPage.Values[0] := '{#RepoRootDefault}'
  else
    RepoPage.Values[0] := ExpandConstant('{src}\..\..\..');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Root: string;
begin
  Result := True;
  if CurPageID = RepoPage.ID then
  begin
    Root := NormalizeRepoPath(RepoPage.Values[0]);
    if not RepoLooksValid(Root) then
    begin
      MsgBox(
        'That folder does not look like a CALT repo.' + #13#10 + #13#10 +
        'Expected file:' + #13#10 +
        '  <repo>\scripts\desktop_tracker\run\run_calt_desktop.bat' + #13#10 + #13#10 +
        'Clone/keep the full project and create .venv before using this skeleton.',
        mbError, MB_OK);
      Result := False;
      exit;
    end;
    GRepoRoot := Root;
  end;
end;

procedure WriteRepoRootFile;
var
  Path: string;
begin
  Path := ExpandConstant('{app}\repo_root.txt');
  SaveStringToFile(Path, GRepoRoot + #13#10, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if GRepoRoot = '' then
      GRepoRoot := NormalizeRepoPath(RepoPage.Values[0]);
    WriteRepoRootFile;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { HKCU Run + Software\CALT\* removed via [Registry] Flags.
    Enforcer scheduled task is NOT auto-removed — see INSTALLER.md. }
  if CurUninstallStep = usPostUninstall then
  begin
    MsgBox(
      'CALT Productivity shortcuts were removed.' + #13#10 + #13#10 +
      'If you registered the enforcer, unregister manually:' + #13#10 +
      '  Unregister-ScheduledTask -TaskName "CALT Enforcer" -Confirm:$false' + #13#10 +
      '  OR Admin: powershell -File scripts\desktop_tracker\install\install_native_enforcer.ps1 -Uninstall' + #13#10 + #13#10 +
      'Your git repo and .venv were not deleted.',
      mbInformation, MB_OK);
  end;
end;