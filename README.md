# Nexo Retro

**English** · [Français](README.fr.md)

> Free software released under the **GNU GPL v3** (see [LICENSE](LICENSE)).

**Nexo Retro** is a multi-system emulation frontend for Windows, built on [libretro](https://www.libretro.com/),
with a clean, minimalist interface: a game library with cover art, a page for every game, BIOS
files recognized by their content, one-click emulator installs, **serverless online play with
rollback** (GGPO-style), voice chat and link cable, rewind, fast forward, per-console control
remapping, self-updating cores and "Playing..." status on Discord. The interface is available in
English, French, Spanish, Portuguese (Brazil), German, Simplified Chinese, Japanese, Korean and
Arabic.

**Systems**: NES, Super Nintendo, Nintendo 64, Game Boy, Game Boy Color, Game Boy Advance, Nintendo
DS, Nintendo 3DS, GameCube, Wii, Mega Drive / Genesis, Sega CD, Saturn, Dreamcast, PlayStation, PS2,
PSP, Atari 2600 / 5200 / 7800 / Lynx / Jaguar, arcade (FBNeo, Capcom CPS1/2/3, Neo Geo; MAME as a
fallback), and through external emulators: Switch, Xbox, Xbox 360, PS3, PS4.

The project is also a C++ learning project (for someone who already knows C): the code is
commented, and comments tagged `[C++]` explain what differs from C. The code and its comments are
written in French.

> ⚖️ **No BIOS and no games are provided.** Only use dumps of consoles and games you own.

## Download

Windows 10 or 11, 64-bit. Every version is published on the
[**Releases**](https://github.com/AdamchDarkness/NexoRetro/releases) page, in two flavors:

| File | What it is |
| --- | --- |
| `NexoRetro-Setup-<version>.exe` | **The installer** (recommended). No administrator rights needed: the program goes to `%LOCALAPPDATA%\Programs\NexoRetro`, with a Start menu shortcut (and a desktop shortcut if you want one). |
| `NexoRetro-<version>-portable.zip` | **The portable version**: unzip it anywhere (a USB stick, for instance); nothing is installed. |
| `SHA256SUMS.txt` | SHA-256 checksums of both files, to verify a download. |

**Where is my data?** (cores, BIOS, games, saves, settings)

- **installed** copy: in `%APPDATA%\NexoRetro`. Updates and uninstalling never touch it: it is
  still there if you reinstall;
- **portable** copy (or one you compiled yourself): next to `NexoRetro.exe`.

An installed copy knows it is installed thanks to the `installed.txt` file the installer puts next
to the executable; a `portable.txt` file in the same folder forces portable mode. The
`--data <folder>` option picks another folder in every case.

**Updates**: Nexo Retro checks the Releases page for new versions by itself. An installed copy
updates in one click (the installer is downloaded, verified against `SHA256SUMS.txt`, then run
silently: it closes the program, replaces it without touching your data and starts it again).
Cores update on their own as well (see below).

**Uninstall**: Windows Settings → Apps → Nexo Retro. Your data stays in `%APPDATA%\NexoRetro`
(delete that folder yourself if you no longer want it).

## How it works

The emulators themselves are **libretro cores**: DLLs (`snes9x_libretro.dll`,
`pcsx_rearmed_libretro.dll`...) written by the community. This program loads them and gives them
video, audio, controllers, and the BIOS and save folders.

```
src/
  main.cpp, App.*        entry point and conductor (interface <-> game)
  Core.*                 loading the DLL and talking to the core
  Video.*, GlFunctions   window and OpenGL display (separate context for 3D cores)
  VulkanRender.*         Vulkan rendering for the cores that ask for it
  Audio.*, Input.*       sound, keyboard, controllers, mouse (Wiimote pointer)
  Remaps.*               per-console control remapping (buttons and sticks)
  Rewind.*               rewind (compressed differences between frames)
  Library.*              supported systems, game scanning, system detection
  UserData.*             history, play time, favorites, covers, chosen Wii controller
  Settings.*, Bios.*     settings; BIOS recognized by their checksum (System.dat database)
  ExternalInstaller.*    one-click installs: cores, external emulators, free system files
  ExternalSetup.*        system files for external emulators (PS3 firmware, keys, Xbox BIOS)
  CoreOptions.*, Cheats.*, SaveFiles.*, Lang.*   core options, cheats, saves, languages
  net/                   serverless online play: UDP, STUN, UPnP, room, rollback, link cable
  scrape/                covers and game info (libretro databases)
  Ui.*                   ImGui setup and notifications
  ui/                    THE DESIGN (see below)
assets/fonts/            Plus Jakarta Sans font (OFL license)
assets/lang/             translations (en.txt...)
assets/database/         libretro's System.dat: checksums of known BIOS files
third_party/             libretro.h, libretro_vulkan.h (MIT), stb_image.h (public domain)
tools/testcore/          tiny test cores (online play, link cable, Vulkan)
outils/                  creer-installateur.ps1 (release files), creer-paquet.ps1 (zip for a friend,
                         with your cores), deux-joueurs.ps1 (test online play alone)
installateur/            NexoRetro.iss: the installer (Inno Setup 6)
.github/workflows/       release.yml: automatic build and release on every v* tag
```

### The design (`src/ui/`)

Organized in layers, from the simplest to the most complete:

| File | Role |
| --- | --- |
| `Theme.*` | Design tokens: colors, fonts, scale. Change a color here and it changes everywhere. |
| `Draw.*` | Drawing primitives: soft shadows, text, dotted lines, vector icons. |
| `Widgets.*` | Clickable components: buttons, chips, toggles, selectors, fields. |
| `Covers.*` | Generated covers, or images chosen by the user. |
| `Screens.hpp`, `Common.cpp` | Navigation, shared context, date and size formatting. |
| `Shell.cpp` | The top bar and dispatching to the current screen. |
| `*Screen.cpp` | One file per screen: Home, Library, Game page, Add, BIOS, Friends, Settings, Big Picture. |
| `PauseMenu.*` | The menu shown on top of the game. |

## Building

Requirements:

- **Visual Studio 2026 Build Tools** with the "Desktop development with C++" workload
  (Visual Studio 2022 works too, see below)
- **CMake 3.24+**: `winget install Kitware.CMake`
- **Git**

```bash
cmake --preset windows
```

```bash
cmake --build --preset release
```

The first `cmake --preset windows` downloads and builds SDL3, Dear ImGui, miniupnpc, Opus and the
Vulkan headers. The executable is then in `build/Release/NexoRetro.exe`, next to its `assets/`
folder. With **Visual Studio 2022**, without the preset:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Making a release (installer and portable zip)

You need [Inno Setup 6](https://jrsoftware.org/isinfo.php)
(`winget install --id JRSoftware.InnoSetup -e --scope user`), then:

```bash
powershell -ExecutionPolicy Bypass -File outils/creer-installateur.ps1
```

The script builds in Release and writes to `dist/`: `NexoRetro-Setup-<version>.exe`,
`NexoRetro-<version>-portable.zip` and `SHA256SUMS.txt`. The version comes from
`project(NexoRetro VERSION x.y.z)` in `CMakeLists.txt`.

Publishing: bump the version in `CMakeLists.txt`, then push an annotated tag `v<version>`
(`git tag -a v0.3.0 -m "Release notes"` then `git push origin v0.3.0`). GitHub Actions
(`.github/workflows/release.yml`) builds, creates the three files and publishes the release, using
the tag message as release notes.

The installer accepts the usual Inno Setup options: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`
installs without any window (this is what the automatic update does) and starts Nexo Retro again at
the end; `/NOLAUNCH` prevents that restart (tests); `/DIR=<folder>` changes the install folder.

## Using it

Start Nexo Retro: the library opens. The `cores/`, `bios/`, `roms/` and `saves/` folders are
created in the data folder (`%APPDATA%\NexoRetro` for an installed copy, next to the executable
otherwise, or in the folder given with `--data <folder>`).
Each game has its own save folder, `saves/<game name>/`: its in-game save (`.srm`), its save states
and its screenshots. The game page has an "Open the save folder" button.
Nexo Retro opens no terminal window: its log is written to `NexoRetro.log` in the data folder
(handy when a game won't start). For debugging, `NexoRetro.exe --console` also shows it live.

1. Install the emulators: Settings → **Emulators (cores)** → "Install missing cores" (or one by
   one, or from a game's page). They come from libretro's official server.
2. Add your games from the **Add** screen (drag and drop, files or watched folders), or put them in
   `roms/<system>/`.
3. Drop your BIOS files on the **BIOS** screen (one file or a whole folder): they are recognized by
   their content and stored in the right place. GameCube/Wii and PSP have no BIOS, but use free
   files from Dolphin and PPSSPP, which can be installed from a game's page.
4. To play online: **Friends** screen → Create the room → send the code (`EMU-XXXX-XXXX-XXXX-XXXX`);
   your friend pastes it and joins. Same game file and same emulator version on both sides. The
   room shows everyone's ping and packet loss, and the connection quality before starting.

**Library**: a single row of controls (search, "Filters", view picker). Search is instant (title,
system, genre, publisher, year; several words, accent-insensitive). The categories (All,
Favorites, Recent, systems) fit on one line that scrolls with the mouse wheel. "Filters" opens a
window to sort and filter by genre, decade, number of players and publisher; active filters are
shown as chips that one click removes. Three views: cover grid, detailed list, carousel (arrow
keys, mouse wheel or controller; Enter to play).

**Big Picture mode** (TV button at the top, or Start on a controller; Settings → Display →
"Start in Big Picture"): dark full screen, large covers, a background taken from the game's
screenshot, the clock, and everything is driven by the controller:

| Button | Action |
| --- | --- |
| D-pad, left stick (arrow keys) | Previous / next game |
| LB / RB (Page Up / Page Down) | Category (All, Favorites, Recent, each system) |
| A (Enter) | Play |
| X (R) | Resume where you left off (automatic save) |
| Y (F) | Add to / remove from favorites |
| B, Start (Esc) | Exit Big Picture |

After a game, you come back to Big Picture, still in full screen.

**Voice chat** (Settings → Online play): open mic (only sends while you speak, adjustable
sensitivity) or push to talk (key of your choice, T by default), voice volume, mic test. In the
room: mute your mic, stop hearing a member, a green ring around whoever is speaking. In game: pause
menu → "Mute my mic" and "Show ping and packet loss".

**Up-to-date cores**: at startup (once a day), the emulator compares its cores with those on
libretro's server and downloads new versions, never the one of the game being played (Settings →
Emulators (cores) → "Automatic updates", "Check now").

**Discord**: "Playing Zelda · Super Nintendo", with the cover and play time, or "Online (2
players)". The emulator talks to the Discord app running on the PC (named pipe `discord-ipc-0`),
without any library to install. It needs the ID of a Discord application (created once at
discord.com/developers): `kDiscordAppId` in `src/Discord.hpp`, or Settings → Online play → Discord.

| Key (in game) | Action |
| --- | --- |
| Arrow keys | D-pad |
| X / C / S / D | B / A / Y / X |
| E / R | L / R |
| Enter / Right Shift | Start / Select |
| Esc, Select + Start | Pause menu |
| F2 / F4 | Save / load state |
| F8 | Reset |
| F12 | Screenshot (in the game's save folder) |
| Tab (held), Select + R | Fast forward (×2 to ×8, Settings → Controllers) |
| Backspace (held), Select + L | Rewind (not online, nor PS2/GameCube/Wii) |
| F11 | Full screen |

Pause menu → **Console controls**: every action of the emulated console (Wiimote "1" button,
"Shake", N64 C buttons...) and each of its sticks can be mapped to the button or stick of your
choice. Remembered per system, and for the Wii per controller type (chosen on the game page).
Online, everyone keeps their own settings without desynchronizing the game.

In the interface: F5 refreshes the library, F1 shows the ImGui demo (for learning).

For automated tests: `--screen library --frames 45 --screenshot capture.bmp` opens a screen, waits
45 frames, then quits and saves a screenshot.
Extra test screens: `covers` (the "Change cover" window of the first game), `infos` (Settings →
Covers and info), `pads` (Settings → Controllers), `delete` (the "Delete" window).
`--test-remove hide|trash` removes the first game from the library (or sends it to the Recycle Bin).
`--library-view 0|1|2` opens the library as a grid, list or carousel (without saving it).
`--screen filters` opens the filters window; `--big-picture` starts in Big Picture mode.
Test variables: `EMU_VOICE_TEST=tone` (an "A" note instead of the microphone), `EMU_VOICE_DUMP=file`
(the received voice, 48 kHz mono PCM), `EMU_TEST_CORE_UPDATE=1` (checks cores even in tests),
`EMU_DISCORD_TRACE=1` (everything Discord answers). During automated tests, the real microphone is
never opened.

## Roadmap

- [x] **Step 1**: load a core, display, sound, controllers, saves
- [x] **Step 2**: OpenGL hardware rendering (3D cores)
- [x] **Step 3**: complete interface (library, game page, BIOS, settings, adding games)
- [x] Remappable keys, every controller (including generic ones)
- [x] Game save visible and replaceable (the old one goes to the Recycle Bin); per-game cheat codes, with codes from the libretro database
- [x] Deleting a game: remove it from the library, or send its files to the Recycle Bin
  (including the `.bin` tracks of a `.cue`); removed games can be shown again from Settings → Folders
- [x] **Network, step 1**: private room with an invite code, serverless (STUN, UPnP, NAT hole punching, chat)
- [x] **Network, step 2**: full room (password, up to 8 players, game choice, file check by checksum)
- [x] **Network, step 3**: "delay-based lockstep" netplay (up to 4 players, state sync, desync detection)
- [x] **Rollback** (GGPO-style: my inputs apply immediately, my friend's are predicted then corrected)
- [x] Online game starting from the host's save (progress saved on the host's side at the end)
- [x] **Online link cable** (GBA with gpSP, Game Boy with Gambatte): each player has their own console and save
- [x] **Arcade and Neo Geo** (FBNeo, MAME as a fallback), online included
- [x] DS wireless (melonDS DS); "each their own console" chosen automatically for Game Boy, GBA and DS
- [x] **Multiple languages** (English by default, French): `assets/lang/<code>.txt` files
- [x] Dreamcast (Flycast), 3DS (Citra); Switch, Xbox, Xbox 360, PS3, PS4 through external emulators
- [x] Arcade sticks: "arcade stick" layout in one click, stick that drives the D-pad
- [x] Adjustable volume (Settings → Sound, and a slider in the in-game menu)
- [x] Core options (Settings → Core options, translated when the core is)
- [x] **Scraping**: covers, screenshots, genre, year, developer, publisher, players (libretro sources, no account)
- [ ] Game summaries in French (ScreenScraper, requires developer credentials)
- [x] **BIOS recognized by their content** (CRC32 checksum, libretro's `System.dat` database), even when misnamed
- [x] **One-click install** of external emulators (official pages), import of system files
- [x] **Vulkan rendering** (with device negotiation), as an alternative to OpenGL for cores that support both
- [x] Settings → **Emulators (cores)**: each core, installed or updated in one click (libretro's official server)
- [x] Separate GameCube and Wii (recognized from the disc content); Wii controller chosen per game; mouse aiming
- [x] **Console controls** (in-game menu): every action (Wiimote 1/2/Shake, N64 C-Up...) on the button of your choice
- [x] Separate Game Boy and Game Boy Color, Sega CD, Saturn, Atari 2600/5200/7800/Lynx/Jaguar; free Dolphin and PPSSPP files
- [x] **Short invite codes** (16 symbols); PCs on the same local network (router, Radmin, Hamachi) find each other on their own
- [x] **Ping and packet loss** for each player, in the room and (optionally) during the game; connection quality before starting
- [x] **Voice chat** (Opus): open mic or push to talk, mute yourself or a member
- [x] **Library**: filters (genre, year, players, publisher), accent-insensitive search, grid / list / carousel views
- [x] **Automatic core updates** (once a day, never the core of the game being played)
- [x] Interface in **Spanish, Portuguese (Brazil) and German**
- [x] Interface in **Simplified Chinese, Japanese, Korean and Arabic** (Windows fonts, joined right-to-left Arabic)
- [x] **Discord Rich Presence** (no SDK: Discord's named pipe)
- [x] Lighter library: filters in a window, categories on one line, active filters as chips
- [x] **Big Picture mode**: full screen, large covers, fully controller-driven
- [x] **Installer and portable version** published on GitHub Releases, in-app updates
- [ ] Copy-free Vulkan rendering (shared Vulkan/OpenGL image), for very high resolutions
- [ ] **Friends list** (next update): serverless, through BitTorrent's decentralized directory
  (DHT, like the Tox messenger). Each player has a key, and their "friend code" is their public key;
  they announce their current address there, encrypted for their friends, then the existing NAT
  hole punching takes over. A self-hosted (open source) server may be added as an option. In the
  meantime: the invite code.

## Languages

The interface is in **English by default**; Settings → Display → Language switches it to French,
Spanish, Portuguese (Brazil), German, Simplified Chinese, Japanese, Korean or Arabic.

Scripts that the interface font (Plus Jakarta Sans) lacks come from fonts already installed with
Windows, without adding anything to the package: Microsoft YaHei (Chinese), Yu Gothic (Japanese),
Malgun Gothic (Korean), Segoe UI (Arabic). Their glyphs are "merged" into the interface font; the
one for the chosen language comes first (Chinese and Japanese ideographs are drawn slightly
differently), and fonts are reloaded when the language changes.

Arabic needs two more steps, because ImGui writes letter by letter, from left to right
(`src/Arabic.cpp`): each letter takes its joined form (isolated, initial, medial, final, lam-alif
ligature), then the text is put back into display order, with numbers and Latin words staying
left-to-right. Paragraphs are wrapped into lines before being reversed.

The code is written in French, and every other language is an `assets/lang/<code>.txt` file:

```
# name: English
Jouer => Play
Il y a {n} min => {} min ago
Jeu choisi : {} => Game chosen: {}
```

`{}` stands for a variable part, `{n}` for a number only. Translation happens when the text is drawn
(`src/Lang.cpp`, called by `draw::text`, `draw::measure`...): no screen has to care about it, and
the layout adapts to the length of the words. **Adding a language**: copy `en.txt` (e.g. `es.txt`),
change the `# name:` line, translate the right-hand side of every line. To find forgotten strings:
run with the variable `EMU_LANG_MISSING=missing.txt`, which records the French strings displayed
without a translation.

## Recent consoles: external emulators

Switch, Xbox, Xbox 360, PS3 and PS4 have no libretro core. Their games show up in the library
(covers, play time...), but a **separate emulator** launches them:

| Console | Emulator | `roms/` folder | Files |
|---|---|---|---|
| Switch | Ryujinx | `switch` | `.nsp`, `.xci` |
| Xbox | xemu | `xbox` | `.iso` (xiso) |
| Xbox 360 | Xenia | `x360` | `.iso`, `.xex` |
| PS3 | RPCS3 | `ps3` | `.iso`, or the game folder (`EBOOT.BIN`) |
| PS4 | shadPS4 | `ps4` | the game folder (`eboot.bin`) |

Settings → External emulators:

- **Install** (PS3, PS4, Xbox, Xbox 360) downloads the latest version of the emulator from its
  **official** GitHub page (`src/ExternalInstaller.cpp`), extracts it to `emulators/<console>/`
  with Windows' `tar.exe` (which also reads `.7z`), sets it to portable mode and remembers its
  path. **Update** does the same again.
- **Choose…** points to an emulator that is already installed (required for the Switch: see below).
- **System files** are **never downloaded on the user's behalf** (`src/ExternalSetup.cpp`): the
  *Import…* button stores them and configures the emulator.

| Console | System files | What *Import…* does |
|---|---|---|
| PS3 | `PS3UPDAT.PUP` (free on Sony's website) | runs `rpcs3 --installfw` |
| Switch | `prod.keys`, `title.keys` (from **your** console) | copies them where the emulator looks for them |
| Xbox | MCPX (512 bytes), flash BIOS, `.qcow2` hard disk | stores them in `bios/xbox/` and writes `xemu.toml` |
| PS4, Xbox 360 | nothing required | — |

Why there is no automatic install for the Switch: its emulators (Yuzu, Ryujinx) were shut down in
2024 after legal action by Nintendo; what remains are copies taken over by other teams. And its
keys are used to break the games' encryption: providing them would be illegal.

The emulator is started from its own folder, with its arguments (`{rom}` = the game); play time is
counted until it closes. No online play or cheat codes for these consoles.

## BIOS recognized by their content

`assets/database/System.dat` (from libretro) gives the size and checksum (CRC32) of 500 BIOS files.
A file dropped on the BIOS screen, or found while searching a folder (*Search a folder*), is
recognized **by its content**, even when misnamed, then copied under **every** name the cores look
for (`dmg_boot.bin` *and* `gb_bios.bin`...). The screen flags with a "!" a file that is present but
unknown to the database (damaged, modified) or that is actually another BIOS. PS2 BIOS files show
their version and region ("PS2 v2.30 · Europe").

## Vulkan rendering

Cores that draw with Vulkan (ParaLLEl-RDP for the N64, Beetle PSX HW, PPSSPP, Dolphin...) are
supported (`src/VulkanRender.cpp`): we create the Vulkan device (or let the core create it, through
the v1 and v2 "negotiation" interface), the core draws into its own image, and we copy it into the
OpenGL texture the interface is drawn on. `vulkan-1.dll` (installed with the graphics driver) is
loaded at run time: without it, only the Vulkan cores are unavailable.
Settings → Display → *Vulkan rendering*: for cores that can do both, asks for Vulkan rather than
OpenGL. The tiny `vulkan_test_libretro.dll` core (`tools/testcore/vulkantest.cpp`) tests the whole
chain without a game: `NexoRetro.exe vulkan_test_libretro.dll` shows a pulsing red background that
turns green when you press A.

## Arcade and Neo Geo

- **Core**: `fbneo_libretro.dll` (FinalBurn Neo, the one used by Fightcade: the best for online
  play). Otherwise, the emulator uses `mame2003_plus_libretro.dll` or `mame_libretro.dll` if they
  are in `cores/`. The game page shows which core is used.
- **Games**: one game = one `.zip` with a fixed name (`sf2.zip`, `kof98.zip`), from a romset that
  matches the core (FBNeo romset for FBNeo). Put them in `roms/arcade/` or `roms/neogeo/`, or in a
  watched folder whose name contains "arcade", "mame", "fbneo" or "neogeo".
- **Names**: the scraper finds the real title from the file name (`kof98.zip` → "The King of
  Fighters '98"), with the cover, year and publisher.
- **BIOS**: `neogeo.zip` goes in `bios/fbneo/`. If it is stored with the games, the emulator copies
  it on its own. Arcade BIOS and chips (`neogeo.zip`, `qsound.zip`, `pgm.zip`...) are not shown as
  games.
- **Online**: like the other systems, with rollback. The emulator tells the core that its save
  states are used for online play (`GET_SAVESTATE_CONTEXT`) and lets it skip video and audio during
  re-simulation (`GET_AUDIO_VIDEO_ENABLE`). All players must have the same core and the same file
  (checked in the room).

### Core options

Each core announces its options and their default values; the emulator gives them back when the
core asks for them (like RetroArch), and tells it which language we speak: translated cores then
give their options in that language. **Settings → Core options** shows them all, core by core;
whatever differs from the default is stored in `core-options.txt` (next to `userdata.txt`, editable
by hand):

```
mame_softlists_enable=enabled
```

Some cores (FBNeo) only announce their options once a game is loaded: the emulator remembers the
ones seen during the last launch (`core-options/` folder). Online, different options between
players prevent the game from starting (the games would diverge).

When a game does not start, the emulator shows the reason given by the core: for instance "files
missing from the zip: the romset doesn't match the emulator version" (MAME and FBNeo require a
romset of their exact version).

## Covers and game info (scraping)

As soon as a game enters the library, the emulator looks in the background for its cover, title
screen, an in-game screenshot, its genre, year, developer, publisher and number of players. No
account is needed; the sources are open:

- **libretro-database**: one `.rdb` file per system (MessagePack format), downloaded once then kept
  for 30 days;
- **thumbnails.libretro.com**: the images, stored by official name (No-Intro).

Recognizing the game:

1. for cartridges (NES, SNES, N64, GB, GBA, DS, Mega Drive), the emulator computes the file's
   **CRC32** (removing the headers added by some formats, and putting N64 ROMs back into standard
   byte order): if it is in the database, it is necessarily the right game;
2. otherwise, it compares names while ignoring punctuation, "The", parenthesized notes and how
   numbers are written (II = 2), then the subtitle alone ("Zelda III - A Link to the Past" finds
   "Legend of Zelda, The - A Link to the Past"). Among several versions, it prefers the file's
   region, then France and Europe, never a demo or a prototype.

Everything is kept in `scraper/`: `games.txt` (what was found, readable in Notepad), `db/`,
`lists/` and `images/`. On a game's page, the ↻ button searches again, and "Change cover → Found
online" offers covers from every region, with a free-text search. The library search also finds
genres, publishers and years ("fighting", "Capcom", "1995"). Settings in Settings → Covers and info.

Code: `src/net/Http.cpp` (HTTPS downloads with WinHTTP), `src/scrape/GameDatabase.cpp` (reading
`.rdb` files and comparing names), `src/scrape/Scraper.cpp` (the thread and the cache).

## Online play: how it works

There is **no server and no database**: the host's emulator is the room.

1. The host creates a room: the emulator discovers its public address (STUN, a free public service)
   and asks the router to open a port (UPnP).
2. The host shares the `EMU-XXXX-XXXX-XXXX-XXXX` code (Discord, WhatsApp...): 16 symbols that
   contain the room number and its public address. The host preferably listens on ports 47800 to
   47809.
3. The friend pastes the code: their emulator tries the address in the code and, at the same time,
   calls its whole local network on those ports ("broadcast"): a host on the same router, on Radmin
   or on Hamachi answers without knowing its address. If the host's router blocks it, the friend
   gets a **reply code** to send back: the host pastes it and both routers open towards each other
   (NAT hole punching).
4. As a last resort, Radmin VPN, Hamachi or Tailscale work directly. Tailscale does not forward
   broadcasts: the host then gives the **full code** (link under the code), which contains all of
   its addresses.

Throughout the room, each link sends 4 "pings" per second: their round-trip time gives the ping,
and those left unanswered over the last 5 seconds give the **packet loss**. Voice (voice chat)
goes through the same port: 20 ms of microphone audio compressed by Opus (~60 bytes), with no
delivery guarantee; the host relays everyone's voice to the others, and a lost chunk is rebuilt
from the backup copy (FEC) slipped into the next one.

In the room, the host picks a game: each member checks that they have **exactly the same file**
using a 64-bit checksum (computed in the background, kept in `hashes.txt`), and shows up as
"Ready", "Different version" or "Game not found".

When everyone is "Ready", the host starts the game:

1. everyone starts the game (without loading their personal save, which is not overwritten either);
2. the host sends a **complete snapshot of its emulator** (a save state): everyone starts from
   exactly the same point;
3. all emulators compute the same game with the same inputs. Two modes, chosen automatically by
   the host:
   - **rollback** (default): we do not wait for the friend's inputs, we **predict** them (they are
     probably pressing the same thing as before). Every frame is snapshotted; when their real
     inputs arrive and differ, we go back to that frame's snapshot and re-simulate up to now,
     without sound, in a fraction of a second. My inputs apply with only 1 or 2 frames of delay.
     If a player gets ahead, they slow down a little to let the other catch up;
   - **delay**: we wait for everyone's inputs; mine apply a few frames later (depending on the
     ping). Used when rollback would be too costly or incorrect: a state larger than 8 MB (3D
     cores), or a core whose save states are not reliable (reported by the core, or a known list:
     2048, Mr.Boom). Variable `EMU_NET_FORCE_DELAY=1` to force it.

   Each packet repeats the inputs not yet confirmed: a lost packet is made up for by the next one;
4. every 2 seconds, everyone's state checksum is compared with the host's to detect a desync.

### Link cable (GBA, Game Boy)

On a **handheld** (Game Boy, GBA, DS), each player had THEIR OWN console, connected to the others
by cable or wirelessly: when the host picks such a game, the room switches **on its own** to "Each
their own console" (the host can go back to "Same console", to play a single-player game together).
Home consoles (SNES, N64, PS1...) stay on a shared console, where the same file is required. Each
player then plays **their own game with their own save** (Ruby on one side, Sapphire on the
other); only the cable data travels. Each player picks their game ("Choose my game"; automatic if
it has the same name as the host's, or if they only have one game for that system). If someone
leaves, the cable is unplugged, but everyone keeps playing.

- **GBA**: **gpSP** core (`gpsp_libretro.dll`) on each player's side, up to 4. It emulates the
  3rd-generation Pokémon cable, the wireless adapter (FireRed / LeafGreen / Emerald) and Advance
  Wars (automatic detection, "Link Cable Connectivity" option). Its packets go through the libretro
  *netpacket* interface and the room's reliable messages (`src/net/LinkPlay.cpp`).
- **DS**: **melonDS DS** core (`melondsds_libretro.dll`), DS wireless with up to 4 players. melonDS
  waits for packets from the other consoles DURING the frame (25 ms at most): the emulator delivers
  them right away (`poll_receive`). DS wireless requires a very low ping: it works on the same
  network, rarely over the Internet.
- **Game Boy**: **Gambatte** core (`gambatte_libretro.dll`), 2 players. Gambatte connects two
  emulators through a direct TCP connection, which would not get through routers: on each PC, a
  relay (`src/net/LinkTunnel.cpp`, in its own thread) pretends to be the other Gambatte and
  carries the bytes over UDP, reliably (numbering, acknowledgments, resends). The emulator sets
  Gambatte's network options itself (server 56400 on the host, client to 127.0.0.1:56401 on the
  friend's side). Windows may ask to allow the emulator on the network.

Test on a single PC: `--net-link` on both sides (with `--net-play N`); the test core mimics both
cables (`tools/testcore/linktest.cpp`) and writes `LINKTEST ...` to the console.

**Starting point**: in the room, the host picks "New game" (nobody's save is touched), "My
progress" (their in-game save, `.srm`), "Auto save" or "F2 save". The save is loaded on the host's
side, then sent to everyone in the starting state; at the end, progress is saved on the host's side
only. Test: `--net-start 0..3` on the host's side.

During an online game, the menu does not pause the game, and save states (F2/F4) are disabled.

### Testing online play alone, on a single PC

```bash
powershell -ExecutionPolicy Bypass -File outils/deux-joueurs.ps1
```

The script starts two emulators: player 1 uses the **keyboard** (click in their window), player 2
uses **controller #1**, even when their window is in the background. Player 2 has their own folder
(`joueur2\`), another nickname, a copy of the cores and BIOS, and shares player 1's library without
copying the games. Without a controller: add `-SansManette` (player 2 only watches). Equivalent
manual options: `--clavier` and `--manette N`.

The password never travels in clear text, but this protection is simple: it keeps the curious
away, not a determined attacker. Room messages are not encrypted.

Automated test on a single PC: `--net-host code.txt --net-password secret --net-expect 2 --net-game`
for the host, `--net-join code.txt --net-password secret` for each guest. Adding `--net-play 600` on
both sides runs a real 600-frame game with simulated inputs (variable `EMU_NET_DEBUG=1` to see every
packet). `--net-latency 100` simulates a 100 ms ping (rollback becomes visible). At the end, the
host shows the number of rollbacks and desyncs.

To check the netcode itself, `tools/testcore/` contains a **perfectly deterministic** tiny libretro
core (`netplay_test_libretro.dll`, 4 squares moved by the D-pad): the slightest input error shows
up in its state checksum. Current results: 0 desyncs at 0, 30, 100 and 200 ms of simulated ping.

## License

Nexo Retro is free software: you can redistribute it and/or modify it under the terms of the
**GNU General Public License version 3** (full text in [LICENSE](LICENSE)). It comes WITHOUT ANY
WARRANTY. GPLv3 was chosen because many libretro cores are themselves GPL-licensed.

Third-party components: SDL3 (zlib), Dear ImGui (MIT), miniupnpc (BSD), Opus (BSD), Vulkan headers
(Apache 2.0 / MIT), `libretro.h` (MIT), `stb_image.h` (public domain), Plus Jakarta Sans font
(SIL OFL 1.1, `assets/fonts/OFL.txt`), `System.dat` from libretro-database. libretro cores,
downloaded separately from libretro's server, each have their own license.

**Disclaimer**: Nexo Retro does not provide any BIOS, firmware, keys or games, and does not help
obtain them. Only use copies of consoles and games you own. Console and game names mentioned here
belong to their respective owners; this project is not affiliated with any of them.
