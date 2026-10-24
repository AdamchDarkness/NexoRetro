#include "Crypto.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace net {

namespace {
constexpr size_t kNonce = 12;
constexpr size_t kTag = 16;
} // namespace

void randomBytes(void* out, size_t size)
{
    BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

std::array<uint8_t, 32> sha256(const std::string& data)
{
    std::array<uint8_t, 32> digest{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                   static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return digest;
}

SecureChannel::~SecureChannel()
{
    reset();
}

void SecureChannel::reset()
{
    if (m_key)
        BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(m_key));
    if (m_algorithm)
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_algorithm), 0);
    m_key = nullptr;
    m_algorithm = nullptr;
}

bool SecureChannel::init(const std::array<uint8_t, 16>& secret)
{
    reset();
    // La clé : SHA-256 du secret (avec un préfixe propre à Nexo Retro).
    const std::array<uint8_t, 32> key =
        sha256("NexoRetro-relais-v1" + std::string(reinterpret_cast<const char*>(secret.data()), secret.size()));
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;
    m_algorithm = algorithm;
    BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, const_cast<PUCHAR>(key.data()),
                                                   static_cast<ULONG>(key.size()), 0))) {
        reset();
        return false;
    }
    m_key = handle;
    randomBytes(m_prefix, sizeof(m_prefix));
    m_counter = 0;
    return true;
}

std::string SecureChannel::seal(const std::string& plain)
{
    if (!m_key)
        return {};
    std::string out(kNonce + plain.size() + kTag, '\0');
    uint8_t* nonce = reinterpret_cast<uint8_t*>(out.data());
    std::memcpy(nonce, m_prefix, 4);
    const uint64_t counter = ++m_counter;
    std::memcpy(nonce + 4, &counter, 8);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce;
    info.cbNonce = kNonce;
    info.pbTag = reinterpret_cast<PUCHAR>(out.data() + kNonce + plain.size());
    info.cbTag = kTag;
    ULONG written = 0;
    const NTSTATUS status = BCryptEncrypt(static_cast<BCRYPT_KEY_HANDLE>(m_key),
                                          reinterpret_cast<PUCHAR>(const_cast<char*>(plain.data())), static_cast<ULONG>(plain.size()),
                                          &info, nullptr, 0, reinterpret_cast<PUCHAR>(out.data() + kNonce),
                                          static_cast<ULONG>(plain.size()), &written, 0);
    return BCRYPT_SUCCESS(status) ? out : std::string();
}

bool SecureChannel::open(const uint8_t* data, size_t size, std::string& plain) const
{
    if (!m_key || size < kNonce + kTag)
        return false;
    const size_t length = size - kNonce - kTag;
    plain.assign(length, '\0');
    // (BCrypt ne modifie pas le nonce ni l'étiquette, mais ses paramètres ne sont pas "const".)
    std::vector<uint8_t> nonce(data, data + kNonce);
    std::vector<uint8_t> tag(data + kNonce + length, data + size);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce.data();
    info.cbNonce = kNonce;
    info.pbTag = tag.data();
    info.cbTag = kTag;
    ULONG written = 0;
    const NTSTATUS status = BCryptDecrypt(static_cast<BCRYPT_KEY_HANDLE>(m_key), const_cast<PUCHAR>(data + kNonce),
                                          static_cast<ULONG>(length), &info, nullptr, 0,
                                          reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(length), &written, 0);
    return BCRYPT_SUCCESS(status);
}

} // namespace net
