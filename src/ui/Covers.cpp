#include "Covers.hpp"

#include "../GlFunctions.hpp"
#include "../Utf8.hpp"
#include "../scrape/Scraper.hpp"
#include "Draw.hpp"
#include "Theme.hpp"
#include "Themes.hpp"

#include <SDL3/SDL.h>

// stb_image est une bibliothèque "en un seul en-tête" : ce #define, dans UN
// seul .cpp, y place le code ; partout ailleurs le .h ne fait que déclarer.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP // (miniatures des emplacements de sauvegarde)
#include <stb_image.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

using theme::px;

Covers::~Covers()
{
    // Le contexte OpenGL a déjà pu disparaître : on ne touche plus aux
    // textures ici, releaseTextures() s'en charge au bon moment.
}

ImU32 Covers::colorFor(const Game& game)
{
    const size_t hash = std::hash<std::string>{}(game.title);
    return theme::coverColor(hash);
}

void Covers::forget(const std::string& file)
{
    const auto it = m_textures.find(file);
    if (it == m_textures.end())
        return;
    if (it->second.id)
        glDeleteTextures(1, &it->second.id);
    m_textures.erase(it);
}

unsigned Covers::createTexture(const uint8_t* rgba, int width, int height)
{
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return id;
}

void Covers::deleteTexture(unsigned id)
{
    if (id)
        glDeleteTextures(1, &id);
}

void Covers::releaseTextures()
{
    for (auto& [file, tex] : m_textures) {
        if (tex.id)
            glDeleteTextures(1, &tex.id);
    }
    m_textures.clear();
}

namespace {

// Les jaquettes en ligne font jusqu'à 1500 pixels de haut : 9 Mo de
// mémoire vidéo chacune. On les réduit à maxSize pixels au plus (720 pour
// une jaquette), en faisant la moyenne de chaque bloc de pixels (factor x factor).
unsigned char* shrink(unsigned char* pixels, int& width, int& height, int maxSize)
{
    const int factor = (std::max(width, height) + maxSize - 1) / maxSize;
    if (factor <= 1)
        return pixels;
    const int w = width / factor;
    const int h = height / factor;
    // [C++] stbi_image_free libère avec free() : on alloue donc avec malloc.
    auto* out = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(w) * h * 4));
    if (!out)
        return pixels;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned sum[4] = {};
            for (int dy = 0; dy < factor; ++dy) {
                const unsigned char* row = pixels + (static_cast<size_t>(y * factor + dy) * width + x * factor) * 4;
                for (int dx = 0; dx < factor; ++dx)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += row[dx * 4 + c];
            }
            unsigned char* dst = out + (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c)
                dst[c] = static_cast<unsigned char>(sum[c] / (factor * factor));
        }
    }
    stbi_image_free(pixels);
    width = w;
    height = h;
    return out;
}

} // namespace

const Covers::Texture* Covers::texture(const std::string& file, int maxSize) const
{
    auto it = m_textures.find(file);
    if (it != m_textures.end())
        return it->second.failed ? nullptr : &it->second;
    // Décoder une image prend ~10 ms : au plus 3 par image affichée, pour
    // que la bibliothèque reste fluide pendant qu'elle se remplit.
    if (m_loadsThisFrame >= 3)
        return nullptr;
    ++m_loadsThisFrame;

    Texture tex;
    // stbi_load attend un chemin "étroit" : on ouvre nous-mêmes le fichier
    // pour gérer les accents (Unicode) sous Windows.
    SDL_IOStream* io = SDL_IOFromFile(file.c_str(), "rb");
    size_t size = 0;
    void* bytes = io ? SDL_LoadFile_IO(io, &size, true) : nullptr;
    int channels = 0;
    unsigned char* pixels = bytes ? stbi_load_from_memory(static_cast<const stbi_uc*>(bytes), static_cast<int>(size),
                                                          &tex.width, &tex.height, &channels, 4)
                                  : nullptr;
    SDL_free(bytes);
    if (!pixels) {
        SDL_Log("Jaquette illisible : %s", file.c_str());
        tex.failed = true;
        m_textures[file] = tex;
        return nullptr;
    }
    pixels = shrink(pixels, tex.width, tex.height, maxSize);
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    return &(m_textures[file] = tex);
}

bool Covers::drawImage(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::string& file, float rounding, int maxSize) const
{
    const Texture* tex = file.empty() ? nullptr : texture(file, maxSize);
    if (!tex)
        return false;
    // Remplissage "cover" : on recadre l'image pour remplir le rectangle
    // sans la déformer.
    const float boxAspect = (max.x - min.x) / (max.y - min.y);
    const float imgAspect = static_cast<float>(tex->width) / tex->height;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (imgAspect > boxAspect) {
        const float keep = boxAspect / imgAspect;
        uv0.x = (1 - keep) / 2;
        uv1.x = 1 - uv0.x;
    } else {
        const float keep = imgAspect / boxAspect;
        uv0.y = (1 - keep) / 2;
        uv1.y = 1 - uv0.y;
    }
    dl->AddImageRounded(static_cast<ImTextureID>(tex->id), min, max, uv0, uv1, IM_COL32_WHITE, rounding);
    return true;
}

bool Covers::drawArt(ImDrawList* dl, ImVec2 min, ImVec2 max, const Game& game, const UserData& userData,
                     float rounding) const
{
    // 1. La jaquette choisie à la main.
    const GameRecord* record = userData.find(game.path);
    if (record && !record->cover.empty() && drawImage(dl, min, max, record->cover, rounding))
        return true;
    // 2. Celle trouvée en ligne.
    if (m_scraper) {
        const scrape::GameInfo* info = m_scraper->find(game.path);
        if (info && drawImage(dl, min, max, info->boxart, rounding))
            return true;
    }
    return false;
}

void Covers::draw(ImDrawList* dl, ImVec2 min, ImVec2 max, const Game& game, const UserData& userData, float rounding,
                  float titleSize) const
{
    // 1. et 2. : la jaquette choisie à la main, ou celle trouvée en ligne.
    if (drawArt(dl, min, max, game, userData, rounding))
        return;
    // 3. Sinon, la jaquette générée.

    draw::rect(dl, min, max, colorFor(game), rounding);
    if (titleSize <= 0.0f)
        return;
    const float pad = std::max(px(8.0f), (max.x - min.x) * 0.085f);
    const float tagSize = std::max(9.0f, titleSize * 0.6f);
    draw::text(dl, ImVec2(min.x + pad, min.y + pad), theme::Weight::Bold, tagSize, theme::withAlpha(theme::CoverInk, 215),
               game.system->shortName);
    // Le titre est collé en bas : on mesure sa hauteur avant de le placer.
    const float wrap = max.x - min.x - pad * 2;
    const float h = draw::wrappedHeight(theme::Weight::ExtraBold, titleSize, game.title, wrap);
    draw::textWrapped(dl, ImVec2(min.x + pad, max.y - pad - h), theme::Weight::ExtraBold, titleSize, theme::CoverInk,
                      game.title, wrap);
}
