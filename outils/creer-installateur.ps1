# Prépare les fichiers d'une version publiée sur GitHub (Releases) :
#
#   powershell -ExecutionPolicy Bypass -File outils/creer-installateur.ps1
#
# Produit dans dist\ :
#   NexoRetro-Setup-<version>.exe      l'installateur (Inno Setup)
#   NexoRetro-<version>-portable.zip   la version portable (à décompresser)
#   SHA256SUMS.txt                     l'empreinte SHA-256 de chacun
#
# La version est lue dans CMakeLists.txt : project(NexoRetro VERSION x.y.z ...).
# Ces noms de fichiers sont un CONTRAT avec la mise à jour automatique de
# l'application : ne pas les changer sans elle.
#
#   -SansCompiler     ne recompile pas (l'exe est déjà dans build\Release : CI)
#   -Cmake <chemin>   cmake.exe à utiliser (sinon celui du PATH)
#   -Iscc <chemin>    ISCC.exe d'Inno Setup 6 (sinon cherché aux endroits connus)
#   -EmpreintesSeules refait seulement SHA256SUMS.txt (après la signature : CI)

param(
    [switch]$SansCompiler,
    [string]$Cmake = "",
    [string]$Iscc = "",
    [switch]$EmpreintesSeules
)

$ErrorActionPreference = "Stop"
$projet = Split-Path -Parent $PSScriptRoot
$release = Join-Path $projet "build\Release"
$dist = Join-Path $projet "dist"

# --- 1. La version ----------------------------------------------------------
$ligne = Select-String -Path (Join-Path $projet "CMakeLists.txt") -Pattern "project\(NexoRetro VERSION (\d+\.\d+\.\d+)"
if (-not $ligne) { throw "Version introuvable dans CMakeLists.txt (project(NexoRetro VERSION x.y.z ...))" }
$version = $ligne.Matches[0].Groups[1].Value
Write-Host "Nexo Retro $version"
$setup = Join-Path $dist "NexoRetro-Setup-$version.exe"
$zip = Join-Path $dist "NexoRetro-$version-portable.zip"
$sommes = Join-Path $dist "SHA256SUMS.txt"

# Format de sha256sum : "<empreinte en minuscules>  <nom>" (deux espaces), fins
# de ligne Unix, sans BOM : « sha256sum -c SHA256SUMS.txt » sait le vérifier.
function Ecrire-Empreintes {
    $lignes = foreach ($fichier in @($setup, $zip)) {
        "{0}  {1}" -f (Get-FileHash $fichier -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $fichier -Leaf)
    }
    [IO.File]::WriteAllText($sommes, (($lignes -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
}

if ($EmpreintesSeules) {
    Ecrire-Empreintes
    Get-Content $sommes
    return
}

# --- 2. La compilation en Release -------------------------------------------
if (-not $SansCompiler) {
    if (-not $Cmake) { $Cmake = "cmake" }
    Push-Location $projet
    try {
        & $Cmake --preset windows
        if ($LASTEXITCODE -ne 0) { throw "cmake --preset windows a échoué" }
        & $Cmake --build --preset release --target emulateur
        if ($LASTEXITCODE -ne 0) { throw "La compilation a échoué" }
    }
    finally { Pop-Location }
}
$exe = Join-Path $release "NexoRetro.exe"
if (-not (Test-Path $exe)) { throw "NexoRetro.exe introuvable dans $release" }

# --- 3. Inno Setup (ISCC.exe, le compilateur d'installateurs) ----------------
if (-not $Iscc) {
    $candidats = @(
        (Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),   # winget --scope user
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),     # installation classique
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )
    $Iscc = $candidats | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $Iscc) {
    throw "Inno Setup 6 introuvable. Installez-le : winget install --id JRSoftware.InnoSetup -e --scope user"
}

New-Item -ItemType Directory -Force $dist | Out-Null
foreach ($ancien in @($setup, $zip, $sommes)) { if (Test-Path $ancien) { Remove-Item $ancien -Force } }

# --- 4. L'installateur --------------------------------------------------------
& $Iscc /Q "/DAppVersion=$version" "/DSourceDir=$release" "/DOutputDir=$dist" (Join-Path $projet "installateur\NexoRetro.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC a échoué (code $LASTEXITCODE)" }
if (-not (Test-Path $setup)) { throw "Installateur absent : $setup" }

# --- 5. La version portable ---------------------------------------------------
# Les fichiers sont à la RACINE du zip (pas de sous-dossier) : on décompresse
# dans le dossier de son choix. portable.txt garde les données à côté de l'exe.
$temp = Join-Path $dist "portable-temp"
if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
New-Item -ItemType Directory -Force $temp | Out-Null
Copy-Item $exe $temp
Copy-Item (Join-Path $release "assets") (Join-Path $temp "assets") -Recurse
Copy-Item (Join-Path $projet "LICENSE") (Join-Path $temp "LICENSE.txt")
$texte = "Portable copy of Nexo Retro: cores, BIOS, games, saves and settings are stored next to NexoRetro.exe.`r`n" +
         "Copie portable de Nexo Retro : cores, BIOS, jeux, sauvegardes et reglages sont ranges a cote de NexoRetro.exe.`r`n"
[IO.File]::WriteAllText((Join-Path $temp "portable.txt"), $texte, (New-Object Text.UTF8Encoding $false))
Compress-Archive -Path (Join-Path $temp "*") -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $temp -Recurse -Force

# --- 6. Les empreintes SHA-256 ---------------------------------------------------
Ecrire-Empreintes

Write-Host ""
Write-Host "Fichiers prêts dans $dist :"
foreach ($fichier in @($setup, $zip, $sommes)) {
    Write-Host ("  {0}  ({1:N1} Mo)" -f (Split-Path $fichier -Leaf), ((Get-Item $fichier).Length / 1MB))
}
