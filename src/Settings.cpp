#include "Settings.hpp"

#include "Hash.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

void Settings::load(const std::filesystem::path& file)
{
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        const int number = std::atoi(value.c_str());
        if (key == "fullscreenOnLaunch") fullscreenOnLaunch = number != 0;
        else if (key == "bigPictureOnStart") bigPictureOnStart = number != 0;
        else if (key == "language" && !value.empty()) language = value;
        else if (key == "scaling") scaling = number;
        else if (key == "aspect") aspect = number;
        else if (key == "showFps") showFps = number != 0;
        else if (key == "preferVulkan") preferVulkan = number != 0;
        else if (key == "audioLatency") audioLatency = number;
        else if (key == "volume") volume = std::clamp(number, 0, 100);
        else if (key == "menuButton") menuButton = number;
        else if (key == "keyboardSeparate") keyboardSeparate = number != 0;
        else if (key == "wiiMousePointer") wiiMousePointer = number != 0;
        else if (key == "rewind") rewind = number != 0;
        else if (key == "fastForwardSpeed") fastForwardSpeed = number;
        else if (key == "autoSaveState") autoSaveState = number != 0;
        else if (key == "autoScrape") autoScrape = number != 0;
        else if (key == "nickname" && !value.empty()) nickname = value;
        else if (key == "libraryView") libraryView = std::clamp(number, 0, 5);
        else if (key == "uiZoom") uiZoom = std::clamp(number, 80, 150);
        else if (key == "firstRunDone") firstRunDone = number != 0;
        else if (key == "speedrunFolder") speedrunFolder = value;
        else if (key == "speedrunRecord") speedrunRecord = number != 0;
        else if (key == "speedrunKeep") speedrunKeep = std::clamp(number, 0, 2);
        else if (key == "speedrunResetGame") speedrunResetGame = number != 0;
        else if (key == "speedrunPadSplit") speedrunPadSplit = std::clamp(number, -1, 25);
        else if (key == "speedrunPadReset") speedrunPadReset = std::clamp(number, -1, 25);
        else if (key == "speedrunVideoOverlay") speedrunVideoOverlay = number != 0;
        else if (key == "inputDisplay") inputDisplay = number != 0;
        else if (key == "parentalOn") parentalOn = number != 0;
        else if (key == "parentalMinutes") parentalMinutes = std::clamp(number, 5, 24 * 60);
        else if (key == "parentalPin") parentalPin = value;
        else if (key == "parentalDay") parentalDay = number;
        else if (key == "parentalUsed") parentalUsed = std::max(0, number);
        else if (key == "parentalBonus") parentalBonus = std::max(0, number);
        else if (key == "netHeavyRollback") netHeavyRollback = number != 0;
        else if (key == "theme" && !value.empty()) theme = value;
        else if (key == "accentColor") accentColor = value;
        else if (key == "background") background = std::clamp(number, 0, 3);
        else if (key == "groundColor") groundColor = value;
        else if (key == "gradientFrom" && !value.empty()) gradientFrom = value;
        else if (key == "gradientTo" && !value.empty()) gradientTo = value;
        else if (key == "gradientAngle") gradientAngle = ((number % 360) + 360) % 360;
        else if (key == "backgroundImage") backgroundImage = value;
        else if (key == "backgroundDim") backgroundDim = std::clamp(number, 0, 90);
        else if (key == "uiFont") uiFont = value;
        else if (key == "uiSounds") uiSounds = number != 0;
        else if (key == "uiSoundVolume") uiSoundVolume = std::clamp(number, 0, 100);
        else if (key == "uiSoundPack") uiSoundPack = std::clamp(number, 0, 3);
        else if (key == "coreAutoUpdate") coreAutoUpdate = number != 0;
        else if (key == "appAutoCheck") appAutoCheck = number != 0;
        else if (key == "appAutoInstall") appAutoInstall = number != 0;
        else if (key == "appLastCheck") appLastCheck = std::atoll(value.c_str()); // (trop grand pour atoi)
        else if (key == "appIgnoredVersion") appIgnoredVersion = value;
        else if (key == "netStats") netStats = number != 0;
        else if (key == "voiceMode") voiceMode = std::clamp(number, 0, 2);
        else if (key == "voiceKey" && number > 0) voiceKey = number;
        else if (key == "voiceVolume") voiceVolume = std::clamp(number, 0, 200);
        else if (key == "voiceSensitivity") voiceSensitivity = std::clamp(number, 0, 100);
        else if (key == "discordPresence") discordPresence = number != 0;
        else if (key == "discordAppId") discordAppId = value;
        else if (key.rfind("external.", 0) == 0) externalPaths[key.substr(9)] = value;
        else if (key.rfind("externalArgs.", 0) == 0) externalArgs[key.substr(13)] = value;
    }
}

void Settings::save(const std::filesystem::path& file) const
{
    std::ofstream out(file, std::ios::trunc);
    out << "language=" << language << '\n'
        << "fullscreenOnLaunch=" << fullscreenOnLaunch << '\n'
        << "bigPictureOnStart=" << bigPictureOnStart << '\n'
        << "scaling=" << scaling << '\n'
        << "aspect=" << aspect << '\n'
        << "showFps=" << showFps << '\n'
        << "preferVulkan=" << preferVulkan << '\n'
        << "audioLatency=" << audioLatency << '\n'
        << "volume=" << volume << '\n'
        << "menuButton=" << menuButton << '\n'
        << "keyboardSeparate=" << keyboardSeparate << '\n'
        << "wiiMousePointer=" << wiiMousePointer << '\n'
        << "rewind=" << rewind << '\n'
        << "fastForwardSpeed=" << fastForwardSpeed << '\n'
        << "autoSaveState=" << autoSaveState << '\n'
        << "autoScrape=" << autoScrape << '\n'
        << "nickname=" << nickname << '\n'
        << "libraryView=" << libraryView << '\n'
        << "uiZoom=" << uiZoom << '\n'
        << "firstRunDone=" << firstRunDone << '\n'
        << "speedrunFolder=" << speedrunFolder << '\n'
        << "speedrunRecord=" << speedrunRecord << '\n'
        << "speedrunKeep=" << speedrunKeep << '\n'
        << "speedrunResetGame=" << speedrunResetGame << '\n'
        << "speedrunPadSplit=" << speedrunPadSplit << '\n'
        << "speedrunPadReset=" << speedrunPadReset << '\n'
        << "speedrunVideoOverlay=" << speedrunVideoOverlay << '\n'
        << "inputDisplay=" << inputDisplay << '\n'
        << "parentalOn=" << parentalOn << '\n'
        << "parentalMinutes=" << parentalMinutes << '\n'
        << "parentalPin=" << parentalPin << '\n'
        << "parentalDay=" << parentalDay << '\n'
        << "parentalUsed=" << parentalUsed << '\n'
        << "parentalBonus=" << parentalBonus << '\n'
        << "netHeavyRollback=" << netHeavyRollback << '\n'
        << "theme=" << theme << '\n'
        << "accentColor=" << accentColor << '\n'
        << "background=" << background << '\n'
        << "groundColor=" << groundColor << '\n'
        << "gradientFrom=" << gradientFrom << '\n'
        << "gradientTo=" << gradientTo << '\n'
        << "gradientAngle=" << gradientAngle << '\n'
        << "backgroundImage=" << backgroundImage << '\n'
        << "backgroundDim=" << backgroundDim << '\n'
        << "uiFont=" << uiFont << '\n'
        << "uiSounds=" << uiSounds << '\n'
        << "uiSoundVolume=" << uiSoundVolume << '\n'
        << "uiSoundPack=" << uiSoundPack << '\n'
        << "coreAutoUpdate=" << coreAutoUpdate << '\n'
        << "appAutoCheck=" << appAutoCheck << '\n'
        << "appAutoInstall=" << appAutoInstall << '\n'
        << "appLastCheck=" << appLastCheck << '\n'
        << "appIgnoredVersion=" << appIgnoredVersion << '\n'
        << "netStats=" << netStats << '\n'
        << "voiceMode=" << voiceMode << '\n'
        << "voiceKey=" << voiceKey << '\n'
        << "voiceVolume=" << voiceVolume << '\n'
        << "voiceSensitivity=" << voiceSensitivity << '\n'
        << "discordPresence=" << discordPresence << '\n'
        << "discordAppId=" << discordAppId << '\n';
    for (const auto& [id, path] : externalPaths)
        out << "external." << id << '=' << path << '\n';
    for (const auto& [id, args] : externalArgs)
        out << "externalArgs." << id << '=' << args << '\n';
}

void Settings::rollParentalDay()
{
    const std::time_t now = std::time(nullptr);
    std::tm day{};
    localtime_s(&day, &now);
    const int today = (day.tm_year + 1900) * 10000 + (day.tm_mon + 1) * 100 + day.tm_mday;
    if (parentalDay != today) {
        parentalDay = today;
        parentalUsed = 0;
        parentalBonus = 0;
    }
}

int Settings::parentalSecondsLeft() const
{
    if (!parentalOn)
        return 1 << 30;
    return std::max(0, (parentalMinutes + parentalBonus) * 60 - parentalUsed);
}

std::string Settings::pinHash(const std::string& pin)
{
    // (Pas besoin d'une protection de banque : il s'agit qu'un enfant ne
    // retrouve pas le code en ouvrant settings.ini au Bloc-notes.)
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx",
                  static_cast<unsigned long long>(Hasher64::of(pin, 0x4E65786F526574ull))); // "NexoRet"
    return text;
}

double Settings::maxAudioQueue() const
{
    switch (audioLatency) {
    case 0: return 0.05;
    case 2: return 0.15;
    default: return 0.08;
    }
}
