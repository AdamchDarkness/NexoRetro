// Un mini-core libretro pour tester le jeu en ligne.
//
// C'est un "émulateur" de console imaginaire, minuscule, mais qui respecte
// l'API libretro comme un vrai : l'émulateur le charge exactement comme
// Snes9x ou PCSX2. Il sert à prouver que le netplay (et le rollback) est
// correct, parce que tout son comportement est maîtrisé :
//
//  - 4 carrés de couleur, un par manette, déplacés avec la croix ;
//  - un compteur d'appuis par joueur (détection du "début d'appui") ;
//  - un générateur de hasard qui mélange les touches de tous les joueurs.
//
// TOUT son état tient dans la structure State, sauvegardée en entier par
// retro_serialize : recharger un état le remet EXACTEMENT où il était.
// C'est la condition pour que le rollback fonctionne (des cores comme
// Mr. Boom gardent une partie de leur état ailleurs, et ne la remplissent pas).

#include <libretro.h>

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr unsigned kWidth = 320;
constexpr unsigned kHeight = 240;

struct State {
    uint32_t frame;
    uint32_t rng;
    int16_t x[4], y[4];
    uint32_t presses[4];
    uint16_t previous[4]; // touches de l'image d'avant (DANS l'état : compatible rollback)
};

State g_state;
uint32_t g_pixels[kWidth * kHeight];

retro_environment_t g_environment;
retro_video_refresh_t g_video;
retro_audio_sample_batch_t g_audioBatch;
retro_input_poll_t g_inputPoll;
retro_input_state_t g_inputState;

void reset()
{
    std::memset(&g_state, 0, sizeof(g_state));
    g_state.rng = 0x12345678u;
    for (int p = 0; p < 4; ++p) {
        g_state.x[p] = static_cast<int16_t>(60 + p * 60);
        g_state.y[p] = 120;
    }
}

void fill(int x0, int y0, int w, int h, uint32_t color)
{
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            if (x >= 0 && y >= 0 && x < static_cast<int>(kWidth) && y < static_cast<int>(kHeight))
                g_pixels[y * kWidth + x] = color;
}

} // namespace

// Le câble link de test (linktest.cpp).
retro_environment_t g_linkEnvironment;
void linkTestSetEnvironment(retro_environment_t cb);
void linkTestLoad();
void linkTestRun();
void linkTestUnload();

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    g_environment = cb;
    g_linkEnvironment = cb;
    linkTestSetEnvironment(cb);
    bool noGame = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { g_video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { g_audioBatch = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { g_inputPoll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { g_inputState = cb; }

RETRO_API void retro_init(void) { reset(); }
RETRO_API void retro_deinit(void) {}
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(retro_system_info* info)
{
    std::memset(info, 0, sizeof(*info));
    info->library_name = "Core de test netplay";
    info->library_version = "1.0";
    info->valid_extensions = "sfc|smc|bin|test";
    info->need_fullpath = false;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info* info)
{
    std::memset(info, 0, sizeof(*info));
    info->geometry = {kWidth, kHeight, kWidth, kHeight, 4.0f / 3.0f};
    info->timing = {60.0, 48000.0};
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}
RETRO_API void retro_reset(void) { reset(); }

RETRO_API void retro_run(void)
{
    linkTestRun(); // (hors de l'état : le câble ne fait pas partie de la console)
    g_inputPoll();
    for (int p = 0; p < 4; ++p) {
        uint16_t buttons = 0;
        for (unsigned id = 0; id < 16; ++id)
            if (g_inputState(static_cast<unsigned>(p), RETRO_DEVICE_JOYPAD, 0, id))
                buttons |= static_cast<uint16_t>(1u << id);
        // Les déplacements dépendent des touches...
        if (buttons & (1u << RETRO_DEVICE_ID_JOYPAD_LEFT)) g_state.x[p] -= 2;
        if (buttons & (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)) g_state.x[p] += 2;
        if (buttons & (1u << RETRO_DEVICE_ID_JOYPAD_UP)) g_state.y[p] -= 2;
        if (buttons & (1u << RETRO_DEVICE_ID_JOYPAD_DOWN)) g_state.y[p] += 2;
        g_state.x[p] = static_cast<int16_t>((g_state.x[p] + 320) % 320);
        g_state.y[p] = static_cast<int16_t>((g_state.y[p] + 240) % 240);
        // ... les appuis se comptent au DÉBUT de l'appui (il faut l'image d'avant)...
        g_state.presses[p] += static_cast<uint32_t>(std::popcount(static_cast<uint16_t>(buttons & ~g_state.previous[p])));
        g_state.previous[p] = buttons;
        // ... et le hasard mélange tout : la moindre erreur se propage.
        g_state.rng = g_state.rng * 1103515245u + 12345u + buttons * (p + 1);
    }
    ++g_state.frame;

    // Image : fond qui change avec le hasard, un carré par joueur, et des
    // barres qui montrent le nombre d'appuis de chacun.
    const uint32_t bg = 0x00202020u | ((g_state.rng >> 8) & 0x003F3F3Fu);
    for (uint32_t& px : g_pixels)
        px = bg;
    const uint32_t colors[4] = {0x00E5484Du, 0x001F4FFFu, 0x0015A34Au, 0x00F5B700u};
    for (int p = 0; p < 4; ++p) {
        fill(g_state.x[p] - 8, g_state.y[p] - 8, 16, 16, colors[p]);
        fill(8, 8 + p * 10, static_cast<int>(g_state.presses[p] % 300), 6, colors[p]);
    }
    fill(0, kHeight - 4, static_cast<int>(g_state.frame % kWidth), 4, 0x00FFFFFFu);
    g_video(g_pixels, kWidth, kHeight, kWidth * sizeof(uint32_t));

    int16_t silence[800 * 2] = {};
    g_audioBatch(silence, 800);
}

RETRO_API size_t retro_serialize_size(void) { return sizeof(State); }
RETRO_API bool retro_serialize(void* data, size_t size)
{
    if (size < sizeof(State))
        return false;
    std::memcpy(data, &g_state, sizeof(State));
    return true;
}
RETRO_API bool retro_unserialize(const void* data, size_t size)
{
    if (size < sizeof(State))
        return false;
    std::memcpy(&g_state, data, sizeof(State));
    return true;
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}

RETRO_API bool retro_load_game(const retro_game_info*)
{
    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
    reset();
    linkTestLoad();
    return true;
}
RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }
RETRO_API void retro_unload_game(void) { linkTestUnload(); }
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void* retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
