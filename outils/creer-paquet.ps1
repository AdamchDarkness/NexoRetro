# Crée un paquet à donner à un ami : un dossier prêt à l'emploi et son .zip.
#
#   powershell -ExecutionPolicy Bypass -File outils/creer-paquet.ps1
#
# Contenu : emulateur.exe (compilé en Release, sans DLL à installer), ses
# polices et langues (assets), les cores de build\Release\cores (pour que
# l'ami ait EXACTEMENT les mêmes émulateurs : obligatoire pour jouer en
# ligne) et un mode d'emploi. Jamais de jeux, de BIOS, de sauvegardes ni de
# réglages personnels.
#
#   -MaxCoreMo 100   les cores plus gros sont laissés de côté (MAME : 370 Mo)
#   -SansCores       aucun core (l'ami les télécharge lui-même)

#   -Donnees "D:\Jeux\Emulateur"   prend d'abord les cores de CE dossier (celui
#                                  avec lequel vous jouez : en ligne, l'ami doit
#                                  avoir exactement les mêmes), puis complète
#                                  avec ceux de build\Release\cores.

param(
    [int]$MaxCoreMo = 100,
    [switch]$SansCores,
    [string]$Donnees = ""
)

$ErrorActionPreference = "Stop"
$projet = Split-Path -Parent $PSScriptRoot
$release = Join-Path $projet "build\Release"
$exe = Join-Path $release "emulateur.exe"
if (-not (Test-Path $exe)) { throw "Compilez d'abord en Release : cmake --build --preset release" }

# La version, lue dans CMakeLists.txt : project(Emulateur VERSION 0.1.0 ...)
$version = (Select-String -Path (Join-Path $projet "CMakeLists.txt") -Pattern "VERSION (\d+\.\d+\.\d+)").Matches[0].Groups[1].Value

$dist = Join-Path $projet "dist"
$dossier = Join-Path $dist "Emulateur"
$zip = Join-Path $dist "Emulateur-$version.zip"
if (Test-Path $dossier) { Remove-Item $dossier -Recurse -Force }
New-Item -ItemType Directory -Force $dossier | Out-Null

# 1. Le programme et ses fichiers (polices, langues).
Copy-Item $exe $dossier
Copy-Item (Join-Path $projet "assets") (Join-Path $dossier "assets") -Recurse

# 2. Les cores (les mêmes que les vôtres).
$cores = @()
New-Item -ItemType Directory -Force (Join-Path $dossier "cores") | Out-Null
if (-not $SansCores) {
    $sources = @()
    if ($Donnees) { $sources += Join-Path $Donnees "cores" }
    $sources += Join-Path $release "cores"
    foreach ($source in $sources) {
        foreach ($core in Get-ChildItem $source -Filter *.dll -ErrorAction SilentlyContinue) {
            if ($cores -contains ($core.BaseName -replace "_libretro$", "")) { continue } # déjà pris (dossier de jeu prioritaire)
            if ($core.Length / 1MB -gt $MaxCoreMo) {
                Write-Host ("  (core {0} laissé de côté : {1:N0} Mo)" -f $core.Name, ($core.Length / 1MB))
                continue
            }
            Copy-Item $core.FullName (Join-Path $dossier "cores")
            $cores += $core.BaseName -replace "_libretro$", ""
        }
    }
}
$liste = if ($cores.Count) { $cores -join ", " } else { "aucun / none" }

# 3. Le mode d'emploi (français + anglais).
$lisezmoi = (Get-Content (Join-Path $PSScriptRoot "LISEZMOI-paquet.txt") -Raw -Encoding UTF8) -replace "\{VERSION\}", $version -replace "\{CORES\}", $liste
[IO.File]::WriteAllText((Join-Path $dossier "LISEZMOI - README.txt"), $lisezmoi, (New-Object Text.UTF8Encoding $true))

# 4. Le .zip, à envoyer (WeTransfer, Google Drive, Discord si moins de 25 Mo...).
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $dossier -DestinationPath $zip -CompressionLevel Optimal

Write-Host ""
Write-Host ("Paquet prêt : {0}" -f $zip)
Write-Host ("  {0:N1} Mo · cores : {1}" -f ((Get-Item $zip).Length / 1MB), $liste)
