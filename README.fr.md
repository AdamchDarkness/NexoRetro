# Nexo Retro

[English](README.md) · **Français**

> Logiciel libre sous licence **GNU GPL v3** (voir [LICENSE](LICENSE)).

**Nexo Retro** est un frontend d'émulation multi-consoles pour Windows, basé sur [libretro](https://www.libretro.com/),
avec une interface claire et minimaliste : bibliothèque de jeux avec jaquettes, fiche de chaque jeu,
BIOS reconnus à leur contenu, émulateurs installés en un clic, **jeu en ligne sans serveur avec
rollback** (comme GGPO), chat vocal et câble link, retour en arrière, avance rapide, commandes
réglables console par console, cores mis à jour tout seuls et « Joue à... » sur Discord. Interface
en anglais, français, espagnol, portugais (Brésil), allemand, chinois simplifié, japonais, coréen et
arabe.

**Consoles** : NES, Super Nintendo, Nintendo 64, Game Boy, Game Boy Color, Game Boy Advance, Nintendo
DS, Nintendo 3DS, GameCube, Wii, Mega Drive / Genesis, Sega CD, Saturn, Dreamcast, PlayStation, PS2,
PSP, Atari 2600 / 5200 / 7800 / Lynx / Jaguar, arcade (FBNeo, Capcom CPS1/2/3, Neo Geo ; MAME en
secours), et par émulateur externe : Switch, Xbox, Xbox 360, PS3, PS4.

Le projet est aussi un projet d'apprentissage du C++ (pour quelqu'un qui connaît le C) : le code est
commenté, et les commentaires marqués `[C++]` expliquent ce qui diffère du C.

> ⚖️ **Aucun BIOS ni aucun jeu n'est fourni.** Utilisez uniquement les dumps de vos propres consoles
> et de vos propres jeux.

## Télécharger

Windows 10 ou 11, 64 bits. Chaque version est publiée sur la page
[**Releases**](https://github.com/AdamchDarkness/NexoRetro/releases), sous deux formes :

| Fichier | Pour qui |
| --- | --- |
| `NexoRetro-Setup-<version>.exe` | **L'installateur** (conseillé). Aucun droit administrateur demandé : le programme va dans `%LOCALAPPDATA%\Programs\NexoRetro`, avec un raccourci dans le menu Démarrer (et sur le bureau si on le souhaite). |
| `NexoRetro-<version>-portable.zip` | **La version portable** : à décompresser où l'on veut (une clé USB par exemple), rien n'est installé. |
| `SHA256SUMS.txt` | Les empreintes SHA-256 des deux fichiers, pour vérifier un téléchargement. |

**Où sont mes données ?** (cores, BIOS, jeux, sauvegardes, réglages)

- version **installée** : dans `%APPDATA%\NexoRetro`. Les mises à jour et la désinstallation n'y
  touchent pas : on les retrouve en réinstallant ;
- version **portable** (ou compilée soi-même) : à côté de `NexoRetro.exe`.

Une copie installée le sait grâce au fichier `installed.txt` que l'installateur pose à côté de
l'exécutable ; un fichier `portable.txt` au même endroit force le mode portable. L'option
`--data <dossier>` choisit un autre dossier, dans tous les cas.

**Mises à jour** : Nexo Retro vérifie lui-même s'il existe une nouvelle version sur la page des
Releases. Une copie installée se met à jour en un clic (l'installateur est téléchargé, vérifié avec
`SHA256SUMS.txt`, puis lancé en silence : il ferme le programme, le remplace sans toucher aux données
et le relance). Les cores, eux, se mettent à jour tout seuls (voir plus bas).

**Désinstaller** : Paramètres de Windows → Applications → Nexo Retro. Les données restent dans
`%APPDATA%\NexoRetro` (supprimez ce dossier vous-même si vous n'en voulez plus).

## Comment ça marche

Les émulateurs eux-mêmes sont des **cores libretro** : des DLL (`snes9x_libretro.dll`,
`pcsx_rearmed_libretro.dll`...) écrites par la communauté. Ce programme les charge et leur fournit
l'image, le son, les manettes et les dossiers de BIOS et de sauvegardes.

```
src/
  main.cpp, App.*        point d'entrée et chef d'orchestre (interface <-> jeu)
  Core.*                 chargement de la DLL et dialogue avec le core
  Video.*, GlFunctions   fenêtre et affichage OpenGL (contexte séparé pour les cores 3D)
  VulkanRender.*         rendu Vulkan des cores qui le demandent
  Audio.*, Input.*       son, clavier, manettes, souris (pointeur de la Wiimote)
  Remaps.*               commandes réglables par console (boutons et sticks)
  Rewind.*               retour en arrière (différences entre images, compressées)
  Library.*              consoles gérées, recherche des jeux, détection de la console
  UserData.*             historique, temps de jeu, favoris, jaquettes, manette Wii choisie
  Settings.*, Bios.*     réglages ; BIOS reconnus par leur empreinte (base System.dat)
  ExternalInstaller.*    installation en un clic : cores, émulateurs externes, fichiers libres
  ExternalSetup.*        fichiers système des émulateurs externes (firmware PS3, clés, BIOS Xbox)
  CoreOptions.*, Cheats.*, SaveFiles.*, Lang.*   options des cores, triches, sauvegardes, langues
  net/                   jeu en ligne sans serveur : UDP, STUN, UPnP, salon, rollback, câble link
  scrape/                jaquettes et informations (bases libretro)
  Ui.*                   mise en place d'ImGui et notifications
  ui/                    LE DESIGN (voir ci-dessous)
assets/fonts/            police Plus Jakarta Sans (licence OFL)
assets/lang/             traductions (en.txt)
assets/database/         System.dat de libretro : empreintes des BIOS connus
third_party/             libretro.h, libretro_vulkan.h (MIT), stb_image.h (domaine public)
tools/testcore/          mini-cores de test (jeu en ligne, câble link, Vulkan)
outils/                  creer-installateur.ps1 (fichiers d'une release), creer-paquet.ps1 (zip à
                         envoyer à un ami, avec vos cores), deux-joueurs.ps1 (tester le jeu en ligne seul)
installateur/            NexoRetro.iss : l'installateur (Inno Setup 6)
.github/workflows/       release.yml : compilation et publication automatiques à chaque tag v*
```

### Le design (`src/ui/`)

Organisé en couches, de la plus simple à la plus complète :

| Fichier | Rôle |
| --- | --- |
| `Theme.*` | Les jetons de design : couleurs, polices, échelle. Changer une couleur ici la change partout. |
| `Draw.*` | Primitives de dessin : ombres douces, texte, pointillés, icônes vectorielles. |
| `Widgets.*` | Composants cliquables : boutons, puces, interrupteurs, sélecteurs, champs. |
| `Covers.*` | Jaquettes générées, ou images choisies par l'utilisateur. |
| `Screens.hpp`, `Common.cpp` | Navigation, contexte partagé, mise en forme des dates et tailles. |
| `Shell.cpp` | La barre du haut et l'aiguillage vers l'écran courant. |
| `*Screen.cpp` | Un fichier par écran : Accueil, Bibliothèque, Fiche du jeu, Ajouter, BIOS, Amis, Paramètres, Big Picture. |
| `PauseMenu.*` | Le menu affiché par-dessus le jeu. |

## Compiler

Prérequis :

- **Visual Studio 2026 Build Tools** avec la charge de travail « Développement Desktop en C++ »
- **CMake 3.24+** : `winget install Kitware.CMake`
- **Git**

```bash
cmake --preset windows
```

```bash
cmake --build --preset release
```

Le premier `cmake --preset windows` télécharge et compile SDL3, Dear ImGui, miniupnpc, Opus et les
en-têtes Vulkan. L'exécutable se trouve ensuite dans `build/Release/NexoRetro.exe`, avec son dossier
`assets/`. Avec **Visual Studio 2022**, sans le preset :

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Créer une version (installateur et zip portable)

Il faut [Inno Setup 6](https://jrsoftware.org/isinfo.php)
(`winget install --id JRSoftware.InnoSetup -e --scope user`), puis :

```bash
powershell -ExecutionPolicy Bypass -File outils/creer-installateur.ps1
```

Le script compile en Release et produit dans `dist/` : `NexoRetro-Setup-<version>.exe`,
`NexoRetro-<version>-portable.zip` et `SHA256SUMS.txt`. La version vient de
`project(NexoRetro VERSION x.y.z)` dans `CMakeLists.txt`.

Publication : changer la version dans `CMakeLists.txt`, puis envoyer un tag annoté `v<version>`
(`git tag -a v0.3.0 -m "Notes de version"` puis `git push origin v0.3.0`). GitHub Actions
(`.github/workflows/release.yml`) compile, crée les trois fichiers et publie la release, avec le
message du tag comme notes.

L'installateur accepte les options habituelles d'Inno Setup : `/VERYSILENT /SUPPRESSMSGBOXES
/NORESTART` installe sans fenêtre (c'est ce que fait la mise à jour automatique) et relance Nexo Retro
à la fin ; `/NOLAUNCH` empêche cette relance (tests) ; `/DIR=<dossier>` change le dossier
d'installation.

## Utiliser

Lancez Nexo Retro : la bibliothèque s'ouvre. Les dossiers `cores/`, `bios/`, `roms/` et `saves/`
sont créés dans le dossier des données (`%APPDATA%\NexoRetro` pour une copie installée, à côté de
l'exécutable sinon, ou dans le dossier donné avec `--data <dossier>`).
Chaque jeu a son dossier de sauvegarde, `saves/<nom du jeu>/` : sa sauvegarde (`.srm`), ses
sauvegardes d'état et ses captures. La fiche du jeu a un bouton « Ouvrir le dossier de sauvegarde ».
Nexo Retro n'ouvre aucune fenêtre de terminal : son journal est écrit dans `NexoRetro.log`, dans le
dossier des données (utile quand un jeu ne se lance pas). Pour déboguer, `NexoRetro.exe --console`
l'affiche aussi en direct.

1. Installez les émulateurs : Paramètres → **Émulateurs (cores)** → « Installer les cores manquants »
   (ou un par un, ou depuis la fiche d'un jeu). Ils viennent du serveur officiel de libretro.
2. Ajoutez vos jeux depuis l'écran **Ajouter** (glisser-déposer, fichiers ou dossiers surveillés), ou
   rangez-les dans `roms/<console>/`.
3. Déposez vos BIOS sur l'écran **BIOS** (un fichier ou un dossier entier) : ils sont reconnus à leur
   contenu et rangés au bon endroit. GameCube/Wii et PSP n'ont pas de BIOS, mais des fichiers libres
   de Dolphin et PPSSPP, installables depuis la fiche d'un jeu.
4. Pour jouer en ligne : écran **Amis** → Créer le salon → envoyer le code (`EMU-XXXX-XXXX-XXXX-XXXX`) ;
   l'ami le colle et rejoint. Même fichier de jeu et même version d'émulateur des deux côtés. Le
   salon affiche le ping et les pertes de chacun, et la qualité de la connexion avant de lancer.

**Bibliothèque** : un seul rang de commandes (recherche, « Filtres », choix de la vue). La recherche
est instantanée (titre, console, genre, éditeur, année ; plusieurs mots, sans tenir compte des
accents). Les catégories (Tous, Favoris, Récents, consoles) tiennent sur une ligne qui défile à la
molette. « Filtres » ouvre une fenêtre pour trier et filtrer par genre, décennie, nombre de joueurs
et éditeur ; les filtres actifs s'affichent en pastilles qu'un clic retire. Trois vues : grille de
jaquettes, liste détaillée, carrousel (flèches, molette ou manette ; Entrée pour jouer).

**Mode Big Picture** (bouton télé en haut, ou Start à la manette ; Paramètres → Affichage →
« Démarrer en Big Picture ») : plein écran sombre, grandes jaquettes, fond tiré de la capture du jeu,
heure, et tout se pilote à la manette :

| Bouton | Action |
| --- | --- |
| Croix, stick gauche (flèches) | Jeu précédent / suivant |
| LB / RB (Page préc. / suiv.) | Catégorie (Tous, Favoris, Récents, chaque console) |
| A (Entrée) | Jouer |
| X (R) | Reprendre là où on s'était arrêté (sauvegarde automatique) |
| Y (F) | Ajouter aux favoris / retirer |
| B, Start (Échap) | Quitter Big Picture |

Après une partie, on revient dans Big Picture, toujours en plein écran.

**Chat vocal** (Paramètres → Jeu en ligne) : micro ouvert (n'envoie que quand on parle, sensibilité
réglable) ou appuyer pour parler (touche au choix, T par défaut), volume des voix, test du micro.
Dans le salon : couper son micro, ne plus entendre un membre, anneau vert autour de celui qui parle.
En jeu : menu pause → « Couper mon micro » et « Afficher le ping et les pertes ».

**Cores à jour** : au démarrage (une fois par jour), l'émulateur compare ses cores à ceux du serveur
de libretro et télécharge les nouvelles versions, jamais celle du jeu en cours (Paramètres →
Émulateurs (cores) → « Mise à jour automatique », « Vérifier maintenant »).

**Discord** : « Joue à Zelda · Super Nintendo », avec la jaquette et le temps de jeu, ou « En ligne
(2 joueurs) ». L'émulateur parle à l'application Discord ouverte sur le PC (tube nommé
`discord-ipc-0`), sans bibliothèque à installer. Il faut l'identifiant d'une application Discord
(créée une fois sur discord.com/developers) : `kDiscordAppId` dans `src/Discord.hpp`, ou Paramètres →
Jeu en ligne → Discord.

| Touche (en jeu) | Action |
| --- | --- |
| Flèches | Croix directionnelle |
| X / C / S / D | B / A / Y / X |
| E / R | L / R |
| Entrée / Maj droite | Start / Select |
| Échap, Select + Start | Menu pause |
| F2 / F4 | Sauvegarder / charger l'état |
| F8 | Redémarrer |
| F12 | Capture d'écran (dans le dossier de sauvegarde du jeu) |
| Tab (maintenu), Select + R | Avance rapide (×2 à ×8, Paramètres → Manettes) |
| Retour arrière (maintenu), Select + L | Retour en arrière (pas en ligne, ni PS2/GameCube/Wii) |
| F11 | Plein écran |

Menu pause → **Commandes de la console** : chaque action de la console émulée (bouton « 1 » de la
Wiimote, « Shake », boutons C de la N64...) et chacun de ses sticks peuvent être mis sur le bouton ou
le stick de son choix. Mémorisé par console, et pour la Wii par type de manette (choisi sur la fiche
du jeu). En ligne, chacun garde ses propres réglages sans désynchroniser la partie.

Dans l'interface : F5 actualise la bibliothèque, F1 affiche la démo d'ImGui (pour apprendre).

Pour les tests automatiques : `--screen library --frames 45 --screenshot capture.bmp` ouvre un écran,
attend 45 images puis quitte en enregistrant une capture.
Écrans de test supplémentaires : `covers` (fenêtre « Changer la jaquette » du premier jeu), `infos`
(Paramètres → Jaquettes et infos), `pads` (Paramètres → Manettes), `delete` (fenêtre « Supprimer »).
`--test-remove hide|trash` retire le premier jeu de la bibliothèque (ou l'envoie à la Corbeille).
`--library-view 0|1|2` ouvre la bibliothèque en grille, liste ou carrousel (sans l'enregistrer).
`--screen filters` ouvre la fenêtre des filtres ; `--big-picture` démarre en mode Big Picture.
Variables de test : `EMU_VOICE_TEST=tone` (un « la » à la place du micro), `EMU_VOICE_DUMP=fichier`
(la voix reçue, PCM 48 kHz mono), `EMU_TEST_CORE_UPDATE=1` (vérifie les cores même en test),
`EMU_DISCORD_TRACE=1` (tout ce que Discord répond). Pendant les tests automatiques, le vrai micro
n'est jamais ouvert.

## Speedrun

Un coin pour les speedrunners : l'onglet **Speedrun** en haut. C'est une première version, faite pour
grandir avec les retours de la communauté.

- **Catégories et splits** par jeu (Any%, 100%, Glitchless...) : ajouter, renommer, réordonner, supprimer
  des splits. Le dernier split arrête le chrono.
- **Chrono en jeu**, à la LiveSplit, en haut à gauche de l'image : chaque split avec votre avance ou votre
  retard sur votre record (vert en avance, rouge en retard), en **or** pour un meilleur segment, et le grand chrono.
- **Record, somme des meilleurs segments, essais et runs terminées** par catégorie, et l'**historique**
  complet des runs (terminées, abandonnées au split N, assistées).
- **Vidéo MP4 de chaque run** (H.264 + AAC, 720p, l'image du jeu sans le chrono), encodée par Windows
  lui-même (Media Foundation, avec la puce vidéo quand elle sait le faire) : rien à installer. On choisit
  le **dossier** et les vidéos à garder (toutes, runs terminées, ou records seulement) ; chaque ligne de
  l'historique a un bouton « Vidéo ».
- **« Remettre le compteur à zéro »** remet les essais à 0 (les records et l'historique restent).
- Le chrono compte le **temps réel (RTA)**, comme sur speedrun.com. Charger un état, rembobiner ou accélérer
  rend la run **« assistée »** : elle reste dans l'historique mais ne peut pas devenir un record.

| Touche (en jeu) | Action |
| --- | --- |
| F5 | Départ, puis split suivant |
| F6 | Annuler le dernier split |
| F7 | Passer un split |
| F9 | Remettre le chrono à zéro (et le jeu, au choix) : un nouvel essai |

« Lancer la run » dans l'onglet Speedrun démarre le jeu depuis le début, chrono affiché ; F5 dans n'importe
quel jeu le lance aussi (catégorie Any%). Tout est enregistré dans `speedrun.txt`, dans le dossier des
données (lisible au Bloc-notes, suit la copie portable). Indisponible pendant une partie en ligne.

- **Raccourcis à la manette** : les boutons de départ/split et de remise à zéro se choisissent dans l'onglet
  Speedrun (L3, R3, bouton central, Partage, pavé tactile).
- **Import et export LiveSplit** (`.lss`) : vos splits, votre record, vos meilleurs segments et vos essais
  arrivent en un clic, et repartent de la même façon.
- **Catégories officielles de speedrun.com** (son API publique gratuite) : les vraies catégories du jeu avec
  leur **record du monde** et son auteur, affichés dans l'onglet et dans le chrono en jeu (ligne WR).
- **Chrono dans la vidéo** (option) : l'enregistrement garde ce qui est dessiné par-dessus le jeu (chrono,
  touches) ; les notifications sont cachées pendant l'enregistrement.
- **Affichage des touches** (option) : une manette à l'écran montre les boutons appuyés, dans tous les jeux.

## Emplacements de sauvegarde et frise du retour en arrière

- **8 emplacements de sauvegarde par jeu**, avec une **miniature** du moment et sa date (menu pause →
  « Emplacements de sauvegarde »).
- **Frise du retour en arrière** : pendant le retour en arrière (Retour arrière, ou Select + L), une bande en
  bas de l'écran montre des miniatures du passé (une par seconde), jusqu'où on est remonté, et d'où on est parti.

## Contrôle parental

Paramètres → Contrôle parental : un **temps de jeu par jour** (15 min à 6 h), protégé par un **code parent**
(seule son empreinte est enregistrée). Cinq minutes avant la fin, un message prévient ; à la fin, la partie
est sauvegardée et Nexo Retro revient à la bibliothèque ; plus aucun jeu ne se lance jusqu'au lendemain,
sauf si un parent ajoute 15 ou 30 minutes avec son code.

## Feuille de route

- [x] **Étape 1** : charger un core, afficher, son, manettes, sauvegardes
- [x] **Étape 2** : rendu matériel OpenGL (cores 3D)
- [x] **Étape 3** : interface complète (bibliothèque, fiche, BIOS, réglages, ajout de jeux)
- [x] Touches réassignables, toutes les manettes (y compris génériques)
- [x] Sauvegarde du jeu visible et remplaçable (l'ancienne part à la Corbeille) ; codes de triche par jeu, avec les codes de la base libretro
- [x] Supprimer un jeu : le retirer de la bibliothèque, ou envoyer ses fichiers à la Corbeille
  (pistes `.bin` d'un `.cue` comprises) ; les jeux retirés se réaffichent depuis Paramètres → Dossiers
- [x] **Réseau, étape 1** : salon privé par code d'invitation, sans serveur (STUN, UPnP, perforation de NAT, chat)
- [x] **Réseau, étape 2** : salon complet (mot de passe, jusqu'à 8 joueurs, choix du jeu, vérification du fichier par empreinte)
- [x] **Réseau, étape 3** : netplay en "lockstep avec délai" (jusqu'à 4 joueurs, synchronisation de l'état, détection des désynchronisations)
- [x] **Rollback** (comme GGPO : mes touches s'appliquent tout de suite, celles de l'ami sont devinées puis corrigées)
- [x] Partie en ligne à partir de la sauvegarde de l'hôte (progression enregistrée chez lui à la fin)
- [x] **Câble link en ligne** (GBA avec gpSP, Game Boy avec Gambatte) : chacun sa console et sa sauvegarde
- [x] **Arcade et Neo Geo** (FBNeo, MAME en secours), en ligne compris
- [x] Sans-fil DS (melonDS DS) ; « chacun sa console » choisi d'office pour Game Boy, GBA et DS
- [x] **Plusieurs langues** (anglais par défaut, français) : fichiers `assets/lang/<code>.txt`
- [x] Dreamcast (Flycast), 3DS (Citra) ; Switch, Xbox, Xbox 360, PS3, PS4 par émulateur externe
- [x] Sticks arcade : disposition « stick arcade » en un clic, stick qui pilote la croix
- [x] Volume réglable (Paramètres → Son, et barre dans le menu en jeu)
- [x] Options des cores (Paramètres → Options des cores, en français quand le core est traduit)
- [x] **Scraping** : jaquettes, captures, genre, année, développeur, éditeur, joueurs (sources libretro, sans compte)
- [ ] Résumés des jeux en français (ScreenScraper, demande des identifiants développeur)
- [x] **BIOS reconnus par leur contenu** (empreinte CRC32, base `System.dat` de libretro), même mal nommés
- [x] **Installation en un clic** des émulateurs externes (pages officielles), import des fichiers système
- [x] **Rendu Vulkan** (avec négociation du device), au choix d'OpenGL pour les cores qui font les deux
- [x] Paramètres → **Émulateurs (cores)** : chaque core, installé ou mis à jour en un clic (serveur officiel de libretro)
- [x] GameCube et Wii séparées (reconnues au contenu du disque) ; manette Wii au choix par jeu ; visée à la souris
- [x] **Commandes de la console** (menu en jeu) : chaque action (Wiimote 1/2/Shake, C-Haut N64...) sur le bouton de son choix
- [x] Game Boy et Game Boy Color séparées, Sega CD, Saturn, Atari 2600/5200/7800/Lynx/Jaguar ; fichiers libres de Dolphin et PPSSPP
- [x] **Codes d'invitation courts** (16 symboles) ; les PC du même réseau local (box, Radmin, Hamachi) se trouvent seuls
- [x] **Ping et pertes** de chaque joueur, dans le salon et (au choix) pendant la partie ; qualité de connexion avant de lancer
- [x] **Chat vocal** (Opus) : micro ouvert ou appuyer pour parler, couper son micro ou un membre
- [x] **Bibliothèque** : filtres (genre, année, joueurs, éditeur), recherche sans accents, vues grille / liste / carrousel
- [x] **Mise à jour automatique des cores** (une fois par jour, jamais celui du jeu en cours)
- [x] Interface en **espagnol, portugais (Brésil) et allemand**
- [x] Interface en **chinois simplifié, japonais, coréen et arabe** (polices de Windows, arabe lié et de droite à gauche)
- [x] **Discord Rich Presence** (sans SDK : tube nommé de Discord)
- [x] Bibliothèque allégée : filtres dans une fenêtre, catégories sur une ligne, filtres actifs en pastilles
- [x] **Mode Big Picture** : plein écran, grandes jaquettes, entièrement à la manette
- [x] **Installateur et version portable** publiés sur GitHub (Releases), mises à jour depuis l'application
- [x] **Export et import des BIOS** (Paramètres → Sauvegardes) : une sauvegarde personnelle de votre dossier bios en .zip
- [x] **Taille du texte réglable** pour toute l'interface (Paramètres → Affichage)
- [x] **Soirée jeu** : l'hôte prépare une liste de jeux, le suivant est choisi pour tous à la fin de chaque partie
- [x] **Bibliothèque portable** : Paramètres → Dossiers → « Copie portable » met tout (programme, jeux, BIOS,
  sauvegardes, réglages) sur une clé USB utilisable sur n'importe quel PC ; les chemins sont notés par
  rapport au dossier des données
- [x] **Jeux sur un NAS / partage réseau** : un chemin `\\NAS\Jeux` devient un dossier surveillé (signalé
  injoignable quand le NAS est éteint, rien n'est oublié)
- [x] **Glisser-déposer d'un .zip de plusieurs ROM** : chaque jeu est décompressé dans `roms\<console>\`
  (les zips d'arcade restent tels quels)
- [x] **Export de la bibliothèque** en CSV (tableur) ou en page HTML autonome (Paramètres → Dossiers)
- [x] **Détection des doublons et des versions multiples** (fichiers identiques, autres régions), retrait en un clic
- [x] Onglet **Speedrun** : catégories et splits, chrono en jeu à la LiveSplit, records et segments en or,
  historique des runs, vidéo MP4 de chaque run (voir la section Speedrun)
- [x] Speedrun : raccourcis manette, import/export LiveSplit, catégories et records du monde de
  speedrun.com, chrono dans la vidéo, affichage des touches
- [x] **Emplacements de sauvegarde avec miniatures** et **frise du retour en arrière**
- [x] **Contrôle parental** (temps de jeu par jour, code parent)
- [x] **Jeu en ligne par le relais chiffré d'un ami** (votre IP reste cachée)
- [x] **Rollback pour PS2 / GameCube** grâce aux états différentiels (expérimental)
- [ ] Rendu Vulkan sans recopie (image partagée Vulkan/OpenGL), pour les très hautes résolutions
- [ ] **Liste d'amis** (prochaine mise à jour) : sans serveur, via l'annuaire décentralisé de BitTorrent
  (DHT, comme le messager Tox). Chaque joueur a une clé, son « code ami » est sa clé publique ; il y
  annonce son adresse du moment, chiffrée pour ses amis, puis la perforation de NAT existante prend
  le relais. Un serveur à soi (open source) pourra s'ajouter en option. En attendant : le code d'invitation.

## Langues

L'interface est en **anglais par défaut** ; Paramètres → Display → Language la passe en français,
espagnol, portugais (Brésil), allemand, chinois simplifié, japonais, coréen ou arabe.

Les écritures que la police de l'interface (Plus Jakarta Sans) n'a pas viennent des polices déjà
installées avec Windows, sans rien ajouter au paquet : Microsoft YaHei (chinois), Yu Gothic
(japonais), Malgun Gothic (coréen), Segoe UI (arabe). Leurs lettres sont « fusionnées » dans la
police de l'interface ; celle de la langue choisie passe en premier (les idéogrammes chinois et
japonais se dessinent un peu différemment), et les polices sont rechargées quand on change de langue.

L'arabe demande deux étapes de plus, car ImGui écrit lettre par lettre, de gauche à droite
(`src/Arabic.cpp`) : chaque lettre prend sa forme liée (isolée, initiale, médiane, finale, ligature
lam-alif), puis le texte est remis dans l'ordre d'affichage, les nombres et les mots latins restant
de gauche à droite. Les paragraphes sont coupés en lignes avant d'être retournés.
Le code est écrit en français, et chaque autre langue est un fichier `assets/lang/<code>.txt` :

```
# name: English
Jouer => Play
Il y a {n} min => {} min ago
Jeu choisi : {} => Game chosen: {}
```

`{}` remplace une partie variable, `{n}` un nombre seulement. La traduction se fait au moment de
dessiner le texte (`src/Lang.cpp`, appelé par `draw::text`, `draw::measure`...) : aucun écran n'a à
s'en soucier, et la mise en page s'adapte à la longueur des mots. **Ajouter une langue** : copier
`en.txt` (ex. `es.txt`), changer la ligne `# name:`, traduire la partie droite de chaque ligne. Pour
trouver les textes oubliés : lancer avec la variable `EMU_LANG_MISSING=manquants.txt`, qui note les
textes français affichés sans traduction.

## Consoles récentes : émulateurs externes

Switch, Xbox, Xbox 360, PS3 et PS4 n'ont pas de core libretro. Leurs jeux apparaissent dans la
bibliothèque (jaquettes, temps de jeu...), mais c'est un **émulateur à part** qui les lance :

| Console | Émulateur | Dossier `roms/` | Fichiers |
|---|---|---|---|
| Switch | Ryujinx | `switch` | `.nsp`, `.xci` |
| Xbox | xemu | `xbox` | `.iso` (xiso) |
| Xbox 360 | Xenia | `x360` | `.iso`, `.xex` |
| PS3 | RPCS3 | `ps3` | `.iso`, ou le dossier du jeu (`EBOOT.BIN`) |
| PS4 | shadPS4 | `ps4` | le dossier du jeu (`eboot.bin`) |

Paramètres → Émulateurs externes :

- **Installer** (PS3, PS4, Xbox, Xbox 360) télécharge la dernière version de l'émulateur sur sa page
  GitHub **officielle** (`src/ExternalInstaller.cpp`), la décompresse dans `emulators/<console>/`
  avec le `tar.exe` de Windows (qui lit aussi les `.7z`), la règle en mode portable et retient son
  chemin. **Mettre à jour** refait la même chose.
- **Choisir…** indique un émulateur déjà installé (obligatoire pour la Switch : voir plus bas).
- Les **fichiers système** ne sont **jamais téléchargés à la place de l'utilisateur**
  (`src/ExternalSetup.cpp`) : le bouton *Importer…* les range et règle l'émulateur.

| Console | Fichiers système | Ce que fait *Importer…* |
|---|---|---|
| PS3 | `PS3UPDAT.PUP` (gratuit sur le site de Sony) | lance `rpcs3 --installfw` |
| Switch | `prod.keys`, `title.keys` (de **votre** console) | les copie là où l'émulateur les cherche |
| Xbox | MCPX (512 octets), BIOS flash, disque `.qcow2` | les range dans `bios/xbox/` et écrit `xemu.toml` |
| PS4, Xbox 360 | rien d'obligatoire | — |

Pourquoi pas d'installation automatique pour la Switch : ses émulateurs (Yuzu, Ryujinx) ont été
arrêtés en 2024 après les actions de Nintendo ; ce qui en reste, ce sont des copies reprises par
d'autres équipes. Et ses clés servent à casser le chiffrement des jeux : les fournir serait illégal.

L'émulateur est lancé depuis son propre dossier, avec ses arguments (`{rom}` = le jeu) ; le temps
de jeu est compté jusqu'à sa fermeture. Pas de jeu en ligne ni de codes de triche pour ces consoles.

## BIOS reconnus par leur contenu

`assets/database/System.dat` (de libretro) donne la taille et l'empreinte (CRC32) de 500 BIOS. Un
fichier déposé sur l'écran BIOS, ou trouvé en fouillant un dossier (*Chercher dans un dossier*), est
reconnu **à son contenu**, même mal nommé, puis copié sous **tous** les noms où les cores le
cherchent (`dmg_boot.bin` *et* `gb_bios.bin`...). L'écran signale d'un « ! » un fichier présent mais
inconnu de la base (abîmé, modifié) ou qui est en fait un autre BIOS. Les BIOS de PS2 affichent leur
version et leur région (« PS2 v2.30 · Europe »).

## Rendu Vulkan

Les cores qui dessinent avec Vulkan (ParaLLEl-RDP pour la N64, Beetle PSX HW, PPSSPP, Dolphin...)
sont pris en charge (`src/VulkanRender.cpp`) : on crée le device Vulkan (ou on laisse le core le
créer, interface de « négociation » v1 et v2), le core dessine dans sa propre image, et on la recopie
dans la texture OpenGL où l'interface est dessinée. `vulkan-1.dll` (installée avec le pilote
graphique) est chargée à l'exécution : sans elle, seuls les cores Vulkan sont indisponibles.
Paramètres → Affichage → *Rendu Vulkan* : pour les cores qui savent faire les deux, demande Vulkan
plutôt qu'OpenGL. Le mini-core `vulkan_test_libretro.dll` (`tools/testcore/vulkantest.cpp`) teste
toute la chaîne sans jeu : `NexoRetro.exe vulkan_test_libretro.dll` affiche un fond rouge qui
pulse, vert quand on appuie sur A.

## Arcade et Neo Geo

- **Core** : `fbneo_libretro.dll` (FinalBurn Neo, celui de Fightcade : le meilleur pour jouer en
  ligne). À défaut, l'émulateur prend `mame2003_plus_libretro.dll` ou `mame_libretro.dll` s'ils sont
  dans `cores/`. La fiche du jeu indique le core utilisé.
- **Jeux** : un jeu = un `.zip` au nom imposé (`sf2.zip`, `kof98.zip`), d'un romset qui correspond
  au core (romset FBNeo pour FBNeo). Rangez-les dans `roms/arcade/` ou `roms/neogeo/`, ou dans un
  dossier surveillé dont le nom contient « arcade », « mame », « fbneo » ou « neogeo ».
- **Noms** : le scraper retrouve le vrai titre grâce au nom du fichier (`kof98.zip` → « The King of
  Fighters '98 »), avec la jaquette, l'année et l'éditeur.
- **BIOS** : `neogeo.zip` va dans `bios/fbneo/`. S'il est rangé avec les jeux, l'émulateur le copie
  tout seul. Les BIOS et puces d'arcade (`neogeo.zip`, `qsound.zip`, `pgm.zip`...) ne sont pas
  affichés comme des jeux.
- **En ligne** : comme les autres consoles, en rollback. L'émulateur dit au core que ses sauvegardes
  d'état servent au jeu en ligne (`GET_SAVESTATE_CONTEXT`) et lui permet de sauter l'image et le son
  pendant les recalculs (`GET_AUDIO_VIDEO_ENABLE`). Tous les joueurs doivent avoir le même core et
  le même fichier (vérifié dans le salon).

### Options des cores

Chaque core annonce ses options et leurs valeurs par défaut ; l'émulateur les lui rend quand il les
demande (comme RetroArch), et lui dit qu'on parle français : les cores traduits donnent alors leurs
options en français. **Paramètres → Options des cores** les affiche toutes, core par core ; ce qui
diffère du défaut est rangé dans `core-options.txt` (à côté de `userdata.txt`, modifiable à la main) :

```
mame_softlists_enable=enabled
```

Certains cores (FBNeo) n'annoncent leurs options qu'une fois un jeu chargé : l'émulateur retient
celles vues au dernier lancement (dossier `core-options/`). En ligne, des options différentes
entre joueurs empêchent le lancement (les parties divergeraient).

Quand un jeu ne démarre pas, l'émulateur affiche la raison donnée par le core : par exemple
« fichiers absents du zip : le romset ne correspond pas à la version de l'émulateur » (MAME et
FBNeo demandent un romset de leur version exacte).

## Jaquettes et informations (scraping)

Dès qu'un jeu arrive dans la bibliothèque, l'émulateur cherche en arrière-plan sa jaquette, son
écran-titre, une capture en jeu, son genre, son année, son développeur, son éditeur et son nombre de
joueurs. Aucun compte n'est nécessaire, les sources sont libres :

- **libretro-database** : un fichier `.rdb` par console (format MessagePack), téléchargé une fois
  puis gardé 30 jours ;
- **thumbnails.libretro.com** : les images, rangées par nom officiel (No-Intro).

Reconnaître le jeu :

1. pour les cartouches (NES, SNES, N64, GB, GBA, DS, Mega Drive), l'émulateur calcule le **CRC32**
   du fichier (en retirant les en-têtes ajoutés par certains formats, et en remettant les ROM N64
   dans l'ordre d'octets standard) : s'il est dans la base, c'est forcément le bon jeu ;
2. sinon, il compare les noms en ignorant la ponctuation, « The », les mentions entre parenthèses
   et l'écriture des numéros (II = 2), puis le sous-titre seul (« Zelda III - A Link to the Past »
   retrouve « Legend of Zelda, The - A Link to the Past »). Entre plusieurs versions, il préfère
   la région du fichier, puis France et Europe, jamais une démo ou un prototype.

Tout est gardé dans `scraper/` : `games.txt` (ce qui a été trouvé, lisible au Bloc-notes), `db/`,
`lists/` et `images/`. Sur la fiche d'un jeu, le bouton ↻ relance la recherche, et « Changer la
jaquette → Trouvées en ligne » propose les jaquettes de toutes les régions, avec une recherche
libre. La recherche de la bibliothèque trouve aussi les genres, éditeurs et années (« combat »,
« Capcom », « 1995 »). Réglages dans Paramètres → Jaquettes et infos.

Code : `src/net/Http.cpp` (téléchargements HTTPS avec WinHTTP), `src/scrape/GameDatabase.cpp`
(lecture des `.rdb` et comparaison des noms), `src/scrape/Scraper.cpp` (le thread et le cache).

## Jeu en ligne : comment ça marche

### Sans montrer son IP : le relais chiffré d'un ami

Un ami ouvre un **relais** (onglet Amis → « Ouvrir un relais ») et vous donne son code `RLY-...`. En créant
votre salon avec « Cacher mon adresse IP » et ce code, tout passe par son PC : les joueurs ne connaissent
que l'adresse du relais, jamais la vôtre, et vous ne voyez jamais la leur. Les paquets sont **chiffrés de
bout en bout** (AES-256-GCM, clé tirée d'un secret aléatoire du code d'invitation, calcul fait par BCrypt de
Windows) : le relais les transmet sans pouvoir les lire ni les modifier. Ça aide aussi quand deux box
refusent de se parler en direct. Testé sur un PC (relais + hôte + joueur, vrai jeu) : 0 désynchronisation.

### Rollback pour les consoles lourdes (expérimental)

Les états d'une PS2 ou d'une GameCube pèsent des dizaines de Mo : en garder un par image pour le rollback
était impossible, ces consoles passaient en mode délai. Avec Paramètres → Jeu en ligne → « Rollback pour
PS2 et GameCube », l'anneau du rollback garde des **états différentiels** : un état « clé » complet toutes
les 30 images, et pour chaque image seulement sa différence (XOR, compressée) avec lui : quelques Ko. La
profondeur est limitée (3 à 6 images) et chaque joueur doit avoir un PC rapide. Vérifié avec les états
différentiels forcés sur GBA (0 désynchronisation) ; pas encore testé sur un vrai jeu PS2.

Il n'y a **aucun serveur ni base de données** : l'émulateur de l'hôte est le salon.

1. L'hôte crée un salon : l'émulateur découvre son adresse publique (STUN, un service public gratuit)
   et demande à la box d'ouvrir un port (UPnP).
2. Il partage le code `EMU-XXXX-XXXX-XXXX-XXXX` (Discord, WhatsApp...) : 16 symboles qui contiennent
   le numéro du salon et son adresse publique. L'hôte écoute de préférence sur les ports 47800 à
   47809.
3. L'ami colle le code : son émulateur essaie l'adresse du code et, en même temps, appelle tout son
   réseau local sur ces ports (« diffusion ») : un hôte sur la même box, sur Radmin ou sur Hamachi
   répond sans connaître son adresse. Si la box de l'hôte bloque, l'ami reçoit un **code de réponse**
   à renvoyer : l'hôte le colle et les deux box s'ouvrent l'une vers l'autre (perforation de NAT).
4. En dernier recours, Radmin VPN, Hamachi ou Tailscale fonctionnent directement. Tailscale ne
   transmet pas la diffusion : l'hôte donne alors le **code complet** (lien sous le code), qui
   contient toutes ses adresses.

Pendant tout le salon, chaque liaison envoie 4 « pings » par seconde : leur temps de retour donne le
ping, et ceux restés sans réponse sur les 5 dernières secondes donnent les **pertes**. La voix
(chat vocal) passe par le même port : 20 ms de micro compressés par Opus (~60 octets), sans garantie
d'arrivée ; l'hôte relaie la voix de chacun aux autres, et un morceau perdu est reconstitué grâce à
la copie de secours (FEC) glissée dans le suivant.

Dans le salon, l'hôte choisit un jeu : chaque membre vérifie qu'il a **exactement le même fichier**
grâce à une empreinte de 64 bits (calculée en arrière-plan, gardée dans `hashes.txt`), et apparaît
« Prêt », « Autre version » ou « Jeu introuvable ».

Quand tout le monde est « Prêt », l'hôte lance la partie :

1. chacun démarre le jeu (sans charger sa sauvegarde personnelle, qui n'est pas écrasée non plus) ;
2. l'hôte envoie une **photographie complète de son émulateur** (sauvegarde d'état) : tout le monde
   part exactement du même point ;
3. tous les émulateurs calculent la même partie avec les mêmes touches. Deux modes, choisis
   automatiquement par l'hôte :
   - **rollback** (par défaut) : on n'attend pas les touches de l'ami, on les **devine** (il appuie
     sûrement sur la même chose qu'avant). Chaque image est photographiée ; quand ses vraies touches
     arrivent et diffèrent, on revient à la photo de cette image et on recalcule jusqu'à maintenant,
     sans le son, en une fraction de seconde. Mes touches s'appliquent avec 1 ou 2 images de délai
     seulement. Si un joueur prend de l'avance, il ralentit un peu pour laisser l'autre le rattraper ;
   - **délai** : on attend les touches de tout le monde ; les miennes s'appliquent quelques images
     plus tard (selon le ping). Utilisé quand le rollback serait trop coûteux ou faux : état de plus
     de 8 Mo (cores 3D), ou core dont les sauvegardes d'état ne sont pas fiables (signalé par le core,
     ou liste connue : 2048, Mr.Boom). Variable `EMU_NET_FORCE_DELAY=1` pour l'imposer.

   Chaque paquet reprend les touches pas encore confirmées : un paquet perdu est rattrapé par le suivant ;
4. toutes les 2 secondes, l'empreinte de l'état de chacun est comparée à celle de l'hôte pour
   détecter une désynchronisation.

### Câble link (GBA, Game Boy)

Sur une console **portable** (Game Boy, GBA, DS), chaque joueur avait SA console, reliée aux autres
par câble ou sans fil : quand l'hôte choisit un tel jeu, le salon passe **tout seul** en « Chacun sa
console » (l'hôte peut revenir à « Même console », pour jouer ensemble à un jeu solo). Les consoles
de salon (SNES, N64, PS1...) restent en console partagée, où il faut le même fichier. Chacun joue alors **son
propre jeu avec sa propre sauvegarde** (Rubis d'un côté, Saphir de l'autre) ; seules les données du
câble voyagent. Chaque joueur choisit son jeu (« Choisir mon jeu » ; automatique s'il porte le même
nom que celui de l'hôte, ou s'il n'a qu'un jeu de cette console). Si quelqu'un quitte, le câble est
débranché, mais chacun continue sa partie.

- **GBA** : core **gpSP** (`gpsp_libretro.dll`) chez chaque joueur, jusqu'à 4. Il émule le câble
  Pokémon 3e génération, l'adaptateur sans fil (Rouge Feu / Vert Feuille / Émeraude) et Advance
  Wars (détection automatique, option « Link Cable Connectivity »). Ses paquets passent par
  l'interface libretro *netpacket* et les messages fiables du salon (`src/net/LinkPlay.cpp`).
- **DS** : core **melonDS DS** (`melondsds_libretro.dll`), sans-fil DS jusqu'à 4. melonDS attend
  les paquets des autres consoles PENDANT l'image (25 ms au plus) : l'émulateur les lui livre aussitôt
  (`poll_receive`). Le sans-fil DS exige un ping très faible : il marche sur un même réseau, rarement
  par Internet.
- **Game Boy** : core **Gambatte** (`gambatte_libretro.dll`), 2 joueurs. Gambatte relie deux
  émulateurs par une connexion TCP directe, qui ne traverserait pas les box : sur chaque PC, un
  relais (`src/net/LinkTunnel.cpp`, dans son propre thread) se fait passer pour l'autre Gambatte et
  transporte les octets en UDP, de façon fiable (numérotation, accusés de réception, renvois).
  L'émulateur règle lui-même les options réseau de Gambatte (serveur 56400 chez l'hôte, client
  vers 127.0.0.1:56401 chez l'ami). Windows peut demander d'autoriser l'émulateur sur le réseau.

Test sur un PC : `--net-link` des deux côtés (avec `--net-play N`) ; le core de test imite les deux
câbles (`tools/testcore/linktest.cpp`) et écrit `LINKTEST ...` dans la console.

**Point de départ** : dans le salon, l'hôte choisit « Nouvelle partie » (personne ne touche à sa
sauvegarde), « Ma progression » (sa sauvegarde du jeu, `.srm`), « Sauvegarde auto » ou « Sauvegarde
F2 ». Sa sauvegarde est chargée chez lui, puis envoyée à tous dans l'état de départ ; à la fin, sa
progression est enregistrée chez lui seulement. Test : `--net-start 0..3` côté hôte.

Pendant une partie en ligne, le menu ne met pas le jeu en pause, et les sauvegardes d'état (F2/F4)
sont désactivées.

### Tester le jeu en ligne seul, sur un seul PC

```bash
powershell -ExecutionPolicy Bypass -File outils/deux-joueurs.ps1
```

Le script lance deux émulateurs : le joueur 1 joue au **clavier** (cliquer dans sa fenêtre), le
joueur 2 avec la **manette n°1**, même quand sa fenêtre est en arrière-plan. Le joueur 2 a son propre
dossier (`joueur2\`), un autre pseudo, une copie des cores et des BIOS, et partage la bibliothèque du
joueur 1 sans copier les jeux. Sans manette : ajouter `-SansManette` (le joueur 2 regarde seulement).
Options équivalentes à la main : `--clavier` et `--manette N`.

Le mot de passe ne circule jamais en clair, mais cette protection est simple : elle éloigne les
curieux, pas un attaquant déterminé. Les messages du salon ne sont pas chiffrés.

Test automatique sur un seul PC : `--net-host code.txt --net-password secret --net-expect 2 --net-game`
pour l'hôte, `--net-join code.txt --net-password secret` pour chaque invité. Ajouter `--net-play 600`
des deux côtés lance une vraie partie de 600 images avec des touches simulées (variable
`EMU_NET_DEBUG=1` pour voir chaque paquet). `--net-latency 100` simule 100 ms de ping (le rollback
devient visible). À la fin, l'hôte affiche le nombre de retours en arrière et de désynchronisations.

Pour vérifier le netcode lui-même, `tools/testcore/` contient un mini core libretro **parfaitement
déterministe** (`netplay_test_libretro.dll`, 4 carrés déplacés par la croix) : la moindre erreur de
touches se voit dans l'empreinte de son état. Résultats actuels : 0 désynchronisation à 0, 30, 100 et
200 ms de ping simulé.

## Politique de signature du code

Signature de code gratuite fournie par [SignPath.io](https://about.signpath.io), certificat de la
[SignPath Foundation](https://signpath.org).

- Les versions publiées sont compilées par GitHub Actions à partir de ce dépôt
  ([`.github/workflows/release.yml`](.github/workflows/release.yml)) à l'envoi d'un tag de version ;
  `NexoRetro.exe` et l'installateur sont signés pendant cette compilation. Rien de compilé ailleurs
  n'est signé.
- Auteurs et relecteurs : [AdamchDarkness](https://github.com/AdamchDarkness)
- Approbateurs : [AdamchDarkness](https://github.com/AdamchDarkness)

**Vie privée** : Nexo Retro ne collecte aucune donnée personnelle, sans statistiques ni télémétrie.
Il ne se connecte à d'autres systèmes que pour les fonctions que vous utilisez : GitHub (recherche
des mises à jour de Nexo Retro, désactivable dans Paramètres → Mises à jour), le buildbot libretro
(téléchargement et mise à jour des cores), les sites de jaquettes et d'informations (scraping, s'il
est activé), les amis avec qui vous jouez en ligne (connexion directe de pair à pair) et l'appli
Discord de votre PC (Rich Presence, si elle est activée).

## Licence

Nexo Retro est un logiciel libre : vous pouvez le redistribuer et le modifier selon les termes de la
**GNU General Public License version 3** (texte intégral dans [LICENSE](LICENSE)). Il est fourni
SANS AUCUNE GARANTIE. La GPLv3 a été choisie car de nombreux cores libretro sont eux-mêmes sous GPL.

Composants tiers : SDL3 (zlib), Dear ImGui (MIT), miniupnpc (BSD), Opus (BSD), en-têtes Vulkan
(Apache 2.0 / MIT), `libretro.h` (MIT), `stb_image.h` (domaine public), police Plus Jakarta Sans
(SIL OFL 1.1, `assets/fonts/OFL.txt`), `System.dat` de libretro-database. Les cores libretro,
téléchargés à part depuis le serveur de libretro, ont chacun leur propre licence.

**Avertissement** : Nexo Retro ne fournit aucun BIOS, firmware, clé ni jeu, et n'aide pas à s'en
procurer. Utilisez uniquement des copies de consoles et de jeux que vous possédez. Les noms de
consoles et de jeux cités appartiennent à leurs propriétaires respectifs ; ce projet n'est affilié à
aucun d'eux.
