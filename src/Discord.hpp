#pragma once
// Discord : "Joue à Zelda sur Super Nintendo" sur votre profil Discord
// (la "Rich Presence").
//
// Pas de bibliothèque à installer : l'application Discord ouverte sur le PC
// écoute sur un "tube nommé" de Windows (\\.\pipe\discord-ipc-0). On s'y
// connecte, on se présente avec l'identifiant de NOTRE application Discord
// (créée une fois pour toutes sur discord.com/developers, il n'a rien de
// secret), puis on envoie l'activité en JSON à chaque changement de jeu.
//
// Tout se passe dans un thread : si Discord n'est pas ouvert, on réessaie
// de temps en temps, sans jamais ralentir l'émulateur.

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

// L'application Discord de l'émulateur (créée sur discord.com/developers).
// Vide tant qu'elle n'a pas été créée : il faut alors en donner une dans
// Paramètres > Jeu en ligne.
inline constexpr const char* kDiscordAppId = "";

class DiscordPresence {
public:
    struct Activity {
        std::string details;     // 1re ligne : le jeu
        std::string state;       // 2e ligne : la console, "En ligne (2 joueurs)"...
        long long startTime = 0; // début de la partie (secondes depuis 1970), 0 = pas de chrono
        std::string image;       // adresse web de la jaquette (vide = pas d'image)
        std::string imageText;
        bool operator==(const Activity&) const = default;
    };

    DiscordPresence();
    ~DiscordPresence();
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // appId vide ou enabled = false : rien n'est affiché sur Discord.
    void configure(bool enabled, const std::string& appId);
    // L'activité à afficher (details vide = aucune).
    void set(const Activity& activity);
    // "Connecté à Discord", "Discord n'est pas ouvert"...
    std::string status() const;

private:
    void run();

    std::thread m_thread;
    mutable std::mutex m_mutex; // protège ce qui suit
    std::condition_variable m_wake;
    bool m_stop = false;
    bool m_enabled = false;
    std::string m_appId;
    Activity m_activity;
    bool m_dirty = false;
    std::string m_status = "Désactivé";
};
