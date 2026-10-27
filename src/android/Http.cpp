// HttpClient sous Android : le téléchargement est confié à Java
// (HttpURLConnection, avec les certificats HTTPS du système). Voir
// NexoBridge.httpGet. Même interface que la version Windows (net/Http.cpp).

#include "../net/Http.hpp"
#include "Jni.hpp"

namespace net {

HttpClient::HttpClient(const char* userAgent) : m_session(new std::string(userAgent ? userAgent : "NexoRetro")) {}

HttpClient::~HttpClient()
{
    delete static_cast<std::string*>(m_session);
}

HttpResponse HttpClient::get(const std::string& url, const std::atomic<bool>* cancel, Progress* progress)
{
    HttpResponse response;
    if (cancel && *cancel) {
        response.error = "annulé";
        return response;
    }
    JNIEnv* e = android::env();
    jmethodID m = e->GetStaticMethodID(android::bridge(), "httpGet", "(Ljava/lang/String;Ljava/lang/String;[I)[B");
    if (!m) {
        e->ExceptionClear();
        response.error = "téléchargement indisponible";
        return response;
    }
    jstring jurl = android::toJava(url);
    jstring jagent = android::toJava(*static_cast<std::string*>(m_session));
    jintArray status = e->NewIntArray(1);
    auto body = static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), m, jurl, jagent, status));
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        body = nullptr;
    }
    jint code = 0;
    e->GetIntArrayRegion(status, 0, 1, &code);
    response.status = code;
    response.body = android::fromJava(body);
    if (code == 0)
        response.error = "pas de réponse (connexion Internet ?)";
    if (progress) {
        progress->total = response.body.size();
        progress->received = response.body.size();
    }
    // [JNI] Ce thread vit longtemps (scraper, mises à jour) : on rend tout de
    // suite les références Java qu'il a créées.
    e->DeleteLocalRef(jurl);
    e->DeleteLocalRef(jagent);
    e->DeleteLocalRef(status);
    if (body)
        e->DeleteLocalRef(body);
    return response;
}

} // namespace net
