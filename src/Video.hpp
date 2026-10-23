#pragma once
// Video : la fenêtre et l'affichage des images produites par le core.
//
// Tout l'affichage passe par OpenGL, de deux manières :
//  - rendu logiciel (consoles 2D) : le core nous donne un tableau de pixels,
//    qu'on copie dans une texture puis qu'on dessine à l'écran ;
//  - rendu matériel (consoles 3D : N64, PS2...) : le core dessine lui-même
//    avec OpenGL dans un "framebuffer" que nous lui prêtons, et qu'on
//    recopie ensuite à l'écran.

#include "VulkanRender.hpp"

#include <SDL3/SDL.h>
#include <libretro.h>

#include <filesystem>
#include <vector>

class Video {
public:
    Video() = default;
    ~Video();
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;

    // autoSize : agrandir la fenêtre à la taille du jeu au premier lancement.
    bool open(const char* title, int width, int height, bool autoSize);
    SDL_Window* window() const { return m_window; }
    void setTitle(const char* title);
    void toggleFullscreen();
    void setFullscreen(bool fullscreen);

    // Réglages de l'écran Paramètres.
    void setSmooth(bool smooth) { m_smooth = smooth; }
    void setAspectOverride(float aspect) { m_aspectOverride = aspect; } // 0 = celui du jeu

    // Appelés (indirectement) par le core.
    bool setPixelFormat(retro_pixel_format format);
    bool requestHwRender(retro_hw_render_callback& callback);
    void setGeometry(const retro_game_geometry& geometry);
    void upload(const void* pixels, unsigned width, unsigned height, size_t pitch);
    void hwFrameReady(unsigned width, unsigned height);
    // Rendu Vulkan (voir VulkanRender) : ce que le core a négocié, et
    // l'interface qu'il récupère une fois le contexte prêt.
    void setVulkanNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* n) { m_vulkan.setNegotiation(n); }
    retro_hw_render_interface_vulkan* vulkanInterface() { return m_hwVulkan ? m_vulkan.interface() : nullptr; }
    bool usesVulkan() const { return m_hwVulkan; }

    // Crée le contexte OpenGL. Appelé juste après retro_load_game, car c'est
    // pendant le chargement du jeu que le core dit de quel OpenGL il a besoin.
    // Sans jeu (bibliothèque), on passe une géométrie vide.
    bool createContext(const retro_game_geometry& geometry);
    // Prévient le core que son contexte OpenGL va disparaître.
    void destroyHwContext();
    // Les cores OpenGL dessinent dans LEUR propre contexte OpenGL, partagé
    // avec le nôtre (les textures sont communes, pas les réglages) : à
    // encadrer autour de chaque appel au core qui peut toucher à OpenGL.
    void beginCore();
    void endCore();
    // Détruit le contexte et oublie tout ce que le core précédent avait
    // demandé, pour repartir de zéro avec le jeu suivant.
    void destroyContext();

    // Efface l'écran et dessine la dernière image du jeu, en gardant les
    // proportions. L'interface se dessine ensuite par-dessus.
    void renderGame();
    // Affiche l'image terminée (et écrit la capture d'écran demandée).
    void swap();
    // La capture sera écrite (en BMP) lors du prochain swap().
    void requestScreenshot(std::filesystem::path file) { m_screenshotPath = std::move(file); }
    // Où l'image du jeu est dessinée dans la fenêtre (x, y, largeur, hauteur,
    // en points SDL) : pour viser à la souris.
    const float* gameRect() const { return m_gameRect; }

private:
    // Callbacks donnés au core dans retro_hw_render_callback.
    static uintptr_t getCurrentFramebuffer();
    static retro_proc_address_t getProcAddress(const char* symbol);

    bool createProgram();
    void resetGlState(); // efface les réglages OpenGL laissés par le core
    bool createFramebuffer(unsigned width, unsigned height);
    void destroyFramebuffer();
    void saveScreenshot(int width, int height);

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;     // le nôtre : interface, affichage
    SDL_GLContext m_coreContext = nullptr; // celui du core OpenGL (partagé avec le nôtre)
    unsigned m_uiFbo = 0; // dans NOTRE contexte : le même dessin, pour le copier à l'écran

    // Identifiants d'objets OpenGL (GLuint, c'est-à-dire unsigned int).
    unsigned m_program = 0;
    unsigned m_vao = 0;
    unsigned m_frameTexture = 0; // rendu logiciel
    unsigned m_fbo = 0;          // rendu matériel : framebuffer prêté au core
    unsigned m_fboTexture = 0;
    unsigned m_fboDepth = 0;
    unsigned m_fboWidth = 0;
    unsigned m_fboHeight = 0;
    int m_uvScaleLocation = -1;
    int m_flipYLocation = -1;

    retro_hw_render_callback m_hw{};
    bool m_hwRender = false;
    bool m_hwContextAlive = false;
    bool m_hwVulkan = false; // le core dessine avec Vulkan
    VulkanRender m_vulkan;
    std::vector<uint32_t> m_vulkanPixels;

    // Format par défaut imposé par libretro tant que le core n'a rien dit.
    retro_pixel_format m_pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    retro_pixel_format m_textureFormat = RETRO_PIXEL_FORMAT_UNKNOWN;
    unsigned m_textureWidth = 0;
    unsigned m_textureHeight = 0;

    // Dernière image reçue.
    bool m_hasFrame = false;
    bool m_frameIsHw = false;
    unsigned m_frameWidth = 0;
    unsigned m_frameHeight = 0;

    float m_aspect = 0.0f;
    float m_gameRect[4] = {};
    float m_aspectOverride = 0.0f;
    bool m_smooth = false;
    bool m_windowSized = false;
    std::filesystem::path m_screenshotPath;
};
