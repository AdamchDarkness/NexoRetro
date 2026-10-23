#pragma once
// Ui : la mise en place de Dear ImGui (polices, thème) et les notifications.
//
// Dear ImGui fonctionne en "mode immédiat" : il n'y a pas d'objets bouton ou
// fenêtre qui existent en permanence. À CHAQUE image, on réécrit toute
// l'interface avec des appels comme ui::button("Jouer"), qui renvoie true
// l'image où l'on clique dessus.
//
// Le design lui-même (couleurs, composants, écrans) est dans src/ui/.

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

class Ui {
public:
    Ui() = default;
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool init(SDL_Window* window, const std::filesystem::path& fontDir);

    // Partie OpenGL d'ImGui : à refaire à chaque nouveau contexte OpenGL
    // (on en recrée un à chaque jeu lancé).
    bool initRenderer();
    void shutdownRenderer();

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void endFrame();

    // Petit message temporaire en haut à droite ("État sauvegardé").
    void notify(std::string text);
    void drawNotifications();

private:
    struct Notification {
        std::string text;
        Uint64 createdAt = 0;
    };

    bool m_initialized = false;
    bool m_rendererReady = false;
    bool m_fontsReady = false;
    std::filesystem::path m_fontDir;
    std::vector<Notification> m_notifications;
};
