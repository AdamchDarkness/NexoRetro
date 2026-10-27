// Crypto sous Android : SHA-256 et AES-256-GCM sont faits par Java
// (java.security / javax.crypto, fournis par le système). Le format des
// paquets est exactement celui de la version Windows (net/Crypto.cpp, BCrypt) :
//   nonce (12 octets) | texte chiffré | étiquette (16 octets)
// Un PC et un téléphone se comprennent donc à travers le même relais.

#include "../net/Crypto.hpp"
#include "Jni.hpp"

#include <cstring>
#include <fstream>

namespace net {

namespace {

constexpr size_t kNonce = 12;
constexpr size_t kTag = 16;

// La clé AES, gardée en clair dans l'objet (32 octets).
struct Key {
    uint8_t bytes[32];
};

// NexoBridge.aesGcm : chiffre (ou déchiffre et vérifie) ; null si refusé.
std::string aesGcm(bool encrypt, const uint8_t* key, const uint8_t* nonce, const uint8_t* data, size_t size, bool& ok)
{
    JNIEnv* e = android::env();
    jmethodID m = e->GetStaticMethodID(android::bridge(), "aesGcm", "(Z[B[B[B)[B");
    ok = false;
    if (!m) {
        e->ExceptionClear();
        return {};
    }
    jbyteArray jkey = android::toJava(key, 32);
    jbyteArray jnonce = android::toJava(nonce, kNonce);
    jbyteArray jdata = android::toJava(data, size);
    auto result = static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), m, encrypt ? JNI_TRUE : JNI_FALSE, jkey,
                                                                    jnonce, jdata));
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        result = nullptr;
    }
    std::string out;
    if (result) {
        out = android::fromJava(result);
        ok = true;
        e->DeleteLocalRef(result);
    }
    e->DeleteLocalRef(jkey);
    e->DeleteLocalRef(jnonce);
    e->DeleteLocalRef(jdata);
    return out;
}

} // namespace

void randomBytes(void* out, size_t size)
{
    // /dev/urandom : le générateur cryptographique du noyau Linux d'Android.
    std::ifstream("/dev/urandom", std::ios::binary).read(static_cast<char*>(out), static_cast<std::streamsize>(size));
}

std::array<uint8_t, 32> sha256(const std::string& data)
{
    std::array<uint8_t, 32> digest{};
    JNIEnv* e = android::env();
    jmethodID m = e->GetStaticMethodID(android::bridge(), "sha256", "([B)[B");
    if (!m) {
        e->ExceptionClear();
        return digest;
    }
    jbyteArray in = android::toJava(data.data(), data.size());
    auto result = static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), m, in));
    const std::string bytes = android::fromJava(result);
    if (bytes.size() == digest.size())
        std::memcpy(digest.data(), bytes.data(), digest.size());
    e->DeleteLocalRef(in);
    if (result)
        e->DeleteLocalRef(result);
    return digest;
}

SecureChannel::~SecureChannel()
{
    reset();
}

void SecureChannel::reset()
{
    delete static_cast<Key*>(m_key);
    m_key = nullptr;
}

bool SecureChannel::init(const std::array<uint8_t, 16>& secret)
{
    reset();
    // Même clé que sous Windows : SHA-256 du préfixe et du secret.
    const std::array<uint8_t, 32> key =
        sha256("NexoRetro-relais-v1" + std::string(reinterpret_cast<const char*>(secret.data()), secret.size()));
    auto* k = new Key;
    std::memcpy(k->bytes, key.data(), 32);
    m_key = k;
    randomBytes(m_prefix, sizeof(m_prefix));
    m_counter = 0;
    return true;
}

std::string SecureChannel::seal(const std::string& plain)
{
    if (!m_key)
        return {};
    uint8_t nonce[kNonce];
    std::memcpy(nonce, m_prefix, 4);
    const uint64_t counter = ++m_counter;
    std::memcpy(nonce + 4, &counter, 8);
    bool ok = false;
    // (Java rend le texte chiffré suivi de l'étiquette : on y ajoute le nonce devant.)
    const std::string sealed = aesGcm(true, static_cast<Key*>(m_key)->bytes, nonce,
                                      reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), ok);
    if (!ok)
        return {};
    return std::string(reinterpret_cast<const char*>(nonce), kNonce) + sealed;
}

bool SecureChannel::open(const uint8_t* data, size_t size, std::string& plain) const
{
    if (!m_key || size < kNonce + kTag)
        return false;
    bool ok = false;
    plain = aesGcm(false, static_cast<Key*>(m_key)->bytes, data, data + kNonce, size - kNonce, ok);
    return ok;
}

} // namespace net
