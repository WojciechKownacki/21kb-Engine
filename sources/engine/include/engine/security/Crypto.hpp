#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// The engine's one door to cryptography. Every primitive behind it is Monocypher's (an audited,
// constant-time C library fetched at a pinned hash by the root CMakeLists.txt); nothing here
// implements a primitive, and nothing else in the engine includes Monocypher directly. A caller
// that needs signing, hashing, authentication or encryption uses these functions so the choice
// of primitive and its parameters lives in exactly one place.
namespace kb::security {

inline constexpr std::size_t kEd25519SeedBytes = 32U;
inline constexpr std::size_t kEd25519PublicKeyBytes = 32U;
inline constexpr std::size_t kEd25519SignatureBytes = 64U;
inline constexpr std::size_t kSha512Bytes = 64U;
inline constexpr std::size_t kAeadKeyBytes = 32U;
inline constexpr std::size_t kAeadNonceBytes = 24U;
inline constexpr std::size_t kAeadTagBytes = 16U;

using Ed25519PublicKey = std::array<std::uint8_t, kEd25519PublicKeyBytes>;
using Ed25519Signature = std::array<std::uint8_t, kEd25519SignatureBytes>;
using Sha512Digest = std::array<std::uint8_t, kSha512Bytes>;
using AeadNonce = std::array<std::uint8_t, kAeadNonceBytes>;
using AeadTag = std::array<std::uint8_t, kAeadTagBytes>;

// Overwrites `bytes` in a way the optimiser may not elide.
void SecureWipe(std::span<std::uint8_t> bytes) noexcept;

// Fixed-size secret that wipes itself whenever its value is discarded.
template <std::size_t Size>
class SecretBytes final {
public:
    SecretBytes() noexcept = default;
    SecretBytes(const SecretBytes& other) noexcept = default;
    SecretBytes& operator=(const SecretBytes& other) noexcept = default;
    ~SecretBytes() {
        SecureWipe(bytes_);
    }

    [[nodiscard]] std::uint8_t* data() noexcept { return bytes_.data(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] static constexpr std::size_t size() noexcept { return Size; }
    [[nodiscard]] std::span<std::uint8_t, Size> Span() noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::uint8_t, Size> Span() const noexcept { return bytes_; }

private:
    std::array<std::uint8_t, Size> bytes_{};
};

// Ed25519 secret key in the expanded form signing uses: the 32-byte seed followed by its
// public key. Only the seed is ever stored.
using Ed25519SecretKey = SecretBytes<64U>;
using Ed25519Seed = SecretBytes<kEd25519SeedBytes>;
using AeadKey = SecretBytes<kAeadKeyBytes>;

// Fills `out` from the operating system's CSPRNG (BCryptGenRandom on Windows, getrandom on
// Linux and Android, getentropy elsewhere). Returns false, with `out` wiped, if the system
// could not supply the bytes; a caller must never fall back to a weaker source.
[[nodiscard]] bool SecureRandom(std::span<std::uint8_t> out) noexcept;

// Equality whose running time depends only on the lengths, never on where the inputs differ.
// Use it for every comparison against a secret or against an expected MAC or digest.
[[nodiscard]] bool ConstantTimeEqual(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) noexcept;

// RFC 8032 Ed25519 (pure, no context).
void Ed25519KeyPairFromSeed(const Ed25519Seed& seed, Ed25519SecretKey& secretKey, Ed25519PublicKey& publicKey) noexcept;
[[nodiscard]] bool GenerateEd25519Seed(Ed25519Seed& seed) noexcept;
[[nodiscard]] Ed25519Signature Ed25519Sign(const Ed25519SecretKey& secretKey, std::span<const std::uint8_t> message) noexcept;
[[nodiscard]] bool Ed25519Verify(
    const Ed25519Signature& signature,
    const Ed25519PublicKey& publicKey,
    std::span<const std::uint8_t> message) noexcept;

// FIPS 180-4 SHA-512, one shot or streamed.
[[nodiscard]] Sha512Digest Sha512(std::span<const std::uint8_t> bytes) noexcept;

class Sha512Hasher final {
public:
    Sha512Hasher() noexcept;
    Sha512Hasher(const Sha512Hasher&) = delete;
    Sha512Hasher& operator=(const Sha512Hasher&) = delete;
    ~Sha512Hasher();

    void Update(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] Sha512Digest Finish() noexcept;

private:
    alignas(8) std::array<std::uint8_t, 256U> state_{};
};

// RFC 2104 HMAC over SHA-512, one shot or streamed.
[[nodiscard]] Sha512Digest HmacSha512(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message) noexcept;

class HmacSha512Hasher final {
public:
    explicit HmacSha512Hasher(std::span<const std::uint8_t> key) noexcept;
    HmacSha512Hasher(const HmacSha512Hasher&) = delete;
    HmacSha512Hasher& operator=(const HmacSha512Hasher&) = delete;
    ~HmacSha512Hasher();

    void Update(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] Sha512Digest Finish() noexcept;

private:
    alignas(8) std::array<std::uint8_t, 384U> state_{};
};

// RFC 5869 HKDF over SHA-512: derives `out.size()` bytes (at most 255 * 64) from input keying
// material, an optional salt and a context label.
void HkdfSha512(
    std::span<std::uint8_t> out,
    std::span<const std::uint8_t> inputKeyMaterial,
    std::span<const std::uint8_t> salt,
    std::span<const std::uint8_t> info) noexcept;

// XChaCha20-Poly1305 authenticated encryption, in place. The 24-byte nonce is large enough to
// be chosen at random or derived from a unique position, but it must never repeat under one
// key. Decryption leaves `bytes` untouched and returns false when the tag does not match.
void AeadEncryptInPlace(
    std::span<std::uint8_t> bytes,
    AeadTag& tag,
    const AeadKey& key,
    const AeadNonce& nonce,
    std::span<const std::uint8_t> associatedData) noexcept;
[[nodiscard]] bool AeadDecryptInPlace(
    std::span<std::uint8_t> bytes,
    const AeadTag& tag,
    const AeadKey& key,
    const AeadNonce& nonce,
    std::span<const std::uint8_t> associatedData) noexcept;

// Lowercase hexadecimal, for key files, manifests and diagnostics.
[[nodiscard]] std::string ToHex(std::span<const std::uint8_t> bytes);
// Parses exactly `out.size()` bytes of hexadecimal (either case); false on anything else.
[[nodiscard]] bool TryParseHex(std::string_view text, std::span<std::uint8_t> out) noexcept;

} // namespace kb::security
