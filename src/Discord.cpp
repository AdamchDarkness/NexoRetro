#include "Discord.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Les messages du tube : un en-tête de 8 octets (type, longueur), puis du JSON.
enum Opcode : int32_t { Handshake = 0, Frame = 1, Close = 2 };

// Un texte placé entre guillemets dans du JSON : les " et \ sont précédés d'un \.
std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        out += c;
    }
    return out + "\"";
}

class Pipe {
public:
    ~Pipe() { close(); }

    bool open()
    {
        // Discord ouvre le premier tube libre : discord-ipc-0, sinon -1...
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            m_handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (m_handle != INVALID_HANDLE_VALUE)
                return true;
        }
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    void close()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }

    bool isOpen() const { return m_handle != INVALID_HANDLE_VALUE; }

    bool write(int32_t opcode, const std::string& json)
    {
        std::vector<char> data(8 + json.size());
        const int32_t header[2] = {opcode, static_cast<int32_t>(json.size())};
        std::memcpy(data.data(), header, 8);
        std::memcpy(data.data() + 8, json.data(), json.size());
        DWORD written = 0;
        if (!WriteFile(m_handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) || written != data.size()) {
            close();
            return false;
        }
        return true;
    }

    // Attend une réponse (au plus timeoutMs) sans jamais bloquer pour de bon :
    // PeekNamedPipe dit combien d'octets sont arrivés.
    bool read(int32_t& opcode, std::string& json, int timeoutMs)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        DWORD available = 0;
        while (true) {
            if (!PeekNamedPipe(m_handle, nullptr, 0, nullptr, &available, nullptr)) {
                close();
                return false;
            }
            if (available >= 8)
                break;
            if (std::chrono::steady_clock::now() > until)
                return false;
            Sleep(20);
        }
        int32_t header[2] = {};
        DWORD got = 0;
        if (!ReadFile(m_handle, header, 8, &got, nullptr) || got != 8 || header[1] < 0 || header[1] > (1 << 20)) {
            close();
            return false;
        }
        json.assign(static_cast<size_t>(header[1]), '\0');
        if (header[1] > 0 && (!ReadFile(m_handle, json.data(), static_cast<DWORD>(header[1]), &got, nullptr) ||
                              got != static_cast<DWORD>(header[1]))) {
            close();
            return false;
        }
        opcode = header[0];
        if (SDL_getenv("EMU_DISCORD_TRACE")) // tests : tout ce que Discord répond
            SDL_Log("Discord <- %d %s", opcode, json.c_str());
        return true;
    }

    // Lit les réponses déjà arrivées (pour que le tube ne se remplisse pas).
    // Renvoie le message de la dernière erreur signalée par Discord, s'il y en a une.
    std::string drain()
    {
        int32_t opcode = 0;
        std::string json, error;
        while (isOpen() && read(opcode, json, 0)) {
            if (json.find("\"evt\":\"ERROR\"") == std::string::npos)
                continue;
            const size_t at = json.find("\"message\":\"");
            error = at == std::string::npos ? json : json.substr(at + 11, json.find('"', at + 11) - at - 11);
            SDL_Log("Discord : erreur : %s", json.c_str());
        }
        return error;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

std::string activityJson(const DiscordPresence::Activity& a, int nonce)
{
    std::string activity = "null"; // "null" : on efface l'activité
    if (!a.details.empty()) {
        activity = "{\"details\":" + quoted(a.details.substr(0, 120));
        if (!a.state.empty())
            activity += ",\"state\":" + quoted(a.state.substr(0, 120));
        if (a.startTime > 0)
            activity += ",\"timestamps\":{\"start\":" + std::to_string(a.startTime) + "}";
        if (!a.image.empty())
            activity += ",\"assets\":{\"large_image\":" + quoted(a.image) +
                        (a.imageText.empty() ? "" : ",\"large_text\":" + quoted(a.imageText.substr(0, 120))) + "}";
        activity += "}";
    }
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
           ",\"activity\":" + activity + "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
}

} // namespace

DiscordPresence::DiscordPresence()
{
    m_thread = std::thread([this] { run(); });
}

DiscordPresence::~DiscordPresence()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

void DiscordPresence::configure(bool enabled, const std::string& appId)
{
    std::lock_guard lock(m_mutex);
    if (enabled == m_enabled && appId == m_appId)
        return;
    m_enabled = enabled;
    m_appId = appId;
    m_dirty = true;
    m_wake.notify_all();
}

void DiscordPresence::set(const Activity& activity)
{
    std::lock_guard lock(m_mutex);
    if (activity == m_activity)
        return;
    m_activity = activity;
    m_dirty = true;
    m_wake.notify_all();
}

std::string DiscordPresence::status() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

void DiscordPresence::run()
{
    Pipe pipe;
    std::string connectedId; // l'identifiant avec lequel on s'est présenté
    int nonce = 0;
    auto nextTry = std::chrono::steady_clock::now();
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        // Désactivé (ou identifiant changé) : on se déconnecte.
        if (pipe.isOpen() && (!m_enabled || m_appId != connectedId)) {
            pipe.close();
            connectedId.clear();
        }
        if (!m_enabled || m_appId.empty()) {
            m_status = m_enabled ? "Identifiant d'application Discord manquant" : "Désactivé";
            m_wake.wait(lock);
            continue;
        }

        // Connexion (Discord vient peut-être d'être ouvert).
        if (!pipe.isOpen() && std::chrono::steady_clock::now() >= nextTry) {
            const std::string appId = m_appId;
            lock.unlock();
            bool ok = pipe.open();
            std::string answer;
            int32_t opcode = -1;
            if (ok)
                ok = pipe.write(Handshake, "{\"v\":1,\"client_id\":" + quoted(appId) + "}") && pipe.read(opcode, answer, 3000) &&
                     opcode == Frame;
            lock.lock();
            if (ok) {
                connectedId = appId;
                m_status = "Connecté à Discord";
                m_dirty = true; // renvoyer l'activité actuelle
                SDL_Log("Discord : connecté");
            } else {
                // Pas ouvert, ou identifiant refusé (Discord répond "Close").
                m_status = opcode == Close ? "Identifiant refusé par Discord" : "Discord n'est pas ouvert";
                if (opcode == Close)
                    SDL_Log("Discord : identifiant refusé : %s", answer.c_str());
                pipe.close();
                nextTry = std::chrono::steady_clock::now() + std::chrono::seconds(opcode == Close ? 120 : 20);
            }
        }

        // Envoi de l'activité.
        if (pipe.isOpen() && m_dirty) {
            const std::string json = activityJson(m_activity, ++nonce);
            m_dirty = false;
            lock.unlock();
            const bool ok = pipe.write(Frame, json);
            std::string answer;
            int32_t opcode = 0;
            const bool answered = ok && pipe.read(opcode, answer, 2000); // la réponse à notre activité
            const std::string error = answered && answer.find("\"evt\":\"ERROR\"") != std::string::npos ? answer : "";
            lock.lock();
            if (!error.empty()) {
                // Le plus souvent : identifiant d'application inconnu.
                SDL_Log("Discord : activité refusée : %s", error.c_str());
                m_status = "Refusé par Discord : vérifiez l'identifiant d'application";
            } else if (answered) {
                m_status = "Connecté à Discord";
            }
            if (!ok) {
                m_status = "Discord n'est pas ouvert";
                m_dirty = true;
                nextTry = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            }
        }

        // On dort jusqu'au prochain changement, ou 5 s (pour lire les
        // réponses de Discord et voir s'il a été fermé ou ouvert).
        m_wake.wait_for(lock, std::chrono::seconds(5));
        if (pipe.isOpen()) {
            lock.unlock();
            pipe.drain(); // (les erreurs y sont déjà notées dans le journal)
            lock.lock();
            if (!pipe.isOpen())
                m_status = "Discord n'est pas ouvert";
        }
    }
}
