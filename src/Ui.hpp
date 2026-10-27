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

    // Taille du texte et de toute l'interface (1 = normale, 1.3 = 130 %),
    // en plus de l'agrandissement demandé par Windows. Appliquée au début
    // de l'image suivante (les lettres sont alors préparées à la nouvelle taille).
    void setZoom(float zoom) { m_wantedZoom = zoom; }

    // Petit message temporaire en haut à droite ("État sauvegardé").
    void notify(std::string text);
    void drawNotifications();
    // Cachées pendant un enregistrement vidéo qui garde ce qui est à l'écran.
    void setNotificationsHidden(bool hidden) { m_notificationsHidden = hidden; }

private:
    struct Notification {
        std::string text;
        Uint64 createdAt = 0;
    };

    bool m_initialized = false;
    bool m_rendererReady = false;
    bool m_fontsReady = false;
    float displayScale() const; // l'agrandissement de l'écran (125 %, 150 %... ; limité sous Android)
    SDL_Window* m_window = nullptr;
    float m_displayScale = 1.0f; // celle de Windows (125 %, 150 %...)
    float m_zoom = 1.0f;
    float m_wantedZoom = 1.0f;
    std::filesystem::path m_fontDir;
    std::vector<Notification> m_notifications;
    bool m_notificationsHidden = false;
};
