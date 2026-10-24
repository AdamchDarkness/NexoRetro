#pragma once
// Audio : envoie le son produit par le core vers la carte son.
//
// Le core produit des "frames" audio stéréo : 2 échantillons int16_t
// (gauche, droite) par frame. SDL se charge de les convertir à la fréquence
// de la carte son (ex. 32040 Hz pour une SNES -> 48000 Hz).

#include <SDL3/SDL.h>

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

private:
    SDL_AudioStream* m_stream = nullptr;
    double m_sampleRate = 0.0;
    bool m_muted = false;
    VideoRecorder* m_recorder = nullptr;
    float m_gain = 1.0f;
};
