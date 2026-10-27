#include "Jni.hpp"

#include "../Utf8.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace android {

namespace {
jclass g_bridge = nullptr;

// Appelle une méthode statique de NexoBridge qui ne prend rien et renvoie un booléen.
bool callBool(const char* name)
{
    JNIEnv* e = env();
    jmethodID m = e->GetStaticMethodID(bridge(), name, "()Z");
    return m && e->CallStaticBooleanMethod(bridge(), m);
}
} // namespace

void init()
{
    JNIEnv* e = env();
    // (Sur le thread principal, FindClass voit les classes de l'appli ; sur un
    // autre thread, non : d'où la référence globale gardée ici.)
    jclass local = e->FindClass("org/nexoretro/app/NexoBridge");
    if (!local) {
        e->ExceptionClear();
        SDL_Log("Android : classe NexoBridge introuvable");
        return;
    }
    g_bridge = static_cast<jclass>(e->NewGlobalRef(local));
    e->DeleteLocalRef(local);
}

JNIEnv* env()
{
    return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
}

jclass bridge()
{
    return g_bridge;
}

jstring toJava(const std::string& text)
{
    return env()->NewStringUTF(text.c_str());
}

std::string fromJava(jstring text)
{
    if (!text)
        return {};
    JNIEnv* e = env();
    const char* chars = e->GetStringUTFChars(text, nullptr);
    std::string out = chars ? chars : "";
    e->ReleaseStringUTFChars(text, chars);
    return out;
}

jbyteArray toJava(const void* data, size_t size)
{
    JNIEnv* e = env();
    jbyteArray array = e->NewByteArray(static_cast<jsize>(size));
    e->SetByteArrayRegion(array, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
    return array;
}

std::string fromJava(jbyteArray bytes)
{
    if (!bytes)
        return {};
    JNIEnv* e = env();
    std::string out(static_cast<size_t>(e->GetArrayLength(bytes)), '\0');
    e->GetByteArrayRegion(bytes, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
    return out;
}

fs::path privateDir()
{
    const char* dir = SDL_GetAndroidInternalStoragePath();
    return dir ? fromUtf8(dir) : fs::path();
}

fs::path dataDir()
{
    // /sdcard/NexoRetro : l'utilisateur y dépose ses jeux et ses BIOS avec
    // n'importe quel gestionnaire de fichiers (ou un câble USB).
    std::error_code ec;
    if (hasAllFilesAccess()) {
        const fs::path shared = "/storage/emulated/0/NexoRetro";
        fs::create_directories(shared, ec);
        if (!ec)
            return shared;
    }
    const char* external = SDL_GetAndroidExternalStoragePath();
    return external ? fromUtf8(external) : privateDir();
}

bool isTv()
{
    return callBool("isTv");
}

bool hasAllFilesAccess()
{
    return callBool("hasAllFilesAccess");
}

void requestAllFilesAccess()
{
    JNIEnv* e = env();
    if (jmethodID m = e->GetStaticMethodID(bridge(), "requestAllFilesAccess", "()V"))
        e->CallStaticVoidMethod(bridge(), m);
}

void prepareAssets(const char* version)
{
    // Déjà fait pour cet APK ? (Un marqueur dans le dossier recopié : la
    // version, et la date d'installation de l'APK.)
    const fs::path dest = privateDir() / "assets";
    const fs::path marker = dest / ".version";
    JNIEnv* e = env();
    jmethodID stampMethod = e->GetStaticMethodID(bridge(), "apkStamp", "()J");
    const std::string stamp =
        std::string(version) + "/" + std::to_string(stampMethod ? e->CallStaticLongMethod(bridge(), stampMethod) : 0);
    std::string done;
    std::getline(std::ifstream(marker), done);
    if (done == stamp)
        return;
    jmethodID m = e->GetStaticMethodID(bridge(), "copyAssets", "(Ljava/lang/String;)Z");
    jstring path = toJava(toUtf8(dest));
    const bool ok = m && e->CallStaticBooleanMethod(bridge(), m, path);
    e->DeleteLocalRef(path);
    if (ok)
        std::ofstream(marker) << stamp;
    SDL_Log("Android : fichiers de l'appli %s", ok ? "prêts" : "impossibles à recopier");
}

} // namespace android
