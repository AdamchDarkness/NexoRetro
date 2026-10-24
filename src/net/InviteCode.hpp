#pragma once
// InviteCode : les adresses de l'hôte, rangées dans un code à partager.
//
// Le code contient tout ce qu'il faut pour joindre un PC : un numéro de
// salon tiré au hasard, l'adresse publique (vue d'Internet) et les adresses
// locales (box, Radmin, Hamachi). Il est écrit en "base 32" : 32 symboles
// faciles à recopier (pas de I, L, O ni U, qu'on confond avec 1, 0 et V).
//
//   EMU-7K3P-Q9XW-...
//
// Un octet de contrôle permet de détecter une faute de frappe.
//
// Deux formats :
//   - le code COURT (16 symboles, EMU-XXXX-XXXX-XXXX-XXXX) : le salon et
//     une seule adresse, la publique. C'est celui qu'on partage. Les PC du
//     même réseau local (box, Radmin, Hamachi) n'en ont pas besoin : l'ami
//     les trouve par "diffusion" (un message envoyé à tout le réseau).
//   - le code COMPLET : jusqu'à 5 adresses, pour les réseaux où la
//     diffusion ne passe pas (Tailscale, par exemple).
//   - le code PAR RELAIS : les adresses du relais d'un ami, le numéro du
//     salon et un secret de chiffrement. L'adresse de l'hôte n'y est pas.

#include "Socket.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace net {

struct Invite {
    uint32_t session = 0;            // numéro du salon
    std::vector<Address> candidates; // adresses où essayer de joindre ce PC
    // Salon qui passe par le relais d'un ami : les adresses sont alors celles
    // du RELAIS (jamais celle de l'hôte), et le secret donne la clé de
    // chiffrement des paquets (voir Crypto.hpp).
    bool relay = false;
    std::array<uint8_t, 16> secret{};
};

std::string encodeInvite(const Invite& invite);
// Le numéro de salon doit tenir sur 24 bits.
std::string encodeShortInvite(uint32_t session, const Address& address);
// Accepte tous les formats (court, complet, par relais).
std::optional<Invite> decodeInvite(const std::string& code);

// Invitation d'un salon par relais : numéro du salon (32 bits), secret,
// adresses du relais.
std::string encodeRelayInvite(uint32_t room, const std::array<uint8_t, 16>& secret, const std::vector<Address>& relay);
// Le "code du relais" qu'un ami donne à l'hôte : les adresses de son relais.
std::string encodeRelayCode(const std::vector<Address>& relay);
std::optional<std::vector<Address>> decodeRelayCode(const std::string& code);

} // namespace net
