; Installateur de Nexo Retro (Inno Setup 6).
;
; Le plus simple est de passer par le script qui compile tout :
;
;   powershell -ExecutionPolicy Bypass -File outils/creer-installateur.ps1
;
; À la main (la version vient de project(NexoRetro VERSION x.y.z) dans
; CMakeLists.txt) :
;
;   iscc /DAppVersion=0.3.0 installateur\NexoRetro.iss
;
; Résultat : dist\NexoRetro-Setup-<version>.exe
;
; Choix importants :
;  - installation PAR UTILISATEUR, sans droits administrateur, dans
;    %LOCALAPPDATA%\Programs\NexoRetro (comme Discord ou VS Code) : la mise à
;    jour depuis l'application n'affiche donc jamais la fenêtre de l'UAC ;
;  - le fichier installed.txt, écrit à côté de NexoRetro.exe, dit au
;    programme qu'il est installé : ses données (cores, BIOS, jeux,
;    sauvegardes, réglages) vont alors dans %APPDATA%\NexoRetro (voir
;    src/main.cpp), que ni les mises à jour ni la désinstallation ne touchent ;
;  - en mode silencieux (/VERYSILENT, utilisé par la mise à jour automatique),
;    l'installateur ferme proprement NexoRetro.exe s'il tourne encore, puis le
;    relance à la fin.

; --- Valeurs passées par la ligne de commande (/D...), avec un défaut -------
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; Le dossier où se trouvent NexoRetro.exe et assets\ après la compilation.
#ifndef SourceDir
  #define SourceDir "..\build\Release"
#endif
; Le dossier où l'installateur est écrit.
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "Nexo Retro"
#define AppExe "NexoRetro.exe"
#define AppUrl "https://github.com/AdamchDarkness/NexoRetro"

[Setup]
; L'AppId identifie le programme pour Windows (liste « Applications
; installées ») : il ne doit JAMAIS changer, sinon une mise à jour serait vue
; comme un second programme. (Les accolades sont doublées : « {{ » est la
; façon d'écrire une accolade dans Inno Setup.)
AppId={{6E3B1C2F-8A4D-4F5B-9C7E-2D1A0B9F4E63}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=AdamchDarkness
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup

; Par utilisateur, sans administrateur : {localappdata}\Programs\NexoRetro.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\NexoRetro
; Une mise à jour réinstalle dans le dossier choisi la première fois.
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
; 64 bits uniquement (l'exécutable est compilé en x64).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; La licence (GPLv3) est affichée et doit être acceptée.
LicenseFile=..\LICENSE

; Icônes : celle de l'installateur et celle de « Applications installées ».
SetupIconFile=..\assets\logo\nexo.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}

; Fermer NexoRetro.exe s'il tourne encore (Gestionnaire de redémarrage de
; Windows : il demande poliment à l'application de se fermer, comme lors
; d'un arrêt du PC). On le relance nous-mêmes à la fin ([Run]), d'où
; RestartApplications=no.
CloseApplications=force
CloseApplicationsFilter=*.exe
RestartApplications=no

OutputDir={#OutputDir}
OutputBaseFilename=NexoRetro-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; L'assistant s'affiche dans la langue de Windows quand elle est disponible,
; sans demander.
ShowLanguageDialog=auto
LanguageDetectionMethod=uilanguage

[Languages]
; Anglais et français au minimum, puis les langues de l'interface de Nexo
; Retro que fournit Inno Setup. #if FileExists(...) : une langue absente de
; l'Inno Setup installé (ancienne version) est simplement ignorée.
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
#if FileExists(CompilerPath + "Languages\Spanish.isl")
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
#endif
#if FileExists(CompilerPath + "Languages\BrazilianPortuguese.isl")
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
#endif
#if FileExists(CompilerPath + "Languages\German.isl")
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
#endif
#if FileExists(CompilerPath + "Languages\Japanese.isl")
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
#endif
#if FileExists(CompilerPath + "Languages\Korean.isl")
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
#endif
#if FileExists(CompilerPath + "Languages\Arabic.isl")
Name: "ar"; MessagesFile: "compiler:Languages\Arabic.isl"
#endif

[CustomMessages]
; Nos propres textes. Sans préfixe : la version par défaut (anglais), utilisée
; par toutes les langues qui n'ont pas la leur.
DataKept=Your games, saves, BIOS files, emulators and settings have been kept in:%n%n%1%n%nDelete this folder yourself if you no longer need them.
fr.DataKept=Vos jeux, sauvegardes, BIOS, émulateurs et réglages ont été conservés dans :%n%n%1%n%nSupprimez ce dossier vous-même si vous n'en avez plus besoin.
#if FileExists(CompilerPath + "Languages\Spanish.isl")
es.DataKept=Tus juegos, partidas guardadas, BIOS, emuladores y ajustes se han conservado en:%n%n%1%n%nBorra esta carpeta tú mismo si ya no los necesitas.
#endif
#if FileExists(CompilerPath + "Languages\BrazilianPortuguese.isl")
ptbr.DataKept=Seus jogos, saves, BIOS, emuladores e configurações foram mantidos em:%n%n%1%n%nApague esta pasta você mesmo se não precisar mais deles.
#endif
#if FileExists(CompilerPath + "Languages\German.isl")
de.DataKept=Deine Spiele, Spielstände, BIOS-Dateien, Emulatoren und Einstellungen wurden behalten in:%n%n%1%n%nLösche diesen Ordner selbst, wenn du sie nicht mehr brauchst.
#endif

[Tasks]
; Le raccourci sur le bureau est proposé, décoché par défaut.
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Le dossier assets\ appartient entièrement au programme : on l'efface avant
; une mise à jour pour qu'aucun fichier d'une ancienne version ne traîne.
Type: filesandordirs; Name: "{app}\assets"

[Files]
Source: "{#SourceDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Assistant normal : case « Lancer Nexo Retro » sur la dernière page.
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; Mode silencieux (mise à jour depuis l'application) : relance automatique.
Filename: "{app}\{#AppExe}"; Flags: nowait; Check: RelancerApresMiseAJour

[UninstallDelete]
; installed.txt est écrit par le code ci-dessous, pas par [Files] : on le
; supprime explicitement. Les données (%APPDATA%\NexoRetro) ne sont PAS
; touchées.
Type: files; Name: "{app}\installed.txt"

[Code]
// Le langage de [Code] est du Pascal (Delphi simplifié).

// Vrai si l'option /NOLAUNCH figure sur la ligne de commande : installation
// silencieuse SANS relancer le programme (utile pour les tests automatiques).
function OptionPresente(const Nom: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Nom) = 0 then
      Result := True;
end;

// Condition (Check:) de la relance automatique, dans [Run].
function RelancerApresMiseAJour: Boolean;
begin
  Result := WizardSilent and not OptionPresente('/NOLAUNCH');
end;

// --- Attendre que NexoRetro.exe soit fermé ---------------------------------
// La mise à jour depuis l'application lance cet installateur juste AVANT
// que NexoRetro.exe ait fini de se fermer (une fraction de seconde). Tant
// qu'un programme tourne, Windows interdit d'ouvrir son .exe en écriture :
// on essaie donc de l'ouvrir, et on recommence toutes les 200 ms tant que
// c'est refusé (10 secondes au plus). Passé ce délai, le Gestionnaire de
// redémarrage (CloseApplications=force) le fermera.
const
  W_GENERIC_WRITE = $40000000;
  W_OPEN_EXISTING = 3;
  W_FILE_ATTRIBUTE_NORMAL = $80;
  W_INVALID_HANDLE_VALUE = -1;

// (Préfixe W_ : Inno Setup définit déjà certains de ces noms.)
// Fonctions de Windows (kernel32.dll), appelées directement depuis le Pascal.
function CreateFile(lpFileName: String; dwDesiredAccess, dwShareMode, lpSecurityAttributes,
  dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile: Integer): Integer;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseHandle(hObject: Integer): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

// Vrai si le fichier peut être ouvert en écriture exclusive (programme fermé).
function FichierLibre(const Chemin: String): Boolean;
var
  H: Integer;
begin
  H := CreateFile(Chemin, W_GENERIC_WRITE, 0, 0, W_OPEN_EXISTING, W_FILE_ATTRIBUTE_NORMAL, 0);
  Result := H <> W_INVALID_HANDLE_VALUE;
  if Result then
    CloseHandle(H);
end;

// Appelée juste avant l'installation, quand le dossier {app} est connu.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Exe: String;
  Essais: Integer;
begin
  Result := ''; // chaîne vide = on peut continuer
  Exe := ExpandConstant('{app}\{#AppExe}');
  if FileExists(Exe) then begin
    Essais := 0;
    while (not FichierLibre(Exe)) and (Essais < 50) do begin
      Sleep(200);
      Essais := Essais + 1;
    end;
    Log(Format('Attente de la fermeture de %s : %d ms', [Exe, Essais * 200]));
  end;
end;

// Après la copie des fichiers : le marqueur installed.txt, qui fait ranger
// les données dans %APPDATA%\NexoRetro (voir src/main.cpp).
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    SaveStringToFile(ExpandConstant('{app}\installed.txt'),
      'This copy of Nexo Retro was installed by its setup program.' + #13#10 +
      'Its data (cores, BIOS, games, saves, settings) is stored in %APPDATA%\NexoRetro.' + #13#10 +
      'Create a file named portable.txt next to NexoRetro.exe to keep the data here instead.' + #13#10,
      False);
end;

// Fin de la désinstallation : dire où sont restées les données. Pas de
// fenêtre en mode silencieux (/VERYSILENT).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Donnees: String;
begin
  if CurUninstallStep = usPostUninstall then begin
    Donnees := ExpandConstant('{userappdata}\NexoRetro');
    if DirExists(Donnees) and not UninstallSilent then
      MsgBox(FmtMessage(CustomMessage('DataKept'), [Donnees]), mbInformation, MB_OK);
  end;
end;
