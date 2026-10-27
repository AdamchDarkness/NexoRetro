// Archive sous Android : les .zip sont lus et écrits par Java
// (java.util.zip, fourni par le système). Même interface que la version
// Windows (Archive.cpp, qui passe par tar.exe).

#include "../Archive.hpp"
#include "../Utf8.hpp"
#include "Jni.hpp"

namespace fs = std::filesystem;

namespace archive {

namespace {

// Une méthode de NexoBridge qui prend deux chemins et renvoie un booléen.
bool callTwoPaths(const char* name, const fs::path& a, const fs::path& b)
{
    JNIEnv* e = android::env();
    jmethodID m = e->GetStaticMethodID(android::bridge(), name, "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (!m) {
        e->ExceptionClear();
        return false;
    }
    jstring ja = android::toJava(toUtf8(a));
    jstring jb = android::toJava(toUtf8(b));
    const bool ok = e->CallStaticBooleanMethod(android::bridge(), m, ja, jb);
    if (e->ExceptionCheck())
        e->ExceptionClear();
    e->DeleteLocalRef(ja);
    e->DeleteLocalRef(jb);
    return ok;
}

} // namespace

bool extract(const fs::path& file, const fs::path& dest)
{
    std::error_code ec;
    fs::create_directories(dest, ec);
    return callTwoPaths("unzip", file, dest);
}

bool zipFolder(const fs::path& folder, const fs::path& file)
{
    std::error_code ec;
    fs::remove(file, ec);
    return callTwoPaths("zipFolder", folder, file) && fs::exists(file, ec);
}

std::vector<fs::path> list(const fs::path& file)
{
    std::vector<fs::path> files;
    JNIEnv* e = android::env();
    jmethodID m = e->GetStaticMethodID(android::bridge(), "listZip", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!m) {
        e->ExceptionClear();
        return files;
    }
    jstring jpath = android::toJava(toUtf8(file));
    auto names = static_cast<jobjectArray>(e->CallStaticObjectMethod(android::bridge(), m, jpath));
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        names = nullptr;
    }
    if (names) {
        const jsize count = e->GetArrayLength(names);
        for (jsize i = 0; i < count; ++i) {
            auto name = static_cast<jstring>(e->GetObjectArrayElement(names, i));
            files.push_back(fromUtf8(android::fromJava(name)));
            e->DeleteLocalRef(name);
        }
        e->DeleteLocalRef(names);
    }
    e->DeleteLocalRef(jpath);
    return files;
}

} // namespace archive
