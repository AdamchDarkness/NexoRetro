#include "Http.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace net {

namespace {

std::wstring widen(std::string_view text)
{
    if (text.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

// Ferme un HINTERNET à la sortie de la fonction, quoi qu'il arrive.
// [C++] Un petit objet RAII "maison" : son destructeur fait le ménage, comme
// un "goto cleanup" du C, mais sans pouvoir l'oublier.
struct Handle {
    HINTERNET h = nullptr;
    ~Handle()
    {
        if (h)
            WinHttpCloseHandle(h);
    }
};

std::string lastError(const char* step)
{
    return std::string(step) + " a échoué (erreur " + std::to_string(GetLastError()) + ")";
}

} // namespace

HttpClient::HttpClient(const char* userAgent)
{
    // AUTOMATIC_PROXY : utilise le proxy configuré dans Windows, s'il y en a un.
    m_session = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (m_session) {
        // Délais en millisecondes : résolution du nom, connexion, envoi, réception.
        WinHttpSetTimeouts(m_session, 10000, 10000, 15000, 30000);
        // Suivre les redirections (y compris http -> https).
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(m_session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    }
}

HttpClient::~HttpClient()
{
    if (m_session)
        WinHttpCloseHandle(m_session);
}

namespace {

// GET ou POST (body : le formulaire à envoyer, nullptr pour un GET).
HttpResponse perform(void* session, const wchar_t* verb, const std::string& url, const std::string* body,
                     const std::string& contentType, const std::atomic<bool>* cancel, HttpClient::Progress* progress)
{
    void* const m_session = session;
    HttpResponse response;
    if (!m_session) {
        response.error = "WinHTTP indisponible";
        return response;
    }

    // Découper l'URL : https://hôte:port/chemin?requête
    const std::wstring wide = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
        response.error = "Adresse invalide : " + url;
        return response;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    Handle connection{WinHttpConnect(static_cast<HINTERNET>(m_session), host.c_str(), parts.nPort, 0)};
    if (!connection.h) {
        response.error = lastError("La connexion");
        return response;
    }
    Handle request{WinHttpOpenRequest(connection.h, verb, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) {
        response.error = lastError("La requête");
        return response;
    }
    // Accepter les réponses compressées (gzip) : les listes de fichiers
    // font 1 Mo en clair, 100 Ko compressées.
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    const std::wstring headers = body ? L"Content-Type: " + widen(contentType) + L"\r\n" : std::wstring();
    void* const data = body ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA;
    const DWORD bodySize = body ? static_cast<DWORD>(body->size()) : 0;
    if (!WinHttpSendRequest(request.h, body ? headers.c_str() : WINHTTP_NO_ADDITIONAL_HEADERS, body ? static_cast<DWORD>(-1) : 0,
                            data, bodySize, bodySize, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        response.error = lastError("Le téléchargement");
        return response;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);
    if (status != 200 && !body)
        return response; // inutile de lire une page d'erreur (un POST, lui, explique souvent l'erreur)
    if (progress) {
        // La taille annoncée par le serveur (absente parfois).
        DWORD length = 0;
        size = sizeof(length);
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
            progress->total = length;
        progress->received = 0;
    }

    // Lire le contenu morceau par morceau.
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        if (cancel && *cancel) {
            response.status = 0;
            response.error = "Annulé";
            response.body.clear();
            return response;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            response.status = 0;
            response.error = lastError("La lecture");
            response.body.clear();
            return response;
        }
        if (read == 0)
            break; // fini
        response.body.append(buffer.data(), read);
        if (progress)
            progress->received = response.body.size();
    }
    return response;
}

} // namespace

HttpResponse HttpClient::get(const std::string& url, const std::atomic<bool>* cancel, Progress* progress)
{
    return perform(m_session, L"GET", url, nullptr, {}, cancel, progress);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body, const std::string& contentType)
{
    return perform(m_session, L"POST", url, &body, contentType, nullptr, nullptr);
}

} // namespace net
