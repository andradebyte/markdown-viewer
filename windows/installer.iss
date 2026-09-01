; Inno Setup script for mdview (Windows installer)
; Compile with: ISCC.exe installer.iss   (from the windows/ folder)

#define MyAppName "Markdown Viewer"
#define MyAppVersion "1.0.0"
#define MyAppExeName "mdview.exe"
#define MyAppPublisher "andradebyte"

[Setup]
AppId={{5E4A6B2C-7D3E-4F8A-9B1C-2D4E6F8A0B1C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Markdown Viewer
DefaultGroupName=Markdown Viewer
OutputBaseFilename=mdview-setup-{#MyAppVersion}
OutputDir=..\
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "mdview.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Markdown Viewer"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Markdown Viewer"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.md";        ValueType: string; ValueName: ""; ValueData: "MarkdownViewer.Document"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.markdown";  ValueType: string; ValueName: ""; ValueData: "MarkdownViewer.Document"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.txt";       ValueType: string; ValueName: ""; ValueData: "MarkdownViewer.Document"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\MarkdownViewer.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\MarkdownViewer.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  WebView2Guid = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  EvergreenUrl = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';

function IsWebView2Installed: Boolean;
var
  Ver: string;
begin
  Result := RegQueryStringValue(HKLM,
    'Software\Microsoft\EdgeUpdate\Clients\' + WebView2Guid, 'pv', Ver) or
    RegQueryStringValue(HKCU,
    'Software\Microsoft\EdgeUpdate\Clients\' + WebView2Guid, 'pv', Ver);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and (not IsWebView2Installed) then
    if MsgBox('WebView2 Runtime is required for the preview. Install it now?',
      mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', EvergreenUrl, '', '', SW_SHOW, ewNoWait, nil);
end;
