#include "engine/security/Crypto.hpp"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <algorithm>
#include <cstring>
#include <limits>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <bcrypt.h>
#elif defined(__linux__) || defined(__ANDROID__)
    #include <cerrno>
    #include <sys/random.h>
#else
    #include <unistd.h>
#endif

namespace kb::security {
namespace {

static_assert(sizeof(crypto_sha512_ctx) <= 256U, "Sha512Hasher storage is too small for crypto_sha512_ctx");
static_assert(sizeof(crypto_sha512_hmac_ctx) <= 384U, "HmacSha512Hasher storage is too small");
static_assert(alignof(crypto_sha512_ctx) <= 8U && alignof(crypto_sha512_hmac_ctx) <= 8U);

template <typename Context, std::size_t Size>
[[nodiscard]] Context* As(std::array<std::uint8_t, Size>& storage) noexcept {
    return reinterpret_cast<Context*>(storage.data());
}

[[nodiscard]] int HexValue(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

} // namespace

void SecureWipe(std::span<std::uint8_t> bytes) noexcept {
    if (!bytes.empty()) {
        crypto_wipe(bytes.data(), bytes.size());
    }
}

bool SecureRandom(std::span<std::uint8_t> out) noexcept {
    if (out.empty()) {
        return true;
    }
#if defined(_WIN32)
    std::size_t offset = 0U;
    while (offset < out.size()) {
        const ULONG chunk = static_cast<ULONG>(
            std::min<std::size_t>(out.size() - offset, std::numeric_limits<ULONG>::max()));
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data() + offset, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            SecureWipe(out);
            return false;
        }
        offset += chunk;
    }
    return true;
#elif defined(__linux__) || defined(__ANDROID__)
    std::size_t offset = 0U;
    while (offset < out.size()) {
        const ssize_t written = getrandom(out.data() + offset, out.size() - offset, 0U);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            SecureWipe(out);
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
#else
    // getentropy hands out at most 256 bytes per call.
    for (std::size_t offset = 0U; offset < out.size(); offset += 256U) {
        const std::size_t chunk = std::min<std::size_t>(out.size() - offset, 256U);
        if (getentropy(out.data() + offset, chunk) != 0) {
            SecureWipe(out);
            return false;
        }
    }
    return true;
#endif
}

bool ConstantTimeEqual(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    // Fixed sizes go to Monocypher's verified constant-time comparisons; anything else is
    // folded with an OR of XORs that never branches on the data.
    if (left.size() == 16U) {
        return crypto_verify16(left.data(), right.data()) == 0;
    }
    if (left.size() == 32U) {
        return crypto_verify32(left.data(), right.data()) == 0;
    }
    if (left.size() == 64U) {
        return crypto_verify64(left.data(), right.data()) == 0;
    }
    volatile std::uint8_t difference = 0U;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        difference = static_cast<std::uint8_t>(difference | (left[index] ^ right[index]));
    }
    return difference == 0U;
}

void Ed25519KeyPairFromSeed(const Ed25519Seed& seed, Ed25519SecretKey& secretKey, Ed25519PublicKey& publicKey) noexcept {
    // crypto_ed25519_key_pair wipes the seed it is handed, so it gets a copy.
    Ed25519Seed scratch = seed;
    crypto_ed25519_key_pair(secretKey.data(), publicKey.data(), scratch.data());
}

bool GenerateEd25519Seed(Ed25519Seed& seed) noexcept {
    return SecureRandom(seed.Span());
}

Ed25519Signature Ed25519Sign(const Ed25519SecretKey& secretKey, std::span<const std::uint8_t> message) noexcept {
    Ed25519Signature signature{};
    crypto_ed25519_sign(signature.data(), secretKey.data(), message.data(), message.size());
    return signature;
}

bool Ed25519Verify(
    const Ed25519Signature& signature,
    const Ed25519PublicKey& publicKey,
    std::span<const std::uint8_t> message) noexcept {
    return crypto_ed25519_check(signature.data(), publicKey.data(), message.data(), message.size()) == 0;
}

Sha512Digest Sha512(std::span<const std::uint8_t> bytes) noexcept {
    Sha512Digest digest{};
    crypto_sha512(digest.data(), bytes.data(), bytes.size());
    return digest;
}

Sha512Hasher::Sha512Hasher() noexcept {
    crypto_sha512_init(As<crypto_sha512_ctx>(state_));
}

Sha512Hasher::~Sha512Hasher() {
    SecureWipe(state_);
}

void Sha512Hasher::Update(std::span<const std::uint8_t> bytes) noexcept {
    crypto_sha512_update(As<crypto_sha512_ctx>(state_), bytes.data(), bytes.size());
}

Sha512Digest Sha512Hasher::Finish() noexcept {
    Sha512Digest digest{};
    crypto_sha512_final(As<crypto_sha512_ctx>(state_), digest.data());
    return digest;
}

Sha512Digest HmacSha512(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message) noexcept {
    Sha512Digest mac{};
    crypto_sha512_hmac(mac.data(), key.data(), key.size(), message.data(), message.size());
    return mac;
}

HmacSha512Hasher::HmacSha512Hasher(std::span<const std::uint8_t> key) noexcept {
    crypto_sha512_hmac_init(As<crypto_sha512_hmac_ctx>(state_), key.data(), key.size());
}

HmacSha512Hasher::~HmacSha512Hasher() {
    SecureWipe(state_);
}

void HmacSha512Hasher::Update(std::span<const std::uint8_t> bytes) noexcept {
    crypto_sha512_hmac_update(As<crypto_sha512_hmac_ctx>(state_), bytes.data(), bytes.size());
}

Sha512Digest HmacSha512Hasher::Finish() noexcept {
    Sha512Digest mac{};
    crypto_sha512_hmac_final(As<crypto_sha512_hmac_ctx>(state_), mac.data());
    return mac;
}

void HkdfSha512(
    std::span<std::uint8_t> out,
    std::span<const std::uint8_t> inputKeyMaterial,
    std::span<const std::uint8_t> salt,
    std::span<const std::uint8_t> info) noexcept {
    crypto_sha512_hkdf(
        out.data(), out.size(),
        inputKeyMaterial.data(), inputKeyMaterial.size(),
        salt.data(), salt.size(),
        info.data(), info.size());
}

void AeadEncryptInPlace(
    std::span<std::uint8_t> bytes,
    AeadTag& tag,
    const AeadKey& key,
    const AeadNonce& nonce,
    std::span<const std::uint8_t> associatedData) noexcept {
    crypto_aead_lock(
        bytes.data(), tag.data(), key.data(), nonce.data(),
        associatedData.data(), associatedData.size(),
        bytes.data(), bytes.size());
}

bool AeadDecryptInPlace(
    std::span<std::uint8_t> bytes,
    const AeadTag& tag,
    const AeadKey& key,
    const AeadNonce& nonce,
    std::span<const std::uint8_t> associatedData) noexcept {
    return crypto_aead_unlock(
               bytes.data(), tag.data(), key.data(), nonce.data(),
               associatedData.data(), associatedData.size(),
               bytes.data(), bytes.size()) == 0;
}

std::string ToHex(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2U);
    for (const std::uint8_t byte : bytes) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

bool TryParseHex(std::string_view text, std::span<std::uint8_t> out) noexcept {
    if (text.size() != out.size() * 2U) {
        return false;
    }
    for (std::size_t index = 0U; index < out.size(); ++index) {
        const int high = HexValue(text[index * 2U]);
        const int low = HexValue(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            SecureWipe(out);
            return false;
        }
        out[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

} // namespace kb::security
