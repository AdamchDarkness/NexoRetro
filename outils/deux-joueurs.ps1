# deux-joueurs.ps1 : lance deux émulateurs sur ce PC pour tester le jeu en ligne seul.
#
#   Joueur 1 : ton dossier de données habituel, joue au CLAVIER.
#   Joueur 2 : un dossier à part (joueur2\), joue avec la MANETTE n°1.
#
# Le joueur 2 partage ta bibliothèque (via un dossier surveillé, sans copier
# les jeux) et reçoit une copie de tes cores et de tes BIOS.
#
# Utilisation (depuis le dossier du projet) :
#   powershell -ExecutionPolicy Bypass -File outils\deux-joueurs.ps1
#   ... -Data "D:\Jeux\Emulateur"     ton dossier de données, s'il n'est pas à côté de l'exe
#   ... -SansManette                   pas de manette : le joueur 2 regarde seulement

param(
    [string]$Data = "",
    [string]$Pseudo2 = "Joueur 2",
    [switch]$SansManette
)
$ErrorActionPreference = "Stop"

$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build\Release\emulateur.exe")).Path
if (-not $Data) { $Data = Split-Path $exe }
$Data = (Resolve-Path $Data).Path
$data2 = Join-Path $Data "joueur2"
New-Item -ItemType Directory -Force $data2 | Out-Null

# 1. Les cores et les BIOS : copiés (robocopy ne recopie que ce qui a changé).
foreach ($folder in "cores", "bios") {
    $source = Join-Path $Data $folder
    if (Test-Path $source) {
        robocopy $source (Join-Path $data2 $folder) /E /NJH /NJS /NFL /NDL | Out-Null
    }
}

# 2. Le pseudo du joueur 2 (seulement la première fois).
$settings = Join-Path $data2 "settings.ini"
if (-not (Test-Path $settings)) {
    [IO.File]::WriteAllText($settings, "nickname=$Pseudo2`n", (New-Object Text.UTF8Encoding $false))
}

# 3. La bibliothèque : ton dossier roms\ et tes jeux ajoutés, en dossiers surveillés.
$userData1 = Join-Path $Data "userdata.txt"
$userData2 = Join-Path $data2 "userdata.txt"
$lines = @()
if (Test-Path $userData2) {
    $lines += Get-Content $userData2 -Encoding UTF8 | Where-Object { $_ -like "game`t*" } # son historique à lui
}
$lines += "folder`t" + (Join-Path $Data "roms")
if (Test-Path $userData1) {
    $lines += Get-Content $userData1 -Encoding UTF8 | Where-Object { $_ -like "folder`t*" -or $_ -like "file`t*" }
}
[IO.File]::WriteAllLines($userData2, [string[]]$lines, (New-Object Text.UTF8Encoding $false))

# 4. Lancement des deux fenêtres. Ce qu'elles écrivent dans la console va
#    dans un fichier (journal-joueur1.txt, journal-joueur2.txt) : utile
#    pour comprendre un problème.
$log1 = Join-Path $Data "journal-joueur1.txt"
$log2 = Join-Path $Data "journal-joueur2.txt"
Start-Process $exe -ArgumentList @("--data", "`"$Data`"", "--clavier") -RedirectStandardError $log1
Start-Sleep -Milliseconds 800
$input2 = if ($SansManette) { @("--clavier") } else { @("--manette", "1") }
Start-Process $exe -ArgumentList (@("--data", "`"$data2`"") + $input2) -RedirectStandardError $log2

Write-Host ""
Write-Host "Deux émulateurs sont lancés :"
Write-Host "  - fenêtre « Emulateur · <ton pseudo> » : joueur 1, au clavier (clique dedans pour jouer)"
if ($SansManette) {
    Write-Host "  - fenêtre « Emulateur · $Pseudo2 » : joueur 2, sans manette (il regarde)"
} else {
    Write-Host "  - fenêtre « Emulateur · $Pseudo2 » : joueur 2, à la manette n°1 (même en arrière-plan)"
}
Write-Host ""
Write-Host "Dans la fenêtre du joueur 1 : Amis > Créer le salon > Copier le code."
Write-Host "Dans celle du joueur 2 : Amis > colle le code > Rejoindre."
Write-Host "Puis le joueur 1 choisit le jeu et clique sur Lancer la partie."
