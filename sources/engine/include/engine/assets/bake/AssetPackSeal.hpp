#pragma once

#include "engine/assets/bake/AssetPack.hpp"
#include "engine/security/Crypto.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::security {
struct ReleaseSigningKey;
}

// The SEAL of an asset pack: an Ed25519 signature over the pack's catalogue and over a SHA-512
// of every block, appended after the bytes the pack header accounts for.
//
//     +0                    the unsigned pack, exactly header.fileBytes long, untouched
//     +header.fileBytes     seal: fixed part, one entry per block (ascending offset), signature
//
// The signature covers the raw 256-byte header, the artifact index, the fragment index and the
// seal itself, so a reader checks it at mount from bytes it reads anyway -- before it decodes a
// single index entry. Each block's SHA-512 is checked whenever that block is read, so a mount
// never has to stream the whole pack and a block is never hashed twice: a modified byte anywhere
// a reader can reach is refused, at mount for the catalogue and at read for a payload.
//
// Sealing can also ENCRYPT every block in place with XChaCha20-Poly1305. The nonce is the seal's
// random salt followed by the block's offset, so it is unique per block and per seal; the block's
// Poly1305 tag is kept in its seal entry, which leaves every offset and length of the unsigned
// layout unchanged. The digest in the entry is of the stored (encrypted) bytes, so tampering is
// refused before anything is decrypted. The index stays readable: it carries only names, sizes
// and digests, and the browser host fetches it with one range request.
//
// A COMPRESSED block is sealed as stored: compressed first, then encrypted, and the SHA-512 is of
// that result. A reader checks the digest, decrypts and only then decompresses, so the decoder
// never sees a byte the signature does not cover.
namespace kb::assets::bake {

inline constexpr std::string_view kAssetPackSealMagic = "21KBSEAL";
inline constexpr std::uint32_t kAssetPackSealVersion = 1U;
inline constexpr std::uint64_t kAssetPackSealFixedBytes = 96U;
inline constexpr std::uint64_t kAssetPackSealEntryBytes = 8U + kb::security::kSha512Bytes + kb::security::kAeadTagBytes;
// Room for an entry per block of the largest index a reader accepts.
inline constexpr std::uint64_t kMaxAssetPackSealBytes = 128ULL * 1024ULL * 1024ULL;

struct AssetPackSealEntry {
    std::uint64_t offset = 0U;
    kb::security::Sha512Digest storedDigest{};
    kb::security::AeadTag tag{};
};

struct AssetPackSeal {
    bool encrypted = false;
    kb::security::Ed25519PublicKey signer{};
    std::array<std::uint8_t, 16U> salt{};
    // Identifies the content key without revealing it, so a reader holding the wrong key says
    // so at mount instead of failing on the first block.
    std::array<std::uint8_t, 16U> contentKeyId{};
    std::vector<AssetPackSealEntry> entries;
    kb::security::Ed25519Signature signature{};
};

// What a reader demands of a pack at mount.
struct AssetPackTrust {
    // When set, the pack must be sealed by exactly this key: an unsigned pack is Unsigned, a
    // pack sealed by any other key is UntrustedSigner. When unset, a seal is still verified
    // against the key it names, which proves it intact but not who made it.
    std::optional<kb::security::Ed25519PublicKey> requiredSigner;
    // Decrypts the blocks of an encrypted pack.
    std::optional<kb::security::AeadKey> contentKey;
};

[[nodiscard]] std::vector<std::uint8_t> EncodeAssetPackSeal(const AssetPackSeal& seal);
// Refuses a seal whose length is not exactly what its entry count implies, whose entries are
// not strictly ascending, or that carries a reserved bit.
[[nodiscard]] AssetPackReadStatus DecodeAssetPackSeal(std::span<const std::uint8_t> bytes, AssetPackSeal& out);

// The 64-byte message the seal's signature is made over.
[[nodiscard]] kb::security::Sha512Digest AssetPackSealMessage(
    std::span<const std::uint8_t> header,
    std::span<const std::uint8_t> artifactIndex,
    std::span<const std::uint8_t> fragmentIndex,
    std::span<const std::uint8_t> sealWithoutSignature);

[[nodiscard]] std::array<std::uint8_t, 16U> AssetPackContentKeyId(const kb::security::AeadKey& key);

// A pack's content key, wrapped under the content key of the release that ships it: a random
// nonce, the encrypted key and its Poly1305 tag. A release's trust anchor carries one content
// key; packs encrypted under another key -- the packs of an earlier release that a patch release
// ships again -- reach the player as wrapped keys in the pack set index, so every release keeps
// a fresh anchor key and every pack keeps the key it was sealed with. The wrapped key says
// nothing about which pack it belongs to: the reader compares the unwrapped key's id with the
// one the pack's signed seal records.
inline constexpr std::size_t kWrappedAssetPackKeyBytes =
    kb::security::kAeadNonceBytes + kb::security::kAeadKeyBytes + kb::security::kAeadTagBytes;
using WrappedAssetPackKey = std::array<std::uint8_t, kWrappedAssetPackKeyBytes>;

// False only when the system random number generator is unavailable.
[[nodiscard]] bool WrapAssetPackContentKey(
    const kb::security::AeadKey& releaseKey,
    const kb::security::AeadKey& packKey,
    WrappedAssetPackKey& out);
// False when `wrapped` was not made under `releaseKey` or was altered.
[[nodiscard]] bool UnwrapAssetPackContentKey(
    const kb::security::AeadKey& releaseKey,
    const WrappedAssetPackKey& wrapped,
    kb::security::AeadKey& out);
[[nodiscard]] kb::security::AeadNonce AssetPackBlockNonce(
    const std::array<std::uint8_t, 16U>& salt, std::uint64_t blockOffset) noexcept;

// The message the seal of the pack at `path` signs (AssetPackReader::SealDigest without mounting
// the pack, so it works without the content key of an encrypted pack). The seal's signature is
// checked against the key the seal names; false for an unsigned or damaged pack.
[[nodiscard]] bool ReadAssetPackSealDigest(
    const std::filesystem::path& path,
    kb::security::Sha512Digest& digest,
    std::string& error);
// The same, with the decoded seal: whether the pack is encrypted and the id of its content key.
[[nodiscard]] bool ReadAssetPackSeal(
    const std::filesystem::path& path,
    AssetPackSeal& seal,
    kb::security::Sha512Digest& digest,
    std::string& error);

// Seals the unsigned pack at `path`: verifies every block against its index digest, encrypts
// the blocks when `contentKey` is given, appends the signed seal, and replaces the file only
// once the sealed copy is complete. A pack that is already sealed is refused.
[[nodiscard]] bool SealAssetPack(
    const std::filesystem::path& path,
    const kb::security::ReleaseSigningKey& key,
    const kb::security::AeadKey* contentKey,
    std::string& error);

} // namespace kb::assets::bake
