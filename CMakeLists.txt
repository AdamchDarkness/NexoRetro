cmake_minimum_required(VERSION 3.24)
project(NexoRetro VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Le "runtime" C/C++ de Microsoft (vcruntime140.dll...) est inclus DANS
# l'exécutable : il marche sur un PC qui n'a pas installé le "Visual C++
# Redistributable" (celui d'un ami, par exemple). Valable aussi pour SDL et
# les autres bibliothèques compilées ici (déclaré avant elles).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- SDL3 : fenêtre, rendu, son et manettes --------------------------------
# Téléchargé et compilé automatiquement au premier "cmake", puis lié en
# statique : l'exécutable final n'a besoin d'aucune DLL à côté de lui.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    # Archive officielle de la release (elle contient VERSION.txt : sans lui,
    # SDL interroge git et se trompe de dépôt).
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.28/SDL3-3.2.28.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

# OpenGL : fourni par le système (opengl32.lib sous Windows).
find_package(OpenGL REQUIRED)

# --- Vulkan : seulement les en-têtes officiels (Khronos) --------------------
# Pas de bibliothèque à lier : vulkan-1.dll, installée avec le pilote de la
# carte graphique, est chargée à l'exécution (voir src/VulkanRender.cpp).
# Sans elle, les cores Vulkan ne marchent pas, mais tout le reste si.
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE) # pas de module C++20 : les en-têtes suffisent
FetchContent_Declare(
    vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.290.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(vulkan_headers)

# --- Dear ImGui : l'interface graphique -------------------------------------
# ImGui n'a pas de CMakeLists.txt : on compile nous-mêmes ses fichiers, avec
# les "backends" qui le branchent sur SDL3 (entrées) et OpenGL 3 (dessin).
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static OpenGL::GL)

# --- miniupnpc : demander à la box d'ouvrir un port (UPnP) ------------------
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    miniupnpc
    URL https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_3_3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR miniupnpc
)
FetchContent_MakeAvailable(miniupnpc)

# --- Opus : la compression de la voix (chat vocal) --------------------------
# Le codec libre de référence pour la voix sur Internet (Discord, WebRTC) :
# 20 ms de voix tiennent dans environ 60 octets.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    opus
    URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(opus)

# --- L'exécutable ----------------------------------------------------------
add_executable(emulateur
    # Le moteur : chargement des cores, image, son, manettes.
    src/main.cpp
    src/app.rc
    src/App.cpp
    src/Core.cpp
    src/CoreOptions.cpp
    src/Cheats.cpp
    src/SaveFiles.cpp
    src/Lang.cpp
    src/Arabic.cpp
    src/Remaps.cpp
    src/Rewind.cpp
    src/ExternalProcess.cpp
    src/ExternalInstaller.cpp
    src/ExternalSetup.cpp
    src/Video.cpp
    src/VulkanRender.cpp
    src/Audio.cpp
    src/VoiceChat.cpp
    src/Discord.cpp
    src/CoreUpdater.cpp
    src/Input.cpp
    src/UiSounds.cpp
    # Les données : jeux, BIOS, réglages, historique.
    src/Library.cpp
    src/UserData.cpp
    src/Settings.cpp
    src/Bios.cpp
    src/FileDialogs.cpp
    src/Hash.cpp
    src/FileHasher.cpp
    src/Trash.cpp
    # Le jeu en ligne (sans serveur central).
    src/net/Socket.cpp
    src/net/Stun.cpp
    src/net/Upnp.cpp
    src/net/InviteCode.cpp
    src/net/Session.cpp
    src/net/Netplay.cpp
    src/net/LinkPlay.cpp
    src/net/LinkTunnel.cpp
    src/net/Http.cpp
    # Le scraping : jaquettes et informations des jeux (sources libretro).
    src/scrape/GameDatabase.cpp
    src/scrape/Scraper.cpp
    # L'interface (le design est dans src/ui/).
    src/Ui.cpp
    src/ui/Theme.cpp
    src/ui/Themes.cpp
    src/ui/Draw.cpp
    src/ui/Widgets.cpp
    src/ui/Covers.cpp
    src/ui/Common.cpp
    src/ui/Shell.cpp
    src/ui/HomeScreen.cpp
    src/ui/LibraryScreen.cpp
    src/ui/GameScreen.cpp
    src/ui/AddGamesScreen.cpp
    src/ui/BiosScreen.cpp
    src/ui/FriendsScreen.cpp
    src/ui/SettingsScreen.cpp
    src/ui/PauseMenu.cpp
    src/ui/BigPictureScreen.cpp
)
# Le fichier produit s'appelle NexoRetro.exe (la cible garde son nom court).
set_target_properties(emulateur PROPERTIES OUTPUT_NAME NexoRetro)
target_include_directories(emulateur PRIVATE third_party/libretro third_party/stb)
target_link_libraries(emulateur PRIVATE SDL3::SDL3-static OpenGL::GL imgui miniupnpc::miniupnpc Vulkan::Headers opus)
if(WIN32)
    # ws2_32 : les sockets de Windows ; iphlpapi : la liste des cartes réseau ;
    # winhttp : les téléchargements HTTPS (scraping).
    target_link_libraries(emulateur PRIVATE ws2_32 iphlpapi winhttp)
endif()

# --- Mini-core de test du jeu en ligne (voir tools/testcore/) ---------------
add_library(netplay_test_libretro SHARED tools/testcore/testcore.cpp tools/testcore/linktest.cpp)
target_include_directories(netplay_test_libretro PRIVATE third_party/libretro)
target_link_libraries(netplay_test_libretro PRIVATE ws2_32)
set_target_properties(netplay_test_libretro PROPERTIES PREFIX "" CXX_STANDARD 20)

# --- Mini-core de test du rendu Vulkan (voir tools/testcore/vulkantest.cpp) ---
add_library(vulkan_test_libretro SHARED tools/testcore/vulkantest.cpp)
target_include_directories(vulkan_test_libretro PRIVATE third_party/libretro)
target_link_libraries(vulkan_test_libretro PRIVATE Vulkan::Headers)
set_target_properties(vulkan_test_libretro PROPERTIES PREFIX "" CXX_STANDARD 20)
if(MSVC)
    target_compile_options(netplay_test_libretro PRIVATE /utf-8 /W4)
endif()

# Les polices (assets/) sont copiées à côté de l'exécutable après chaque compilation.
add_custom_command(TARGET emulateur POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:emulateur>/assets
)

if(MSVC)
    # /utf-8 : les sources contiennent des accents.
    # /W4 : beaucoup d'avertissements, utile pour apprendre.
    target_compile_options(emulateur PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(emulateur PRIVATE -Wall -Wextra)
endif()
