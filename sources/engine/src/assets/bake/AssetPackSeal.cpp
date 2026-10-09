#include "engine/assets/bake/AssetPackSeal.hpp"

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>

namespace kb::assets::bake {
namespace {

constexpr std::uint32_t kSealEncrypted = 1U << 0U;
// The domain includes its terminating NUL, which separates it from what follows it.
constexpr std::string_view kSealSignatureDomain{ "21KB-PACK-SEAL-V1\0", 18U };
constexpr std::string_view kContentKeyIdDomain = "21KB-PACK-CONTENT-KEY-ID";

void PutUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PutUInt64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint32_t GetUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t GetUInt64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

template <std::size_t Size>
void Append(std::vector<std::uint8_t>& bytes, const std::array<std::uint8_t, Size>& value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
}

template <std::size_t Size>
void Extract(std::span<const std::uint8_t> bytes, std::size_t offset, std::array<std::uint8_t, Size>& out) noexcept {
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), Size, out.begin());
}

[[nodiscard]] bool ReadExact(std::istream& stream, std::uint64_t offset, std::uint64_t bytes, std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(bytes), 0U);
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return stream.gcount() == static_cast<std::streamsize>(bytes);
}

} // namespace

std::vector<std::uint8_t> EncodeAssetPackSeal(const AssetPackSeal& seal) {
    std::vector<std::uint8_t> bytes(kAssetPackSealMagic.begin(), kAssetPackSealMagic.end());
    PutUInt32(bytes, kAssetPackSealVersion);
    PutUInt32(bytes, seal.encrypted ? kSealEncrypted : 0U);
    PutUInt64(bytes, kAssetPackSealFixedBytes + seal.entries.size() * kAssetPackSealEntryBytes +
            kb::security::kEd25519SignatureBytes);
    PutUInt32(bytes, static_cast<std::uint32_t>(seal.entries.size()));
    PutUInt32(bytes, 0U);
    Append(bytes, seal.signer);
    Append(bytes, seal.salt);
    Append(bytes, seal.contentKeyId);
    for (const AssetPackSealEntry& entry : seal.entries) {
        PutUInt64(bytes, entry.offset);
        Append(bytes, entry.storedDigest);
        Append(bytes, entry.tag);
    }
    Append(bytes, seal.signature);
    return bytes;
}

AssetPackReadStatus DecodeAssetPackSeal(std::span<const std::uint8_t> bytes, AssetPackSeal& out) {
    constexpr std::uint64_t kMinimumBytes = kAssetPackSealFixedBytes + kb::security::kEd25519SignatureBytes;
    if (bytes.size() < kMinimumBytes || bytes.size() > kMaxAssetPackSealBytes ||
        std::memcmp(bytes.data(), kAssetPackSealMagic.data(), kAssetPackSealMagic.size()) != 0) {
        return AssetPackReadStatus::SealCorrupt;
    }
    const std::uint32_t flags = GetUInt32(bytes, 12U);
    const std::uint64_t sealBytes = GetUInt64(bytes, 16U);
    const std::uint32_t entryCount = GetUInt32(bytes, 24U);
    if (GetUInt32(bytes, 8U) != kAssetPackSealVersion || (flags & ~kSealEncrypted) != 0U ||
        GetUInt32(bytes, 28U) != 0U || sealBytes != bytes.size() ||
        sealBytes != kMinimumBytes + static_cast<std::uint64_t>(entryCount) * kAssetPackSealEntryBytes) {
        return AssetPackReadStatus::SealCorrupt;
    }
    AssetPackSeal seal{};
    seal.encrypted = (flags & kSealEncrypted) != 0U;
    Extract(bytes, 32U, seal.signer);
    Extract(bytes, 64U, seal.salt);
    Extract(bytes, 80U, seal.contentKeyId);
    seal.entries.resize(entryCount);
    std::size_t offset = static_cast<std::size_t>(kAssetPackSealFixedBytes);
    for (std::uint32_t index = 0U; index < entryCount; ++index) {
        AssetPackSealEntry& entry = seal.entries[index];
        entry.offset = GetUInt64(bytes, offset);
        Extract(bytes, offset + 8U, entry.storedDigest);
        Extract(bytes, offset + 8U + kb::security::kSha512Bytes, entry.tag);
        offset += static_cast<std::size_t>(kAssetPackSealEntryBytes);
        if (index != 0U && entry.offset <= seal.entries[index - 1U].offset) {
            return AssetPackReadStatus::SealCorrupt;
        }
    }
    Extract(bytes, offset, seal.signature);
    out = std::move(seal);
    return AssetPackReadStatus::Success;
}

kb::security::Sha512Digest AssetPackSealMessage(
    std::span<const std::uint8_t> header,
    std::span<const std::uint8_t> artifactIndex,
    std::span<const std::uint8_t> fragmentIndex,
    std::span<const std::uint8_t> sealWithoutSignature) {
    kb::security::Sha512Hasher hasher;
    hasher.Update(std::span{ reinterpret_cast<const std::uint8_t*>(kSealSignatureDomain.data()), kSealSignatureDomain.size() });
    hasher.Update(header);
    hasher.Update(artifactIndex);
    hasher.Update(fragmentIndex);
    hasher.Update(sealWithoutSignature);
    return hasher.Finish();
}

std::array<std::uint8_t, 16U> AssetPackContentKeyId(const kb::security::AeadKey& key) {
    const kb::security::Sha512Digest mac = kb::security::HmacSha512(
        key.Span(),
        std::span{ reinterpret_cast<const std::uint8_t*>(kContentKeyIdDomain.data()), kContentKeyIdDomain.size() });
    std::array<std::uint8_t, 16U> id{};
    std::copy_n(mac.begin(), id.size(), id.begin());
    return id;
}

kb::security::AeadNonce AssetPackBlockNonce(const std::array<std::uint8_t, 16U>& salt, std::uint64_t blockOffset) noexcept {
    kb::security::AeadNonce nonce{};
    std::copy(salt.begin(), salt.end(), nonce.begin());
    for (std::size_t index = 0U; index < 8U; ++index) {
        nonce[16U + index] = static_cast<std::uint8_t>((blockOffset >> (index * 8U)) & 0xFFU);
    }
    return nonce;
}

bool ReadAssetPackSealDigest(const std::filesystem::path& path, kb::security::Sha512Digest& digest, std::string& error) {
    std::ifstream raw{ path, std::ios::binary };
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    std::vector<std::uint8_t> headerBytes;
    AssetPackHeader header{};
    if (!raw.is_open() || sizeError || !ReadExact(raw, 0U, kAssetPackHeaderBytes, headerBytes) ||
        DecodeAssetPackHeader(headerBytes, header) != AssetPackReadStatus::Success) {
        error = "not a readable asset pack";
        return false;
    }
    if (size <= header.fileBytes || size - header.fileBytes > kMaxAssetPackSealBytes) {
        error = "asset pack is not signed";
        return false;
    }
    std::vector<std::uint8_t> indexBytes;
    std::vector<std::uint8_t> fragmentBytes;
    std::vector<std::uint8_t> sealBytes;
    AssetPackSeal seal{};
    if (!ReadExact(raw, header.indexOffset, header.indexBytes, indexBytes) ||
        !ReadExact(raw, header.fragmentIndexOffset, header.fragmentIndexBytes, fragmentBytes) ||
        !ReadExact(raw, header.fileBytes, size - header.fileBytes, sealBytes) ||
        DecodeAssetPackSeal(sealBytes, seal) != AssetPackReadStatus::Success) {
        error = "asset pack seal is damaged";
        return false;
    }
    digest = AssetPackSealMessage(headerBytes, indexBytes, fragmentBytes,
        std::span{ sealBytes }.first(sealBytes.size() - kb::security::kEd25519SignatureBytes));
    if (!kb::security::Ed25519Verify(seal.signature, seal.signer, digest)) {
        error = "asset pack seal does not verify";
        return false;
    }
    return true;
}

bool SealAssetPack(
    const std::filesystem::path& path,
    const kb::security::ReleaseSigningKey& key,
    const kb::security::AeadKey* contentKey,
    std::string& error) {
    AssetPackReader reader;
    if (const AssetPackReadStatus status = reader.Mount(path); status != AssetPackReadStatus::Success) {
        error = "asset pack could not be mounted for sealing: " + std::string{ ToString(status) };
        return false;
    }
    if (reader.Seal() != nullptr) {
        error = "asset pack is already sealed";
        return false;
    }
    const AssetPackHeader header = reader.Header();

    struct Located {
        std::uint64_t offset = 0U;
        const AssetPackArtifactEntry* artifact = nullptr;
        const AssetPackBlockEntry* block = nullptr;
    };
    std::vector<Located> blocks;
    for (const AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const AssetPackBlockEntry& block : artifact.blocks) {
            blocks.push_back(Located{ block.offset, &artifact, &block });
        }
    }
    std::ranges::sort(blocks, {}, &Located::offset);

    std::vector<std::uint8_t> headerBytes;
    std::vector<std::uint8_t> indexBytes;
    std::vector<std::uint8_t> fragmentBytes;
    {
        std::ifstream raw{ path, std::ios::binary };
        if (!raw.is_open() || !ReadExact(raw, 0U, kAssetPackHeaderBytes, headerBytes) ||
            !ReadExact(raw, header.indexOffset, header.indexBytes, indexBytes) ||
            !ReadExact(raw, header.fragmentIndexOffset, header.fragmentIndexBytes, fragmentBytes)) {
            error = "asset pack catalogue could not be read for sealing";
            return false;
        }
    }

    AssetPackSeal seal{};
    seal.encrypted = contentKey != nullptr;
    seal.signer = key.publicKey;
    if (seal.encrypted) {
        if (!kb::security::SecureRandom(seal.salt)) {
            error = "the system random number generator is unavailable";
            return false;
        }
        seal.contentKeyId = AssetPackContentKeyId(*contentKey);
    }

    std::filesystem::path sealing = path;
    sealing += ".sealing";
    std::error_code fileError;
    std::filesystem::copy_file(path, sealing, std::filesystem::copy_options::overwrite_existing, fileError);
    if (fileError) {
        error = "asset pack could not be copied for sealing: " + fileError.message();
        return false;
    }
    const auto abandon = [&sealing](std::string& message, std::string text) {
        std::error_code removeError;
        std::filesystem::remove(sealing, removeError);
        message = std::move(text);
        return false;
    };
    {
        std::fstream output{ sealing, std::ios::binary | std::ios::in | std::ios::out };
        if (!output.is_open()) {
            return abandon(error, "sealed asset pack could not be opened for writing");
        }
        seal.entries.reserve(blocks.size());
        std::vector<std::uint8_t> bytes;
        for (const Located& located : blocks) {
            // ReadStoredBlock decodes the block and checks the index digest, so only content the
            // baker vouched for is signed -- and what is signed is the block as stored, which for
            // a compressed block is the compressed frame.
            if (const AssetPackReadStatus status = reader.ReadStoredBlock(*located.artifact, located.block->name, bytes);
                status != AssetPackReadStatus::Success) {
                return abandon(error, "asset pack block failed verification before sealing: " + std::string{ ToString(status) });
            }
            AssetPackSealEntry entry{};
            entry.offset = located.offset;
            if (seal.encrypted) {
                kb::security::AeadEncryptInPlace(bytes, entry.tag, *contentKey, AssetPackBlockNonce(seal.salt, entry.offset), {});
                output.seekp(static_cast<std::streamoff>(entry.offset), std::ios::beg);
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            entry.storedDigest = kb::security::Sha512(bytes);
            seal.entries.push_back(entry);
        }
        std::vector<std::uint8_t> sealBytes = EncodeAssetPackSeal(seal);
        const std::span<const std::uint8_t> body{ sealBytes.data(), sealBytes.size() - kb::security::kEd25519SignatureBytes };
        const kb::security::Sha512Digest message = AssetPackSealMessage(headerBytes, indexBytes, fragmentBytes, body);
        seal.signature = kb::security::Ed25519Sign(key.secretKey, message);
        std::copy(seal.signature.begin(), seal.signature.end(), sealBytes.end() - static_cast<std::ptrdiff_t>(seal.signature.size()));
        output.seekp(static_cast<std::streamoff>(header.fileBytes), std::ios::beg);
        output.write(reinterpret_cast<const char*>(sealBytes.data()), static_cast<std::streamsize>(sealBytes.size()));
        output.flush();
        if (!output.good()) {
            return abandon(error, "sealed asset pack could not be written");
        }
    }
    reader.Unmount();
    std::filesystem::rename(sealing, path, fileError);
    if (fileError) {
        return abandon(error, "sealed asset pack could not replace the unsigned pack: " + fileError.message());
    }
    return true;
}

} // namespace kb::assets::bake
