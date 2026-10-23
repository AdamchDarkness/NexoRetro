#include "Settings.hpp"

#include <algorithm>
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
        else if (key.rfind("external.", 0) == 0) externalPaths[key.substr(9)] = value;
        else if (key.rfind("externalArgs.", 0) == 0) externalArgs[key.substr(13)] = value;
    }
}

void Settings::save(const std::filesystem::path& file) const
{
    std::ofstream out(file, std::ios::trunc);
    out << "language=" << language << '\n'
        << "fullscreenOnLaunch=" << fullscreenOnLaunch << '\n'
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
        << "nickname=" << nickname << '\n';
    for (const auto& [id, path] : externalPaths)
        out << "external." << id << '=' << path << '\n';
    for (const auto& [id, args] : externalArgs)
        out << "externalArgs." << id << '=' << args << '\n';
}

double Settings::maxAudioQueue() const
{
    switch (audioLatency) {
    case 0: return 0.08;
    case 2: return 0.35;
    default: return 0.2;
    }
}
