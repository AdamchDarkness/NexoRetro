// Le câble link du core de test : il imite les deux façons de faire des
// vrais cores, pour tester l'émulateur sans jeu Pokémon sous la main.
//
//  - comme gpSP (interface "netpacket") : à chaque image, un paquet numéroté
//    envoyé à tous ; à l'arrivée, on vérifie qu'aucun ne manque ni n'est
//    dans le désordre ;
//  - comme Gambatte (câble Game Boy en TCP, mêmes noms d'options) : le
//    "serveur" envoie 2 octets à chaque image et ATTEND la réponse (en
//    bloquant, comme Gambatte) ; le "client" répond octet + 1.
//
// Les résultats sont écrits dans le journal à la fermeture du jeu.

#include <libretro.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern retro_environment_t g_linkEnvironment; // défini dans testcore.cpp

namespace {

retro_log_printf_t g_log;

void log(const char* text)
{
    if (g_log)
        g_log(RETRO_LOG_INFO, "%s\n", text);
}

// --- Façon gpSP -------------------------------------------------------------

struct NetpacketTest {
    retro_netpacket_send_t send = nullptr;
    retro_netpacket_poll_receive_t poll = nullptr;
    uint32_t duringFrame = 0; // reçus pendant l'image (via poll_receive)
    bool inRun = false;
    uint16_t myId = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t errors = 0;
    uint32_t expected[4] = {};
    bool seen[4] = {};
} g_np;

void RETRO_CALLCONV npStart(uint16_t clientId, retro_netpacket_send_t send, retro_netpacket_poll_receive_t poll)
{
    g_np.send = send;
    g_np.poll = poll;
    g_np.myId = clientId;
}

void RETRO_CALLCONV npReceive(const void* buf, size_t len, uint16_t from)
{
    uint32_t packet[3];
    if (len != sizeof(packet) || from >= 4) {
        ++g_np.errors;
        return;
    }
    std::memcpy(packet, buf, sizeof(packet));
    // [0] = signature, [1] = numéro du joueur qui l'envoie, [2] = numéro du paquet
    if (packet[0] != 0x54535431u || packet[1] != from || packet[2] != g_np.expected[from])
        ++g_np.errors;
    g_np.expected[from] = packet[2] + 1;
    g_np.seen[from] = true;
    ++g_np.received;
    if (g_np.inRun)
        ++g_np.duringFrame;
}

void RETRO_CALLCONV npStop()
{
    char text[200];
    int players = 0;
    for (bool s : g_np.seen)
        players += s;
    std::snprintf(text, sizeof(text), "LINKTEST netpacket : joueur %u, %u envoyés, %u reçus de %d joueur(s) (dont %u pendant l'image), %u erreur(s)",
                  g_np.myId, g_np.sent, g_np.received, players, g_np.duringFrame, g_np.errors);
    log(text);
    g_np.send = nullptr;
}

bool RETRO_CALLCONV npConnected(uint16_t)
{
    return true;
}

const retro_netpacket_callback g_netpacket = {npStart, npReceive, npStop, nullptr, npConnected, nullptr, "test-1"};

// --- Façon Gambatte -----------------------------------------------------------

struct TcpTest {
    enum Mode { Off, Server, Client } mode = Off;
    uint16_t port = 56400;
    std::string ip;
    SOCKET listener = INVALID_SOCKET;
    SOCKET link = INVALID_SOCKET;
    uint8_t counter = 0;
    uint32_t exchanges = 0;
    uint32_t errors = 0;
    uint32_t lastTry = 0;
    uint32_t frame = 0;
} g_tcp;

std::string option(const char* key)
{
    retro_variable var{key, nullptr};
    return g_linkEnvironment(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : "";
}

void closeLink()
{
    if (g_tcp.link != INVALID_SOCKET)
        closesocket(g_tcp.link);
    g_tcp.link = INVALID_SOCKET;
}

void tcpConnectIfNeeded()
{
    if (g_tcp.link != INVALID_SOCKET)
        return;
    if (g_tcp.mode == TcpTest::Server) {
        // Comme Gambatte : accept() seulement si quelqu'un attend (sans bloquer).
        fd_set read;
        FD_ZERO(&read);
        FD_SET(g_tcp.listener, &read);
        timeval none{0, 0};
        if (g_tcp.listener != INVALID_SOCKET && select(0, &read, nullptr, nullptr, &none) > 0)
            g_tcp.link = accept(g_tcp.listener, nullptr, nullptr);
        if (g_tcp.link != INVALID_SOCKET)
            log("LINKTEST tcp : un client est branché");
    } else if (g_tcp.frame - g_tcp.lastTry > 120 || g_tcp.lastTry == 0) {
        g_tcp.lastTry = g_tcp.frame + 1;
        const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(g_tcp.port);
        inet_pton(AF_INET, g_tcp.ip.c_str(), &a.sin_addr);
        if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
            g_tcp.link = s;
            log("LINKTEST tcp : connecté au serveur");
        } else {
            closesocket(s);
        }
    }
    if (g_tcp.link != INVALID_SOCKET) {
        DWORD timeout = 5000; // le vrai Gambatte attendrait sans fin
        setsockopt(g_tcp.link, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    }
}

} // namespace

// Appelé par testcore.cpp.
void linkTestSetEnvironment(retro_environment_t cb)
{
    cb(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, const_cast<retro_netpacket_callback*>(&g_netpacket));
    // Les mêmes options que Gambatte (l'émulateur les règle pour le câble Game Boy).
    static std::string storage[16];
    static retro_variable vars[16];
    size_t n = 0;
    auto add = [&](const char* key, std::string value) {
        storage[n] = std::move(value);
        vars[n] = {key, storage[n].c_str()};
        ++n;
    };
    add("gambatte_gb_link_mode", "Game Link Mode; Not Connected|Network Server|Network Client");
    std::string ports = "Port; ";
    for (int p = 56400; p <= 56420; ++p)
        ports += std::to_string(p) + (p < 56420 ? "|" : "");
    add("gambatte_gb_link_network_port", ports);
    static const char* const ipKeys[12] = {
        "gambatte_gb_link_network_server_ip_1",  "gambatte_gb_link_network_server_ip_2",
        "gambatte_gb_link_network_server_ip_3",  "gambatte_gb_link_network_server_ip_4",
        "gambatte_gb_link_network_server_ip_5",  "gambatte_gb_link_network_server_ip_6",
        "gambatte_gb_link_network_server_ip_7",  "gambatte_gb_link_network_server_ip_8",
        "gambatte_gb_link_network_server_ip_9",  "gambatte_gb_link_network_server_ip_10",
        "gambatte_gb_link_network_server_ip_11", "gambatte_gb_link_network_server_ip_12"};
    for (const char* key : ipKeys)
        add(key, "Chiffre; 0|1|2|3|4|5|6|7|8|9");
    vars[n] = {nullptr, nullptr};
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars);

    retro_log_callback logging{};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        g_log = logging.log;
}

void linkTestLoad()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_np = NetpacketTest{};
    g_tcp = TcpTest{};
    const std::string mode = option("gambatte_gb_link_mode");
    g_tcp.mode = mode == "Network Server" ? TcpTest::Server : mode == "Network Client" ? TcpTest::Client : TcpTest::Off;
    const std::string port = option("gambatte_gb_link_network_port");
    if (!port.empty())
        g_tcp.port = static_cast<uint16_t>(std::stoi(port));
    // 12 chiffres -> "127.0.0.1"
    std::string digits;
    for (int i = 1; i <= 12; ++i)
        digits += option(("gambatte_gb_link_network_server_ip_" + std::to_string(i)).c_str());
    if (digits.size() == 12)
        for (int part = 0; part < 4; ++part)
            g_tcp.ip += std::to_string(std::stoi(digits.substr(part * 3, 3))) + (part < 3 ? "." : "");
    if (g_tcp.mode == TcpTest::Server) {
        g_tcp.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(g_tcp.port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(g_tcp.listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(g_tcp.listener, 1) != 0)
            log("LINKTEST tcp : impossible d'écouter");
    }
}

void linkTestRun()
{
    // gpSP : un paquet numéroté par image, pour tout le monde.
    if (g_np.send) {
        const uint32_t packet[3] = {0x54535431u, g_np.myId, g_np.sent++};
        g_np.send(RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT, packet, sizeof(packet), RETRO_NETPACKET_BROADCAST);
        // Comme melonDS DS : attendre (25 ms au plus) le paquet de l'autre
        // console pour CETTE image, en réclamant les paquets en boucle.
        g_np.inRun = true;
        const uint16_t other = g_np.myId == 0 ? 1 : 0;
        const ULONGLONG until = GetTickCount64() + 25;
        while (g_np.poll && g_np.expected[other] < packet[2] + 1 && GetTickCount64() < until)
            g_np.poll();
        g_np.inRun = false;
    }
    // Gambatte.
    ++g_tcp.frame;
    if (g_tcp.mode == TcpTest::Off)
        return;
    tcpConnectIfNeeded();
    if (g_tcp.link == INVALID_SOCKET)
        return;
    unsigned char buffer[2];
    if (g_tcp.mode == TcpTest::Server) {
        // Le "maître" : envoie et attend la réponse (bloquant).
        buffer[0] = g_tcp.counter;
        buffer[1] = 1;
        if (send(g_tcp.link, reinterpret_cast<char*>(buffer), 2, 0) != 2 ||
            recv(g_tcp.link, reinterpret_cast<char*>(buffer), 2, MSG_WAITALL) != 2) {
            ++g_tcp.errors;
            closeLink();
            return;
        }
        if (buffer[0] == static_cast<unsigned char>(g_tcp.counter + 1))
            ++g_tcp.exchanges;
        else
            ++g_tcp.errors;
        ++g_tcp.counter;
    } else {
        // L'"esclave" : répond à ce qui est arrivé, sans attendre.
        u_long available = 0;
        ioctlsocket(g_tcp.link, FIONREAD, &available);
        while (available >= 2) {
            if (recv(g_tcp.link, reinterpret_cast<char*>(buffer), 2, 0) != 2) {
                closeLink();
                return;
            }
            buffer[0] = static_cast<unsigned char>(buffer[0] + 1);
            buffer[1] = 0;
            send(g_tcp.link, reinterpret_cast<char*>(buffer), 2, 0);
            ++g_tcp.exchanges;
            available -= 2;
        }
    }
}

void linkTestUnload()
{
    if (g_tcp.mode != TcpTest::Off) {
        char text[160];
        std::snprintf(text, sizeof(text), "LINKTEST tcp : %s, %u échange(s) réussi(s), %u erreur(s)",
                      g_tcp.mode == TcpTest::Server ? "serveur" : "client", g_tcp.exchanges, g_tcp.errors);
        log(text);
    }
    closeLink();
    if (g_tcp.listener != INVALID_SOCKET)
        closesocket(g_tcp.listener);
    g_tcp.listener = INVALID_SOCKET;
}
