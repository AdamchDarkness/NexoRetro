#pragma once
// Http : télécharger une page ou un fichier sur le web (HTTPS compris).
//
// On utilise WinHTTP, la bibliothèque fournie avec Windows : rien à
// installer, et les certificats HTTPS sont vérifiés par le système.
//
// Les appels sont BLOQUANTS (ils attendent la réponse) : à n'utiliser que
// depuis un thread d'arrière-plan, jamais depuis l'interface.

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace net {

struct HttpResponse {
    int status = 0;     // 200 = trouvé, 404 = introuvable, 0 = pas de réponse
    std::string body;   // le contenu (octets bruts : texte, PNG...)
    std::string error;  // explication si status == 0

    bool ok() const { return status == 200; }
};

class HttpClient {
public:
    // userAgent : le nom sous lequel on se présente aux serveurs.
    explicit HttpClient(const char* userAgent);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Où en est un gros téléchargement (lu depuis un autre thread : atomiques).
    struct Progress {
        std::atomic<uint64_t> received{0};
        std::atomic<uint64_t> total{0}; // 0 : taille inconnue
    };

    // cancel : si ce drapeau passe à true, le téléchargement s'arrête.
    // progress : rempli au fur et à mesure (facultatif).
    HttpResponse get(const std::string& url, const std::atomic<bool>* cancel = nullptr, Progress* progress = nullptr);

private:
    // HINTERNET est un "void*" : on évite d'inclure <windows.h> partout.
    void* m_session = nullptr;
};

// Encode un morceau d'URL : "Street Fighter II (USA)" -> "Street%20Fighter%20II%20%28USA%29".
std::string urlEncode(std::string_view text);
// L'inverse : "%C3%A9" -> "é".
std::string urlDecode(std::string_view text);

} // namespace net
