#pragma once
// Crypto : le chiffrement des parties qui passent par un relais.
//
// Quand un ami sert de relais, tous les paquets passent par son PC. Pour
// qu'il ne puisse ni les lire ni les modifier, ils sont chiffrés de bout en
// bout avec AES-256-GCM : un secret tiré au hasard par l'hôte (dans le code
// d'invitation) donne la clé ; chaque paquet porte un "nonce" (numéro unique)
// et une "étiquette" qui prouve qu'il n'a pas été modifié en route.
//
// Le calcul est fait par Windows (BCrypt, sa bibliothèque de cryptographie,
// la même que pour la mise à jour) : pas d'algorithme fait maison.

#include <array>
#include <cstdint>
#include <string>

namespace net {

// Des octets tirés au hasard par Windows (générateur cryptographique).
void randomBytes(void* out, size_t size);
// Empreinte SHA-256 (32 octets).
std::array<uint8_t, 32> sha256(const std::string& data);

class SecureChannel {
public:
    SecureChannel() = default;
    ~SecureChannel();
    SecureChannel(const SecureChannel&) = delete;
    SecureChannel& operator=(const SecureChannel&) = delete;

    // La clé vient du secret de l'invitation (16 octets).
    bool init(const std::array<uint8_t, 16>& secret);
    bool ready() const { return m_key != nullptr; }
    void reset();

    // Chiffre : nonce (12 octets) + texte chiffré + étiquette (16 octets).
    std::string seal(const std::string& plain);
    // Déchiffre et vérifie ; false si le paquet a été modifié ou vient d'ailleurs.
    bool open(const uint8_t* data, size_t size, std::string& plain) const;

private:
    void* m_algorithm = nullptr; // BCRYPT_ALG_HANDLE (on évite <windows.h> ici)
    void* m_key = nullptr;       // BCRYPT_KEY_HANDLE
    uint8_t m_prefix[4] = {};    // début du nonce, propre à ce PC
    uint64_t m_counter = 0;      // fin du nonce : jamais deux fois le même
};

} // namespace net
