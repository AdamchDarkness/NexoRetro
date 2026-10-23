#include "UiSounds.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::GetHoveredID

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sounds {

namespace {

constexpr int kRate = 48000; // échantillons par seconde
constexpr float kPi = 3.14159265f;
constexpr int kEventCount = 5;

enum class Wave { Sine, Square, Pulse, Saw };

// Un son = une suite d'échantillons (nombres de -1 à 1), en mono.
using Sample = std::vector<float>;

// Ajoute une note au son, à partir de "startMs" : sa hauteur glisse de
// fromHz à toHz, et son volume suit une "enveloppe" (montée très courte,
// puis descente exponentielle, comme une corde pincée).
void note(Sample& out, float startMs, float durationMs, float fromHz, float toHz, Wave wave, float gain)
{
    const int start = static_cast<int>(startMs * kRate / 1000);
    const int count = static_cast<int>(durationMs * kRate / 1000);
    if (static_cast<int>(out.size()) < start + count)
        out.resize(static_cast<size_t>(start + count), 0.0f);
    float phase = 0.0f; // position dans la période, de 0 à 1
    const int attack = std::max(1, kRate / 1000 * 3); // 3 ms : évite le "clic" du début
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float hz = fromHz + (toHz - fromHz) * t;
        phase += hz / kRate;
        phase -= std::floor(phase);
        float v = 0.0f;
        switch (wave) {
        case Wave::Sine: v = std::sin(phase * 2 * kPi); break;
        case Wave::Square: v = phase < 0.5f ? 1.0f : -1.0f; break;
        case Wave::Pulse: v = phase < 0.25f ? 1.0f : -1.0f; break; // carré "étroit", plus nasillard
        case Wave::Saw: v = 2.0f * phase - 1.0f; break;
        }
        const float envelope = std::min(1.0f, static_cast<float>(i) / attack) * std::exp(-4.0f * t) * (1.0f - t * t);
        out[static_cast<size_t>(start + i)] += v * envelope * gain;
    }
}

// Les sons de chaque pack, dans l'ordre de Event.
using PackSounds = std::array<Sample, kEventCount>;

PackSounds makeSoft()
{
    PackSounds s;
    note(s[0], 0, 45, 880, 860, Wave::Sine, 0.45f);                 // clic
    note(s[1], 0, 22, 1320, 1320, Wave::Sine, 0.12f);               // survol
    note(s[2], 0, 70, 660, 660, Wave::Sine, 0.4f);                  // validation : deux notes qui montent
    note(s[2], 55, 120, 990, 990, Wave::Sine, 0.4f);
    note(s[3], 0, 70, 660, 660, Wave::Sine, 0.38f);                 // retour : deux notes qui descendent
    note(s[3], 55, 110, 440, 440, Wave::Sine, 0.38f);
    note(s[4], 0, 260, 784, 784, Wave::Sine, 0.32f);                // notification : un petit carillon
    note(s[4], 90, 260, 988, 988, Wave::Sine, 0.3f);
    note(s[4], 180, 360, 1175, 1175, Wave::Sine, 0.28f);
    return s;
}

PackSounds makeBits()
{
    PackSounds s;
    note(s[0], 0, 30, 1000, 1000, Wave::Square, 0.16f);
    note(s[1], 0, 14, 1500, 1500, Wave::Pulse, 0.06f);
    note(s[2], 0, 45, 523, 523, Wave::Square, 0.15f);               // arpège do, mi, sol
    note(s[2], 40, 45, 659, 659, Wave::Square, 0.15f);
    note(s[2], 80, 90, 784, 784, Wave::Square, 0.15f);
    note(s[3], 0, 50, 784, 784, Wave::Pulse, 0.15f);
    note(s[3], 45, 90, 523, 523, Wave::Pulse, 0.15f);
    note(s[4], 0, 70, 988, 988, Wave::Square, 0.14f);
    note(s[4], 65, 240, 1319, 1319, Wave::Square, 0.14f);
    return s;
}

PackSounds makeArcade()
{
    PackSounds s;
    note(s[0], 0, 40, 1300, 600, Wave::Saw, 0.2f);                  // glissement vers le bas
    note(s[1], 0, 16, 2100, 1600, Wave::Saw, 0.06f);
    note(s[2], 0, 130, 400, 1600, Wave::Square, 0.14f);             // "laser" qui monte
    note(s[3], 0, 130, 1200, 300, Wave::Square, 0.14f);             // ... qui descend
    for (int i = 0; i < 6; ++i)                                     // trille rapide
        note(s[4], static_cast<float>(i) * 40, 50, i % 2 ? 1568.0f : 1047.0f, i % 2 ? 1568.0f : 1047.0f, Wave::Saw, 0.14f);
    return s;
}

std::array<PackSounds, kPackCount> g_sounds;
SDL_AudioStream* g_stream = nullptr;
bool g_enabled = true;
bool g_suspended = false;
Pack g_pack = Pack::Soft;
float g_volume = 0.4f;
Uint64 g_busyUntil = 0;   // fin du dernier son "important" (pas un survol)
Uint64 g_lastHover = 0;
ImGuiID g_hovered = 0;    // l'élément survolé à l'image précédente

void playSample(const Sample& sample, bool important)
{
    if (!g_stream || sample.empty())
        return;
    const Uint64 now = SDL_GetTicks();
    if (!important) {
        // Un survol ne coupe pas un son plus important, et pas plus d'un
        // toutes les 45 ms (sinon, en balayant la grille, ça crépite).
        if (now < g_busyUntil || now - g_lastHover < 45)
            return;
        g_lastHover = now;
    } else {
        g_busyUntil = now + sample.size() * 1000 / kRate;
    }
    // Le nouveau son remplace celui en cours (un seul à la fois suffit).
    SDL_ClearAudioStream(g_stream);
    SDL_PutAudioStreamData(g_stream, sample.data(), static_cast<int>(sample.size() * sizeof(float)));
}

bool pressed(std::initializer_list<ImGuiKey> keys)
{
    for (const ImGuiKey k : keys)
        if (ImGui::IsKeyPressed(k, false))
            return true;
    return false;
}

} // namespace

bool init()
{
    g_sounds = {makeSoft(), makeBits(), makeArcade()};
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = kRate;
    // Un flux à nous, sur la sortie par défaut : SDL le mélange avec celui
    // des jeux (Audio.cpp) et celui du chat vocal.
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g_stream) {
        SDL_Log("Sons de l'interface indisponibles : %s", SDL_GetError());
        return false;
    }
    SDL_SetAudioStreamGain(g_stream, g_volume);
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

void shutdown()
{
    if (g_stream)
        SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
}

void configure(bool enabled, int volume, Pack pack)
{
    g_enabled = enabled;
    g_pack = pack;
    g_volume = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
    if (g_stream)
        SDL_SetAudioStreamGain(g_stream, g_volume);
}

void setSuspended(bool suspended)
{
    g_suspended = suspended;
    if (suspended && g_stream)
        SDL_ClearAudioStream(g_stream);
}

void play(Event event)
{
    if (!g_enabled || g_suspended)
        return;
    playSample(g_sounds[static_cast<size_t>(g_pack)][static_cast<size_t>(event)], event != Event::Hover);
}

void preview(Pack pack)
{
    if (g_suspended)
        return;
    playSample(g_sounds[static_cast<size_t>(pack)][static_cast<size_t>(Event::Validate)], true);
}

void watchImGui()
{
    if (!g_enabled || g_suspended || !g_stream)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    // L'élément sous la souris (un bouton, une jaquette...), 0 = aucun.
    const ImGuiID hovered = ImGui::GetHoveredID();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered != 0)
        play(Event::Click);
    else if (hovered != 0 && hovered != g_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
        play(Event::Hover);
    g_hovered = hovered;

    // Clavier et manette (pas pendant la saisie d'un texte).
    if (io.WantTextInput)
        return;
    if (pressed({ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_GamepadFaceDown}))
        play(Event::Validate);
    else if (pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}))
        play(Event::Back);
    else if (pressed({ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Tab,
                      ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft,
                      ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadL1, ImGuiKey_GamepadR1}))
        play(Event::Hover);
}

} // namespace sounds
