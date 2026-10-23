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

#include "Socket.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace net {

struct Invite {
    uint32_t session = 0;            // numéro du salon
    std::vector<Address> candidates; // adresses où essayer de joindre ce PC
};

std::string encodeInvite(const Invite& invite);
std::optional<Invite> decodeInvite(const std::string& code);

} // namespace net
