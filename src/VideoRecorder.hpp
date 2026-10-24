#pragma once
// VideoRecorder : enregistre ce qu'on joue dans une vidéo MP4 (H.264 + AAC),
// pour garder ou publier une run (speedrun.com, YouTube...).
//
// L'encodage est fait par Windows (Media Foundation, présent sur tous les
// Windows 10/11, et qui utilise la puce vidéo quand elle sait le faire) :
// aucune bibliothèque à ajouter. Il tourne dans un thread à part : le jeu
// ne fait que déposer ses images et son son dans une file.
//
// Les dates des images et du son viennent de l'ÉMULATION (n-ième image du
// jeu, n-ième échantillon) et non de l'horloge : image et son restent
// synchronisés même si le PC rame un instant.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SDL_AudioStream;

class VideoRecorder {
public:
    VideoRecorder() = default;
    ~VideoRecorder();
    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    // fps : images par seconde du jeu ; sampleRate : fréquence de son du core.
    bool start(const std::filesystem::path& file, double fps, double sampleRate);
    // Termine la vidéo (le fichier est complet une fois le thread fini).
    void stop();
    bool recording() const { return m_recording; }
    const std::filesystem::path& file() const { return m_file; }

    // Une image du jeu (BGRA, lignes de BAS en HAUT comme OpenGL la lit),
    // pour l'image d'émulation numéro "frame" (compteur du jeu).
    void addFrame(std::vector<uint8_t> bgra, int width, int height, uint64_t frame);
    // Du son du core (stéréo, 16 bits).
    void addAudio(const int16_t* frames, size_t count);

    // Erreur rencontrée par le thread (vide si tout va bien), une seule fois.
    std::string takeError();

private:
    struct Job {
        bool video = false;
        std::vector<uint8_t> data; // image BGRA ou son PCM 48 kHz
        int width = 0;
        int height = 0;
        uint64_t frame = 0;
    };
    void worker();

    std::filesystem::path m_file;
    double m_fps = 60.0;
    int m_width = 0;  // taille de la vidéo (fixée à la première image)
    int m_height = 0;
    std::atomic<bool> m_recording{false};
    SDL_AudioStream* m_resampler = nullptr; // son du core -> 48 kHz (seul l'AAC à 48 kHz est sûr)
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    bool m_stopping = false;
    std::string m_error;
};
