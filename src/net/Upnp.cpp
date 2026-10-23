#include "Upnp.hpp"

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <SDL3/SDL.h>

#include <cstdio>

namespace net {

PortMapping::~PortMapping()
{
    if (m_thread.joinable())
        m_thread.join();
    remove();
}

void PortMapping::start(uint16_t port)
{
    if (m_thread.joinable())
        return;
    // [C++] La lambda est exécutée dans le nouveau fil d'exécution.
    m_thread = std::thread([this, port] { run(port); });
}

std::string PortMapping::message() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_message;
}

void PortMapping::run(uint16_t port)
{
    auto finish = [&](bool ok, std::string text) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_message = std::move(text);
        }
        m_succeeded = ok;
        m_finished = true;
    };

    // 1. Trouver la box sur le réseau local (2 secondes au plus).
    int error = 0;
    UPNPDev* devices = upnpDiscover(2000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &error);
    if (!devices)
        return finish(false, "Aucune box compatible UPnP trouvée");

    UPNPUrls urls{};
    IGDdatas data{};
    char lanAddress[64] = "";
    char wanAddress[64] = "";
    const int igd = UPNP_GetValidIGD(devices, &urls, &data, lanAddress, sizeof(lanAddress), wanAddress, sizeof(wanAddress));
    freeUPNPDevlist(devices);
    if (igd != UPNP_CONNECTED_IGD) {
        if (igd != UPNP_NO_IGD)
            FreeUPNPUrls(&urls);
        return finish(false, "La box n'accepte pas l'ouverture automatique de port");
    }

    // 2. Demander la redirection du port UDP vers ce PC (pour 2 heures).
    const std::string portText = std::to_string(port);
    const int result = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, portText.c_str(), portText.c_str(),
                                           lanAddress, "Emulateur (jeu en ligne)", "UDP", nullptr, "7200");
    if (result != UPNPCOMMAND_SUCCESS) {
        FreeUPNPUrls(&urls);
        return finish(false, std::string("La box a refusé l'ouverture du port : ") + strupnperror(result));
    }

    char external[64] = "";
    UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external);
    // "82.64.12.7" -> nombre de 32 bits, octet par octet.
    uint32_t ip = 0;
    int parts = 0;
    unsigned value = 0;
    for (const char* c = external;; ++c) {
        if (*c >= '0' && *c <= '9') {
            value = value * 10 + static_cast<unsigned>(*c - '0');
        } else {
            ip = (ip << 8) | (value & 0xFF);
            value = 0;
            ++parts;
            if (*c != '.')
                break;
        }
    }
    if (parts == 4)
        m_externalIp = ip;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_controlUrl = urls.controlURL;
        m_serviceType = data.first.servicetype;
        m_port = portText;
    }
    FreeUPNPUrls(&urls);
    SDL_Log("UPnP : port UDP %s ouvert sur la box (%s)", portText.c_str(), external);
    finish(true, "Port ouvert sur la box");
}

void PortMapping::remove()
{
    if (!m_succeeded || m_controlUrl.empty())
        return;
    UPNP_DeletePortMapping(m_controlUrl.c_str(), m_serviceType.c_str(), m_port.c_str(), "UDP", nullptr);
    SDL_Log("UPnP : port %s refermé", m_port.c_str());
}

} // namespace net
