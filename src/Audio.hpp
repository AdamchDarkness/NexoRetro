#pragma once
// Audio : envoie le son produit par le core vers la carte son.
//
// Le core produit des "frames" audio stéréo : 2 échantillons int16_t
// (gauche, droite) par frame. SDL se charge de les convertir à la fréquence
// de la carte son (ex. 32040 Hz pour une SNES -> 48000 Hz).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

class VideoRecorder;

class Audio {
public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool open(double sampleRate);
    void push(const int16_t* frames, size_t count);

    // Durée du son en attente d'être joué, en secondes.
    double queuedSeconds() const;
    void clear();
    // Coupe le son pendant le menu pause.
    void setPaused(bool paused);
    // Ignore le son produit (images recalculées par le rollback : on ne
    // veut pas les entendre une deuxième fois).
    void setMuted(bool muted) { m_muted = muted; }
    // Enregistrement vidéo : le son joué lui est aussi transmis (nullptr = personne).
    void setRecorder(VideoRecorder* recorder) { m_recorder = recorder; }
    // Volume, de 0 (muet) à 100 (le son d'origine du jeu).
    void setVolume(int percent);

    // --- Régulation (à appeler après chaque image du jeu) -----------------
    // L'horloge du jeu et celle de la carte son ne battent jamais exactement
    // au même rythme, et une console lourde (PS2) prend parfois plus d'une
    // image de temps pour en calculer une. Au lieu de vider la file quand
    // elle déborde (un "clic") ou de la laisser se vider (un "trou"), on
    // ajuste très légèrement la vitesse de lecture (±0,5 %, inaudible) pour
    // garder la file à moitié pleine. Des trous répétés agrandissent la file.
    void regulate();
    // Le tampon visé, en secondes (réglage "latence" ; le core peut demander plus).
    void setTargetLatency(double seconds);
    void setMinimumLatency(double seconds); // demandé par le core (0 = rien)
    // Pour les cores qui sautent des images quand le son prend du retard.
    unsigned occupancy() const;   // remplissage de la file, 0 à 100 %
    bool underrunLikely() const;  // la file va bientôt être vide

private:
    SDL_AudioStream* m_stream = nullptr;
    double m_sampleRate = 0.0;
    bool m_muted = false;
    VideoRecorder* m_recorder = nullptr;
    double m_baseTarget = 0.08; // tampon visé (réglage)
    double m_minimum = 0.0;     // demandé par le core
    double m_extra = 0.0;       // ajouté après des trous répétés
    float m_ratio = 1.0f;
    Uint64 m_underrunWindow = 0;
    int m_underruns = 0;
    double target() const { return std::max(m_baseTarget, m_minimum) + m_extra; }
    float m_gain = 1.0f;
};
