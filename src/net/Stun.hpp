#pragma once
// Stun : découvrir son adresse publique.
//
// Derrière une box, le PC a une adresse privée (192.168.1.20). Pour qu'un
// ami nous joigne, il faut l'adresse PUBLIQUE de la box, et le port que la
// box a choisi pour nous. On envoie une petite question à un serveur STUN
// public et gratuit, qui répond "je te vois depuis 82.64.12.7:51000".
// Aucun compte, rien n'est stocké : c'est un simple miroir.
//
// Protocole : RFC 5389 (requête "Binding" de 20 octets).

#include "Socket.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace net {

class StunRequest {
public:
    StunRequest();
    // Le paquet à envoyer au serveur.
    std::vector<uint8_t> bytes() const;
    // Si ce paquet reçu est la réponse à NOTRE question, renvoie l'adresse publique.
    std::optional<Address> parseResponse(const uint8_t* data, size_t size) const;
    // Les réponses STUN se reconnaissent à leur "nombre magique".
    static bool looksLikeStun(const uint8_t* data, size_t size);

private:
    std::array<uint8_t, 12> m_transaction{}; // identifiant aléatoire de la question
};

} // namespace net
