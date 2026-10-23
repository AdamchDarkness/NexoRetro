#pragma once
// Hash : une "empreinte" de 64 bits pour des données quelconques.
//
// Deux fichiers identiques donnent toujours la même empreinte ; changer un
// seul octet la change complètement. Comparer deux empreintes de 8 octets
// suffit donc à savoir si deux joueurs ont le même fichier de jeu, sans
// envoyer le fichier.
//
// L'algorithme mélange les données 8 octets à la fois (multiplications et
// rotations, à la manière de MurmurHash). Il est très rapide, mais n'est
// pas fait pour la sécurité : il détecte les différences, il ne protège pas
// d'un tricheur déterminé.

#include <cstddef>
#include <cstdint>
#include <string_view>

class Hasher64 {
public:
    explicit Hasher64(uint64_t seed = 0x9E3779B97F4A7C15ull) : m_state(seed) {}

    // On peut fournir les données en plusieurs morceaux.
    void update(const void* data, size_t size);
    uint64_t finish() const;

    static uint64_t of(std::string_view text, uint64_t seed = 0x9E3779B97F4A7C15ull);

private:
    void mix(uint64_t word);

    uint64_t m_state;
    uint64_t m_length = 0;
    unsigned char m_pending[8] = {}; // octets en attente d'un mot complet
    size_t m_pendingSize = 0;
};

// CRC32 : l'empreinte "standard" de 32 bits (celle des fichiers .zip).
// Les bases de jeux (No-Intro, libretro) identifient chaque cartouche par
// son CRC32 : c'est grâce à lui que le scraping reconnaît un jeu à coup sûr,
// quel que soit le nom du fichier.
// Usage : crc = crc32(0, données, taille) ; puis crc = crc32(crc, suite, taille)...
uint32_t crc32(uint32_t crc, const void* data, size_t size);
