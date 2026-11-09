#include "ConnectionTest.hpp"

#include <algorithm>
#include <climits>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <netfw.h>
#endif

namespace net {

namespace {
// Les serveurs de Session, plus d'autres : il en faut au moins deux qui
// répondent (à des adresses différentes) pour comparer les ports.
const char* const kHosts[] = {"stun.l.google.com", "stun1.l.google.com", "stun.cloudflare.com", "global.stun.twilio.com",
                              "stun.nextcloud.com"};
constexpr uint16_t kPorts[] = {19302, 19302, 3478, 3478, 443};
constexpr uint16_t kPortBase = 47800; // les ports des salons (Session)
constexpr int kPortCount = 10;
constexpr Uint64 kResendMs = 400;
constexpr int kMaxAttempts = 8;     // ~3 s de questions STUN
constexpr Uint64 kMaxDurationMs = 9000;
constexpr int kNearbySpread = 64;   // le code de réponse essaie 16 ports avant, 48 après (Session.cpp)
} // namespace

ConnectionTest::~ConnectionTest()
{
    if (m_firewallThread.joinable())
        m_firewallThread.join();
}

void ConnectionTest::start()
{
    if (m_firewallThread.joinable())
        m_firewallThread.join();
    m_socket.close();
    m_upnp.reset();
    m_servers.clear();
    m_done = false;
    m_active = true;
    m_mapping = Mapping::NoAnswer;
    m_attempts = 0;
    m_lastSend = 0;

    // Le même port qu'un salon : le test voit ce que verra le salon.
    for (int i = 0; i < kPortCount && !m_socket.isOpen(); ++i)
        m_socket.open(static_cast<uint16_t>(kPortBase + i));
    if (!m_socket.isOpen() && !m_socket.open(0)) {
        SDL_Log("Test de connexion : impossible d'ouvrir un port UDP");
        m_started = SDL_GetTicks();
        finish();
        return;
    }
    m_port = m_socket.localPort();
    m_stun.emplace();
    for (size_t i = 0; i < std::size(kHosts); ++i)
        if (auto a = resolve(kHosts[i], kPorts[i]))
            // Deux noms peuvent mener au même serveur : on ne le garde qu'une fois.
            if (std::none_of(m_servers.begin(), m_servers.end(), [&](const Server& s) { return s.address == *a; }))
                m_servers.push_back({kHosts[i], *a, std::nullopt});

    m_upnp = std::make_unique<PortMapping>();
    m_upnp->start(m_socket.localPort());
    m_firewall = Firewall::Checking;
    // [C++] Le fil reçoit une copie du port ; "this" reste valable car le
    // destructeur attend la fin du fil (join).
    m_firewallThread = std::thread([this, port = m_socket.localPort()] { checkFirewall(port); });
    m_started = SDL_GetTicks();
}

void ConnectionTest::update()
{
    if (!running())
        return;
    const Uint64 now = SDL_GetTicks();

    // Les réponses arrivées.
    uint8_t buffer[1500];
    Address from;
    while (const size_t n = m_socket.receive(from, buffer, sizeof(buffer))) {
        if (!m_stun || !StunRequest::looksLikeStun(buffer, n))
            continue;
        if (auto seen = m_stun->parseResponse(buffer, n))
            for (Server& s : m_servers)
                if (s.address == from)
                    s.seen = seen;
    }

    // Les questions, renvoyées aux serveurs muets (un paquet UDP peut se perdre).
    const bool allAnswered = std::all_of(m_servers.begin(), m_servers.end(), [](const Server& s) { return s.seen.has_value(); });
    if (!allAnswered && m_attempts < kMaxAttempts && now - m_lastSend >= kResendMs) {
        const std::vector<uint8_t> request = m_stun->bytes();
        for (const Server& s : m_servers)
            if (!s.seen)
                m_socket.sendTo(s.address, request.data(), request.size());
        m_lastSend = now;
        ++m_attempts;
    }

    const bool stunDone = allAnswered || (m_attempts >= kMaxAttempts && now - m_lastSend >= kResendMs);
    const bool upnpDone = !m_upnp || m_upnp->finished();
    const bool firewallDone = m_firewall != Firewall::Checking;
    if ((stunDone && upnpDone && firewallDone) || now - m_started > kMaxDurationMs)
        finish();
}

void ConnectionTest::finish()
{
    m_done = true;
    if (m_firewall == Firewall::Checking)
        m_firewall = Firewall::Unknown; // (le fil finira seul ; on ne l'attend plus)
    if (answers() == 0)
        m_mapping = Mapping::NoAnswer;
    else if (answers() == 1 || portSpread() == 0)
        m_mapping = Mapping::Stable; // (un seul serveur : rien à comparer, on suppose le meilleur)
    else if (portSpread() <= kNearbySpread)
        m_mapping = Mapping::Nearby;
    else
        m_mapping = Mapping::Random;
    m_socket.close(); // libère le port pour un salon
    SDL_Log("Réseau : test de connexion\n%s", report().c_str());
}

int ConnectionTest::answers() const
{
    return static_cast<int>(std::count_if(m_servers.begin(), m_servers.end(), [](const Server& s) { return s.seen.has_value(); }));
}

int ConnectionTest::portSpread() const
{
    int lo = INT_MAX, hi = INT_MIN;
    for (const Server& s : m_servers)
        if (s.seen) {
            lo = std::min<int>(lo, s.seen->port);
            hi = std::max<int>(hi, s.seen->port);
        }
    return lo > hi ? 0 : hi - lo;
}

std::optional<Address> ConnectionTest::publicAddress() const
{
    for (const Server& s : m_servers)
        if (s.seen)
            return s.seen;
    return std::nullopt;
}

ConnectionTest::Verdict ConnectionTest::verdict() const
{
    if (m_mapping == Mapping::NoAnswer)
        return Verdict::NoInternet;
    if (m_firewall == Firewall::Blocked)
        return Verdict::Blocked;
    if (m_mapping == Mapping::Random)
        return Verdict::Difficult;
    // Pas besoin d'UPnP si la box ne fait aucune traduction (adresse publique sur le PC).
    const auto pub = publicAddress();
    const std::vector<uint32_t> mine = localAddresses();
    const bool direct = pub && pub->port == localPort() && std::find(mine.begin(), mine.end(), pub->ip) != mine.end();
    if (upnp() || direct)
        return Verdict::Ready;
    return Verdict::NeedsAnswer;
}

std::string ConnectionTest::report() const
{
    std::string r = "Nexo Retro - test de connexion\n";
    r += "Port local : " + std::to_string(localPort()) + "\n";
    for (const Server& s : m_servers)
        r += "  " + s.name + " : " + (s.seen ? s.seen->toString() : std::string("pas de réponse")) + "\n";
    const char* mapping[] = {"aucune réponse", "port stable", "ports voisins", "ports au hasard"};
    r += "Port public : " + std::string(mapping[static_cast<int>(m_mapping)]) + " (écart " + std::to_string(portSpread()) + ")\n";
    r += "UPnP : " + std::string(upnp() ? "port ouvert" : (m_upnp ? m_upnp->message() : std::string("non essayé"))) + "\n";
    const char* firewall[] = {"en cours", "autorisé", "bloqué", "inconnu"};
    r += "Pare-feu : " + std::string(firewall[static_cast<int>(m_firewall.load())]) + "\n";
    r += "Bilan : " + std::string(verdictTitle(verdict())) + "\n";
    return r;
}

void ConnectionTest::checkFirewall(uint16_t port)
{
#ifdef _WIN32
    // On demande au pare-feu de Windows : "les paquets UDP qui arrivent sur
    // ce port, pour ce programme, passent-ils ?" (interface COM INetFwMgr).
    Firewall result = Firewall::Unknown;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    INetFwMgr* manager = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(NetFwMgr), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwMgr),
                                   reinterpret_cast<void**>(&manager)))) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        BSTR image = SysAllocString(exe);
        VARIANT allowed, restricted;
        VariantInit(&allowed);
        VariantInit(&restricted);
        if (SUCCEEDED(manager->IsPortAllowed(image, NET_FW_IP_VERSION_V4, port, nullptr, NET_FW_IP_PROTOCOL_UDP, &allowed,
                                             &restricted)) &&
            allowed.vt == VT_BOOL)
            result = allowed.boolVal == VARIANT_TRUE ? Firewall::Allowed : Firewall::Blocked;
        VariantClear(&allowed);
        VariantClear(&restricted);
        SysFreeString(image);
        manager->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    m_firewall = result;
#else
    (void)port;
    m_firewall = Firewall::Unknown; // Android : pas de pare-feu à régler
#endif
}

const char* verdictTitle(ConnectionTest::Verdict verdict)
{
    switch (verdict) {
    case ConnectionTest::Verdict::Ready: return "Votre connexion est prête";
    case ConnectionTest::Verdict::NeedsAnswer: return "Ça devrait marcher, avec le code de réponse";
    case ConnectionTest::Verdict::Difficult: return "Connexion directe difficile";
    case ConnectionTest::Verdict::Blocked: return "Le pare-feu de Windows bloque Nexo Retro";
    case ConnectionTest::Verdict::NoInternet: return "Internet ne répond pas";
    }
    return "";
}

const char* verdictAdvice(ConnectionTest::Verdict verdict)
{
    switch (verdict) {
    case ConnectionTest::Verdict::Ready:
        return "Votre box a ouvert un port : vos amis vous rejoignent directement avec votre code.";
    case ConnectionTest::Verdict::NeedsAnswer:
        return "Votre box n'ouvre pas de port toute seule. Si votre ami reste bloqué sur « Connexion… », il vous envoie son "
               "code de réponse et vous le collez dans votre salon. Pour vous en passer : activez UPnP dans les réglages de "
               "votre box.";
    case ConnectionTest::Verdict::Difficult:
        return "Votre box change de port à chaque destination : la connexion directe échouera souvent. Solutions : que "
               "l'autre joueur héberge, passer par le relais d'un ami, activer UPnP sur la box, ou utiliser Radmin VPN, "
               "Hamachi ou Tailscale.";
    case ConnectionTest::Verdict::Blocked:
        return "Sécurité Windows > Pare-feu et protection du réseau > Autoriser une application : cochez Nexo Retro, "
               "réseaux privés ET publics, puis relancez le test.";
    case ConnectionTest::Verdict::NoInternet:
        return "Aucun serveur n'a répondu : vérifiez la connexion Internet. Un réseau d'entreprise ou d'école bloque "
               "parfois le jeu en ligne.";
    }
    return "";
}

} // namespace net
