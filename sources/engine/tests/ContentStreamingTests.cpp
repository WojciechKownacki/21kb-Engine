#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/bake/AssetPack.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackSet.hpp"
#include "engine/assets/bake/AssetPackTools.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/assets/streaming/BackgroundLoadService.hpp"
#include "engine/assets/streaming/PackBlockStream.hpp"
#include "engine/assets/streaming/StreamingResidency.hpp"
#include "engine/security/ReleaseKeys.hpp"
#include "engine/security/ReleaseManifest.hpp"

#include "assets/bake/AssetPackCompression.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <process.h>
    #include <winioctl.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace kb::tests {
namespace {

namespace bake = kb::assets::bake;
namespace streaming = kb::assets::streaming;

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::filesystem::path Root() {
#if defined(_WIN32)
    const long long process = static_cast<long long>(_getpid());
#else
    const long long process = static_cast<long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / ("21kb_content_streaming_" + std::to_string(process));
}

void Purge(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

[[nodiscard]] std::vector<std::uint8_t> Noise(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint8_t> bytes(count);
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ULL + 1U;
    for (std::uint8_t& value : bytes) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        value = static_cast<std::uint8_t>((state >> 33U) & 0xFFU);
    }
    return bytes;
}

// Content with the redundancy real assets have: repeated records with a little variation.
[[nodiscard]] std::vector<std::uint8_t> Compressible(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(count);
    std::uint64_t record = seed;
    while (bytes.size() < count) {
        const std::string line = "vertex " + std::to_string(record % 97U) + " 0.5 0.25 1.0 normal 0 1 0\n";
        bytes.insert(bytes.end(), line.begin(), line.end());
        ++record;
    }
    bytes.resize(count);
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] std::span<const std::uint8_t> Bytes(std::string_view text) {
    return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
}

struct TestBlock {
    std::string name;
    bake::BakedAssetBlockResidency residency = bake::BakedAssetBlockResidency::Resident;
    std::vector<std::uint8_t> bytes;
};

struct TestArtifact {
    std::uint64_t seed = 0U;
    std::string assetTypeId = "TestArtifact";
    std::vector<std::uint8_t> primary;
    std::vector<TestBlock> blocks;
};

[[nodiscard]] bake::AssetBakeKey ArtifactKey(const bake::BakeTargetProfile& profile, std::uint64_t seed) {
    bake::AssetBakeKey key{};
    key.sourceContentHash = seed * 0x9E3779B97F4A7C15ULL + 7U;
    key.bakerId = "ContentStreamingTest";
    key.bakerVersion = "1";
    key.targetProfileId = std::string{ profile.identifier };
    key.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
    key.settingsHash = seed;
    return key;
}

void WritePack(
    const std::filesystem::path& path,
    const bake::AssetPackWriterOptions& options,
    const std::vector<TestArtifact>& artifacts) {
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::AssetPackWriter writer{ path, profile, options };
    for (const TestArtifact& artifact : artifacts) {
        Require(writer.BeginAsset(bake::BakedAssetDescriptor{ .key = ArtifactKey(profile, artifact.seed),
                    .assetTypeId = artifact.assetTypeId }) == bake::BakedAssetSinkStatus::Success,
            "A test artifact could not be opened");
        Require(writer.WritePrimaryBlock(artifact.primary, profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "A test primary block was refused");
        for (const TestBlock& block : artifact.blocks) {
            bake::BakedAssetBlock description{};
            description.name = block.name;
            description.residency = block.residency;
            description.alignmentBytes = profile.packageBlockAlignmentBytes;
            Require(writer.WriteAuxiliaryBlock(description, block.bytes) == bake::BakedAssetSinkStatus::Success,
                "A test auxiliary block was refused");
        }
        Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "A test artifact could not be committed");
    }
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "A test pack could not be published");
}

[[nodiscard]] kb::security::ReleaseSigningKey NewKey() {
    kb::security::ReleaseSigningKey key;
    Require(kb::security::GenerateReleaseSigningKey(key), "A test release key could not be generated");
    return key;
}

[[nodiscard]] bake::AssetPackTrust TrustOnly(const kb::security::ReleaseSigningKey& key) {
    bake::AssetPackTrust trust{};
    trust.requiredSigner = key.publicKey;
    return trust;
}

void Seal(const std::filesystem::path& path, const kb::security::ReleaseSigningKey& key,
    const kb::security::AeadKey* contentKey = nullptr) {
    std::string error;
    {
        const bool succeeded = bake::SealAssetPack(path, key, contentKey, error);
        Require(succeeded, error.c_str());
    }
}

// Rewrites the artifact index of an unsealed pack through `edit` and fixes the header checksum,
// so a test reaches the field it means and not the checksum in front of it.
void EditIndex(const std::filesystem::path& path, const std::function<void(std::vector<bake::AssetPackArtifactEntry>&)>& edit) {
    std::vector<std::uint8_t> file = ReadFileBytes(path);
    bake::AssetPackHeader header{};
    Require(bake::DecodeAssetPackHeader(file, header) == bake::AssetPackReadStatus::Success, "The test pack header did not decode");
    const std::span<const std::uint8_t> index{ file.data() + header.indexOffset, static_cast<std::size_t>(header.indexBytes) };
    std::vector<bake::AssetPackArtifactEntry> artifacts;
    Require(bake::DecodeAssetPackIndex(index, header.artifactCount, artifacts) == bake::AssetPackReadStatus::Success,
        "The test pack index did not decode");
    edit(artifacts);
    const std::vector<std::uint8_t> encoded = bake::EncodeAssetPackIndex(artifacts);
    Require(encoded.size() == header.indexBytes, "An index edit changed the index length");
    std::copy(encoded.begin(), encoded.end(), file.begin() + static_cast<std::ptrdiff_t>(header.indexOffset));
    const std::span<const std::uint8_t> fragments{
        file.data() + header.fragmentIndexOffset, static_cast<std::size_t>(header.fragmentIndexBytes) };
    header.indexChecksum = bake::AssetPackIndexChecksum(encoded, header.fragmentCount == 0U ? std::span<const std::uint8_t>{} : fragments);
    const std::vector<std::uint8_t> headerBytes = bake::EncodeAssetPackHeader(header);
    std::copy(headerBytes.begin(), headerBytes.end(), file.begin());
    WriteFileBytes(path, file);
}

[[nodiscard]] const bake::AssetPackBlockEntry& BlockOf(const bake::AssetPackReader& reader, std::string_view name) {
    for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const bake::AssetPackBlockEntry& block : artifact.blocks) {
            if (block.name == name) {
                return block;
            }
        }
    }
    Require(false, "A test block is missing from its pack");
    return reader.Artifacts().front().blocks.front();
}

[[nodiscard]] std::vector<std::uint8_t> ReadNamed(bake::AssetPackReader& reader, std::string_view name,
    bake::AssetPackReadStatus expected = bake::AssetPackReadStatus::Success) {
    for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const bake::AssetPackBlockEntry& block : artifact.blocks) {
            if (block.name == name) {
                std::vector<std::uint8_t> out;
                const bake::AssetPackReadStatus status = reader.ReadBlock(artifact, name, out);
                Require(status == expected, ("Reading block " + std::string{ name } + " answered " +
                    std::string{ bake::ToString(status) }).c_str());
                return out;
            }
        }
    }
    Require(false, "A test block is missing from its pack");
    return {};
}

// Red when: a compressed pack does not hand back every block exactly as it was baked, in any
// order; when a block that does not shrink, or a Mapped block, is stored compressed; or when
// writing the same content twice produces different bytes.
void CompressedBlocksRoundTripWithRandomAccess() {
    const std::filesystem::path root = Root() / "compression";
    Purge(root);
    std::filesystem::create_directories(root);
    std::vector<TestArtifact> artifacts(2U);
    artifacts[0].seed = 1U;
    artifacts[0].primary = Compressible(1U, 96U * 1024U);
    artifacts[0].blocks.push_back(TestBlock{ "mip0", bake::BakedAssetBlockResidency::Streaming, Compressible(2U, 512U * 1024U) });
    artifacts[0].blocks.push_back(TestBlock{ "noise", bake::BakedAssetBlockResidency::Resident, Noise(3U, 64U * 1024U) });
    artifacts[0].blocks.push_back(TestBlock{ "mapped", bake::BakedAssetBlockResidency::Mapped, Compressible(4U, 80U * 1024U) });
    artifacts[0].blocks.push_back(TestBlock{ "tiny", bake::BakedAssetBlockResidency::Resident, Compressible(5U, 40U) });
    artifacts[1].seed = 2U;
    artifacts[1].primary = Compressible(6U, 200U * 1024U);

    bake::AssetPackWriterOptions options{};
    options.compression = bake::AssetPackBlockCompression::Zstd;
    const std::filesystem::path packPath = root / "compressed.kbpack";
    WritePack(packPath, options, artifacts);

    bake::AssetPackReader reader;
    Require(reader.Mount(packPath) == bake::AssetPackReadStatus::Success, "A compressed pack did not mount");
    Require(reader.Header().formatVersion == bake::kAssetPackFormatVersion && reader.Header().role == bake::AssetPackRole::Base,
        "A new pack is not written in the current format as a base pack");
    const auto expectCompression = [&](std::string_view name, bake::AssetPackBlockCompression expected) {
        const bake::AssetPackBlockEntry& block = BlockOf(reader, name);
        Require(block.compression == expected, ("Block " + std::string{ name } + " was stored in the wrong form").c_str());
        if (expected == bake::AssetPackBlockCompression::Zstd) {
            Require(block.storedBytes < block.uncompressedBytes, "A compressed block is not smaller than its payload");
        } else {
            Require(block.storedBytes == block.uncompressedBytes, "An uncompressed block changed length");
        }
    };
    expectCompression(bake::kBakedAssetPrimaryBlockName, bake::AssetPackBlockCompression::Zstd);
    expectCompression("mip0", bake::AssetPackBlockCompression::Zstd);
    expectCompression("noise", bake::AssetPackBlockCompression::None);
    expectCompression("mapped", bake::AssetPackBlockCompression::None);
    expectCompression("tiny", bake::AssetPackBlockCompression::None);

    // Random access: blocks in reverse file order, each decoded on its own.
    for (auto artifact = artifacts.rbegin(); artifact != artifacts.rend(); ++artifact) {
        const bake::AssetPackArtifactEntry* entry = reader.FindArtifact(
            ArtifactKey(bake::WindowsX64BakeTargetProfile(), artifact->seed).Digest());
        Require(entry != nullptr, "A compressed artifact is missing");
        for (auto block = artifact->blocks.rbegin(); block != artifact->blocks.rend(); ++block) {
            std::vector<std::uint8_t> out;
            Require(reader.ReadBlock(*entry, block->name, out) == bake::AssetPackReadStatus::Success && out == block->bytes,
                "A compressed block did not decode to its payload");
        }
        std::vector<std::uint8_t> primary;
        Require(reader.ReadBlock(*entry, bake::kBakedAssetPrimaryBlockName, primary) == bake::AssetPackReadStatus::Success &&
                primary == artifact->primary,
            "A compressed primary block did not decode to its payload");
    }
    std::uint64_t stored = 0U;
    std::uint64_t payload = 0U;
    for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const bake::AssetPackBlockEntry& block : artifact.blocks) {
            stored += block.storedBytes;
            payload += block.uncompressedBytes;
        }
    }
    std::cout << "content-streaming: test content " << payload << " bytes stored as " << stored
              << " bytes (ratio " << static_cast<double>(payload) / static_cast<double>(stored) << ")\n";
    reader.Unmount();

    const std::filesystem::path again = root / "compressed-again.kbpack";
    WritePack(again, options, artifacts);
    Require(ReadFileBytes(packPath) == ReadFileBytes(again), "Compressing the same content twice produced different packs");

    const std::filesystem::path plain = root / "plain.kbpack";
    WritePack(plain, {}, artifacts);
    Require(reader.Mount(plain) == bake::AssetPackReadStatus::Success &&
            BlockOf(reader, "mip0").compression == bake::AssetPackBlockCompression::None,
        "A pack written without compression compressed a block");
    reader.Unmount();

    bake::AssetPackWriterOptions invalid = options;
    invalid.compressionLevel = 40;
    bake::AssetPackWriter refused{ root / "refused.kbpack", bake::WindowsX64BakeTargetProfile(), invalid };
    Require(refused.Finish() == bake::BakedAssetSinkStatus::InvalidPackOptions, "An impossible compression level was accepted");
    Purge(root);
}

// Red when: a modified byte of a compressed block is decoded and handed out, in an unsealed, a
// sealed or an encrypted pack, synchronously or through the asynchronous reader; when an index
// may promise more than a block ceiling of decoded bytes; or when a Mapped block may be stored
// compressed.
void TamperedCompressedBlocksAreRefused() {
    const std::filesystem::path root = Root() / "tamper";
    Purge(root);
    std::filesystem::create_directories(root);
    TestArtifact artifact{};
    artifact.seed = 11U;
    artifact.primary = Compressible(11U, 128U * 1024U);
    artifact.blocks.push_back(TestBlock{ "mip0", bake::BakedAssetBlockResidency::Streaming, Compressible(12U, 256U * 1024U) });
    bake::AssetPackWriterOptions options{};
    options.compression = bake::AssetPackBlockCompression::Zstd;
    const std::filesystem::path original = root / "original.kbpack";
    WritePack(original, options, { artifact });
    const std::vector<std::uint8_t> unsealed = ReadFileBytes(original);
    std::uint64_t blockOffset = 0U;
    std::uint64_t blockStored = 0U;
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(original) == bake::AssetPackReadStatus::Success, "The tamper fixture did not mount");
        blockOffset = BlockOf(reader, "mip0").offset;
        blockStored = BlockOf(reader, "mip0").storedBytes;
        Require(BlockOf(reader, "mip0").compression == bake::AssetPackBlockCompression::Zstd, "The tamper fixture is not compressed");
    }

    const std::filesystem::path tampered = root / "tampered.kbpack";
    for (const std::uint64_t at : { blockOffset + 5U, blockOffset + blockStored / 2U, blockOffset + blockStored - 1U }) {
        std::vector<std::uint8_t> bytes = unsealed;
        bytes[static_cast<std::size_t>(at)] ^= 0x5AU;
        WriteFileBytes(tampered, bytes);
        bake::AssetPackReader reader;
        Require(reader.Mount(tampered) == bake::AssetPackReadStatus::Success, "A payload byte damaged an unsealed catalogue");
        const std::vector<std::uint8_t> out = ReadNamed(reader, "mip0", bake::AssetPackReadStatus::PayloadCorrupt);
        Require(out.empty(), "A damaged compressed block returned bytes");
    }

    // A frame that decodes to more or fewer bytes than its entry promises is refused, and an
    // entry may not promise more than the block ceiling at all.
    {
        WriteFileBytes(tampered, unsealed);
        EditIndex(tampered, [](std::vector<bake::AssetPackArtifactEntry>& entries) {
            for (bake::AssetPackBlockEntry& block : entries.front().blocks) {
                if (block.name == "mip0") {
                    ++block.uncompressedBytes;
                }
            }
        });
        bake::AssetPackReader reader;
        Require(reader.Mount(tampered) == bake::AssetPackReadStatus::Success, "A consistent but wrong size did not mount");
        static_cast<void>(ReadNamed(reader, "mip0", bake::AssetPackReadStatus::PayloadCorrupt));
    }
    {
        WriteFileBytes(tampered, unsealed);
        EditIndex(tampered, [](std::vector<bake::AssetPackArtifactEntry>& entries) {
            for (bake::AssetPackBlockEntry& block : entries.front().blocks) {
                if (block.name == "mip0") {
                    block.uncompressedBytes = bake::kMaxAssetPackBlockBytes + 1U;
                }
            }
        });
        bake::AssetPackReader reader;
        Require(reader.Mount(tampered) == bake::AssetPackReadStatus::IndexCorrupt,
            "An index promising a block past the ceiling was mounted");
    }
    {
        WriteFileBytes(tampered, unsealed);
        EditIndex(tampered, [](std::vector<bake::AssetPackArtifactEntry>& entries) {
            for (bake::AssetPackBlockEntry& block : entries.front().blocks) {
                if (block.name == "mip0") {
                    block.residency = bake::BakedAssetBlockResidency::Mapped;
                }
            }
        });
        bake::AssetPackReader reader;
        Require(reader.Mount(tampered) == bake::AssetPackReadStatus::IndexCorrupt, "A compressed Mapped block was mounted");
    }
    {
        // Garbage that claims to be a frame of the right size.
        std::vector<std::uint8_t> decoded;
        Require(!bake::DecompressAssetPackBlock(Noise(5U, 4096U), 8192U, decoded) && decoded.empty(),
            "Random bytes decoded as a block");
        std::vector<std::uint8_t> frame;
        Require(bake::CompressAssetPackBlock(Compressible(3U, 9000U), 3, frame), "A test frame did not compress");
        std::vector<std::uint8_t> twoFrames = frame;
        twoFrames.insert(twoFrames.end(), frame.begin(), frame.end());
        Require(!bake::DecompressAssetPackBlock(twoFrames, 9000U, decoded), "A block with trailing bytes decoded");
        Require(bake::DecompressAssetPackBlock(frame, 9000U, decoded) && decoded == Compressible(3U, 9000U),
            "A valid frame did not decode");
    }

    // Sealed: the seal's SHA-512 covers the compressed bytes, so a flip is caught before decoding,
    // by the synchronous and the asynchronous reader alike.
    const kb::security::ReleaseSigningKey key = NewKey();
    const std::filesystem::path sealedPath = root / "sealed.kbpack";
    WriteFileBytes(sealedPath, unsealed);
    Seal(sealedPath, key);
    const std::vector<std::uint8_t> sealed = ReadFileBytes(sealedPath);
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(sealedPath, bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::AssetPackReadStatus::Success,
            "A sealed compressed pack did not mount");
        Require(ReadNamed(reader, "mip0") == artifact.blocks.front().bytes, "A sealed compressed block did not round-trip");
    }
    {
        std::vector<std::uint8_t> bytes = sealed;
        bytes[static_cast<std::size_t>(blockOffset + blockStored / 3U)] ^= 0x01U;
        WriteFileBytes(tampered, bytes);
        bake::AssetPackReader reader;
        Require(reader.Mount(tampered, bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::AssetPackReadStatus::Success,
            "A sealed payload flip damaged the catalogue");
        static_cast<void>(ReadNamed(reader, "mip0", bake::AssetPackReadStatus::PayloadCorrupt));
        reader.Unmount();

        // A bare artifact pack has no runtime manifest; the asynchronous path is exercised
        // through a reader-level location instead.
        bake::AssetPackReader located;
        Require(located.Mount(tampered, bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::AssetPackReadStatus::Success,
            "The tampered sealed pack did not mount for the asynchronous check");
        const bake::AssetPackBlockEntry& block = BlockOf(located, "mip0");
        streaming::BackgroundLoadService io{ {} };
        const streaming::BackgroundRequestHandle request = io.Read(tampered, block.offset, block.storedBytes, 1,
            [&located, &block](std::vector<std::uint8_t>& stored, std::string& error) {
                const bake::AssetPackReadStatus status = located.DecodeStoredBlock(block, stored);
                error = std::string{ bake::ToString(status) };
                return status == bake::AssetPackReadStatus::Success;
            });
        Require(streaming::BackgroundLoadService::WaitUntilDone(request, Clock::now() + std::chrono::seconds{ 30 }),
            "An asynchronous read of a tampered block never finished");
        Require(request->State() == streaming::BackgroundRequestState::Failed && request->Bytes().empty() &&
                request->Error() == "PayloadCorrupt",
            "An asynchronous read handed out a tampered compressed block");
    }

    // Compressed, then encrypted: still one block, still verified before anything is decoded.
    kb::security::AeadKey contentKey{};
    Require(kb::security::SecureRandom(contentKey.Span()), "A content key could not be generated");
    const std::filesystem::path encryptedPath = root / "encrypted.kbpack";
    WriteFileBytes(encryptedPath, unsealed);
    Seal(encryptedPath, key, &contentKey);
    bake::AssetPackTrust trust = TrustOnly(key);
    trust.contentKey = contentKey;
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(encryptedPath, bake::AssetPackAccess::Ranged, trust) == bake::AssetPackReadStatus::Success,
            "An encrypted compressed pack did not mount");
        Require(ReadNamed(reader, "mip0") == artifact.blocks.front().bytes, "An encrypted compressed block did not round-trip");
        Require(ReadNamed(reader, bake::kBakedAssetPrimaryBlockName) == artifact.primary,
            "An encrypted compressed primary block did not round-trip");
    }
    {
        std::vector<std::uint8_t> bytes = ReadFileBytes(encryptedPath);
        bytes[static_cast<std::size_t>(blockOffset + 1U)] ^= 0x80U;
        WriteFileBytes(tampered, bytes);
        bake::AssetPackReader reader;
        Require(reader.Mount(tampered, bake::AssetPackAccess::Ranged, trust) == bake::AssetPackReadStatus::Success,
            "An encrypted payload flip damaged the catalogue");
        static_cast<void>(ReadNamed(reader, "mip0", bake::AssetPackReadStatus::PayloadCorrupt));
    }
    Purge(root);
}

// Red when: a pack in the previous format no longer mounts, or a format-3 header that claims a
// role without what the role needs is mounted.
void PreviousFormatStillMountsAndRolesAreChecked() {
    const std::filesystem::path root = Root() / "format";
    Purge(root);
    std::filesystem::create_directories(root);
    TestArtifact artifact{};
    artifact.seed = 21U;
    artifact.primary = Noise(21U, 4096U);
    const std::filesystem::path path = root / "pack.kbpack";
    WritePack(path, {}, { artifact });
    const std::vector<std::uint8_t> current = ReadFileBytes(path);

    std::vector<std::uint8_t> previous = current;
    previous[8] = 2U;
    WriteFileBytes(path, previous);
    bake::AssetPackReader reader;
    Require(reader.Mount(path) == bake::AssetPackReadStatus::Success && reader.Header().formatVersion == 2U &&
            reader.Header().role == bake::AssetPackRole::Base,
        "A format-2 pack no longer mounts as a base pack");
    Require(ReadNamed(reader, bake::kBakedAssetPrimaryBlockName) == artifact.primary, "A format-2 pack lost a block");
    reader.Unmount();

    std::vector<std::uint8_t> patchWithoutBase = current;
    patchWithoutBase[168] = static_cast<std::uint8_t>(bake::AssetPackRole::Patch);
    WriteFileBytes(path, patchWithoutBase);
    Require(reader.Mount(path) == bake::AssetPackReadStatus::HeaderCorrupt, "A patch naming no base was mounted");
    std::vector<std::uint8_t> unknownRole = current;
    unknownRole[168] = 9U;
    WriteFileBytes(path, unknownRole);
    Require(reader.Mount(path) == bake::AssetPackReadStatus::HeaderCorrupt, "A pack with an unknown role was mounted");
    std::vector<std::uint8_t> future = current;
    future[8] = static_cast<std::uint8_t>(bake::kAssetPackFormatVersion + 1U);
    WriteFileBytes(path, future);
    Require(reader.Mount(path) == bake::AssetPackReadStatus::UnsupportedVersion, "A pack from a future format was mounted");
    Purge(root);
}

#if defined(_WIN32)
[[nodiscard]] bool MakeSparse(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD returned = 0U;
    const bool sparse = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0U, nullptr, 0U, &returned, nullptr) != FALSE;
    static_cast<void>(CloseHandle(file));
    return sparse;
}
#endif

// Red when: a pack whose blocks lie past 4 GiB cannot be mounted by ranges, or a block there is
// read from the wrong place by the synchronous or the asynchronous reader; or when such a pack
// is accepted by a reader that would have to hold it in memory.
void BlocksBeyondFourGigabytesAreAddressed() {
    const std::filesystem::path root = Root() / "large";
    Purge(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "large.kbpack";
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
#if defined(_WIN32)
    if (!MakeSparse(path)) {
        std::cout << "content-streaming: the temporary volume has no sparse files; the >4 GiB pack check was skipped\n";
        Purge(root);
        return;
    }
#endif
    // Laid out by hand: a primary block at the front and a streamed block 5 GiB in. The bytes
    // between are a hole in a sparse file, so the test costs kilobytes of disk, not gigabytes.
    const std::vector<std::uint8_t> primary = Noise(31U, 4096U);
    const std::vector<std::uint8_t> distant = Noise(32U, 1024U * 1024U + 333U);
    const std::uint64_t farOffset = 5ULL * 1024ULL * 1024ULL * 1024ULL + 4096ULL;

    bake::AssetPackArtifactEntry artifact{};
    artifact.key = ArtifactKey(profile, 31U).Digest();
    artifact.assetTypeId = "TestArtifact";
    artifact.blocks.push_back(bake::AssetPackBlockEntry{
        .name = std::string{ bake::kBakedAssetPrimaryBlockName },
        .residency = bake::BakedAssetBlockResidency::Resident,
        .compression = bake::AssetPackBlockCompression::None,
        .alignmentBytes = profile.packageBlockAlignmentBytes,
        .offset = 0U,
        .storedBytes = primary.size(),
        .uncompressedBytes = primary.size(),
        .payloadDigest = bake::HashBakeDigest(primary),
    });
    artifact.blocks.push_back(bake::AssetPackBlockEntry{
        .name = "far",
        .residency = bake::BakedAssetBlockResidency::Streaming,
        .compression = bake::AssetPackBlockCompression::None,
        .alignmentBytes = profile.packageBlockAlignmentBytes,
        .offset = farOffset,
        .storedBytes = distant.size(),
        .uncompressedBytes = distant.size(),
        .payloadDigest = bake::HashBakeDigest(distant),
    });
    std::vector<bake::AssetPackArtifactEntry> artifacts{ artifact };
    const std::uint64_t indexBytes = bake::EncodeAssetPackIndex(artifacts).size();
    std::uint64_t primaryOffset = 0U;
    Require(bake::TryAlignAssetPackOffset(bake::kAssetPackHeaderBytes + indexBytes, profile.packageBlockAlignmentBytes, primaryOffset),
        "The large fixture could not place its primary block");
    artifacts.front().blocks.front().offset = primaryOffset;
    const std::vector<std::uint8_t> index = bake::EncodeAssetPackIndex(artifacts);
    bake::AssetPackHeader header{};
    header.targetProfileId = std::string{ profile.identifier };
    header.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
    header.indexBytes = index.size();
    header.indexChecksum = bake::AssetPackIndexChecksum(index, {});
    header.artifactCount = 1U;
    header.packageBlockAlignmentBytes = profile.packageBlockAlignmentBytes;
    header.mappedBlockAlignmentBytes = profile.mappedBlockAlignmentBytes;
    header.fileBytes = farOffset + distant.size();
    const std::vector<std::uint8_t> headerBytes = bake::EncodeAssetPackHeader(header);
    {
        std::fstream output{ path, std::ios::binary | std::ios::in | std::ios::out };
        Require(output.is_open(), "The large fixture could not be opened");
        output.write(reinterpret_cast<const char*>(headerBytes.data()), static_cast<std::streamsize>(headerBytes.size()));
        output.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));
        output.seekp(static_cast<std::streamoff>(primaryOffset));
        output.write(reinterpret_cast<const char*>(primary.data()), static_cast<std::streamsize>(primary.size()));
        output.seekp(static_cast<std::streamoff>(farOffset));
        output.write(reinterpret_cast<const char*>(distant.data()), static_cast<std::streamsize>(distant.size()));
        Require(output.good(), "The large fixture could not be written");
    }
    Require(std::filesystem::file_size(path) == header.fileBytes, "The large fixture has the wrong length");
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(path) == bake::AssetPackReadStatus::Success, "A pack past 4 GiB did not mount by ranges");
        Require(ReadNamed(reader, "far") == distant, "A block past 4 GiB was read from the wrong place");
        Require(ReadNamed(reader, bake::kBakedAssetPrimaryBlockName) == primary, "The front of a large pack was misread");

        streaming::BackgroundLoadService io{ {} };
        const bake::AssetPackBlockEntry& block = BlockOf(reader, "far");
        const streaming::BackgroundRequestHandle request = io.Read(path, block.offset, block.storedBytes, 0,
            [&reader, &block](std::vector<std::uint8_t>& bytes, std::string& error) {
                const bake::AssetPackReadStatus status = reader.DecodeStoredBlock(block, bytes);
                error = std::string{ bake::ToString(status) };
                return status == bake::AssetPackReadStatus::Success;
            });
        Require(streaming::BackgroundLoadService::WaitUntilDone(request, Clock::now() + std::chrono::seconds{ 60 }) &&
                request->State() == streaming::BackgroundRequestState::Completed && request->Bytes() == distant,
            ("The asynchronous reader misread a block past 4 GiB: " + request->Error()).c_str());
    }
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(path, bake::AssetPackAccess::WholeFile) == bake::AssetPackReadStatus::PackTooLarge,
            "A reader that holds packs in memory accepted a 5 GiB pack");
    }
    Purge(root);
}

// ---- Pack sets --------------------------------------------------------------------------------

struct SetAsset {
    std::string virtualPath;
    std::string type;
    std::string content;
    std::vector<std::string> dependencies;
};

struct SetPack {
    bake::AssetPackRole role = bake::AssetPackRole::Base;
    std::string label;
    std::uint32_t patchLevel = 0U;
    bake::AssetBakeDigest baseIdentity{};
    std::string gameName = "Base";
    std::vector<SetAsset> assets;
    std::vector<std::pair<std::string, std::string>> auxiliaryFiles;
    // Tombstones the pack's manifest carries.
    std::vector<SetAsset> removedAssets;
    std::vector<std::string> removedFiles;
};

[[nodiscard]] kb::assets::AssetId SetAssetId(const SetAsset& asset) {
    return kb::assets::MakeAssetId(asset.virtualPath + ":" + asset.type);
}

void WriteSetPack(const std::filesystem::path& path, const SetPack& description, bool compress = true) {
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::AssetPackWriterOptions options{};
    options.compression = compress ? bake::AssetPackBlockCompression::Zstd : bake::AssetPackBlockCompression::None;
    options.role = description.role;
    options.label = description.label;
    options.patchLevel = description.patchLevel;
    options.baseIdentity = description.baseIdentity;
    bake::AssetPackWriter writer{ path, profile, options };
    const auto store = [&](std::span<const std::uint8_t> bytes, std::string_view type, std::string_view salt) {
        bake::AssetBakeKey key{};
        key.sourceContentHash = bake::HashBakeBytes(bytes);
        key.bakerId = "PackSetTest";
        key.bakerVersion = "1";
        key.targetProfileId = std::string{ profile.identifier };
        key.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
        key.settingsHash = bake::HashBakeBytes(Bytes(salt));
        Require(writer.BeginAsset({ .key = key, .assetTypeId = std::string{ type } }) == bake::BakedAssetSinkStatus::Success &&
                writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success &&
                writer.CommitAsset() == bake::BakedAssetSinkStatus::Success,
            "A pack set artifact could not be stored");
        return key.Digest();
    };
    bake::RuntimeAssetManifest manifest{};
    manifest.targetProfileId = std::string{ profile.identifier };
    manifest.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
    manifest.partial = description.role != bake::AssetPackRole::Base;
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = "PackSet";
    manifest.settings.gameName = description.gameName;
    manifest.settings.defaultMap = "/Game/Maps/Start.21kbscene";
    for (const SetAsset& asset : description.assets) {
        std::vector<std::uint8_t> blob;
        Require(bake::EncodeRuntimeSourceBlob(Bytes(asset.content), blob), "A pack set source did not encode");
        const bake::AssetBakeDigest digest = store(blob, bake::kSourceAssetTypeId, asset.virtualPath);
        bake::RuntimeAssetManifestEntry entry{};
        entry.id = SetAssetId(asset);
        entry.type = asset.type;
        entry.name = std::filesystem::path{ asset.virtualPath }.stem().string();
        entry.virtualPath = asset.virtualPath;
        entry.sourceExtension = std::filesystem::path{ asset.virtualPath }.extension().string();
        entry.contentHash = bake::HashBakeBytes(Bytes(asset.content));
        for (const std::string& dependency : asset.dependencies) {
            entry.dependencies.push_back(kb::assets::MakeAssetId(dependency));
        }
        entry.artifacts.push_back(bake::RuntimeArtifactReference{ .digest = digest, .encoding = bake::RuntimeArtifactEncoding::SourceBytes });
        manifest.assets.push_back(std::move(entry));
    }
    for (const auto& [virtualPath, content] : description.auxiliaryFiles) {
        std::vector<std::uint8_t> blob;
        Require(bake::EncodeRuntimeSourceBlob(Bytes(content), blob), "A pack set file did not encode");
        manifest.auxiliaryFiles.push_back(bake::RuntimeAuxiliaryFileEntry{
            .virtualPath = virtualPath,
            .contentHash = bake::HashBakeBytes(Bytes(content)),
            .artifactDigest = store(blob, bake::kSourceAssetTypeId, virtualPath),
        });
    }
    for (const SetAsset& removed : description.removedAssets) {
        manifest.removedAssets.push_back(SetAssetId(removed));
    }
    manifest.removedAuxiliaryFiles = description.removedFiles;
    std::vector<std::uint8_t> manifestBytes;
    Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) == bake::RuntimeAssetManifestStatus::Success,
        "A pack set manifest did not encode");
    static_cast<void>(store(manifestBytes, bake::kRuntimeManifestAssetTypeId, description.label));
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "A pack set pack could not be published");
}

[[nodiscard]] bake::AssetBakeDigest IdentityOf(const std::filesystem::path& path) {
    bake::AssetPackReader reader;
    Require(reader.Mount(path) == bake::AssetPackReadStatus::Success, "A pack set member did not mount for its identity");
    return reader.CatalogIdentity();
}

[[nodiscard]] std::string ReadAssetText(bake::RuntimeAssetPack& pack, const SetAsset& asset) {
    bake::RuntimeAssetPayload payload{};
    const bake::RuntimeAssetPackStatus status =
        pack.ReadAssetPayload(SetAssetId(asset), bake::RuntimeArtifactEncoding::SourceBytes, {}, payload);
    Require(status == bake::RuntimeAssetPackStatus::Success && payload.blocks.size() == 1U,
        ("A pack set asset could not be read: " + asset.virtualPath).c_str());
    return { payload.blocks.front().bytes.begin(), payload.blocks.front().bytes.end() };
}

const SetAsset kStartMap{ "/Game/Maps/Start.21kbscene", "Scene", "start map v1", {} };
const SetAsset kRock{ "/Game/Props/Rock.kbdata", "DataTable", "rock v1", {} };
const SetAsset kTree{ "/Game/Props/Tree.kbdata", "DataTable", "tree v1", {} };
const SetAsset kCellProp{ "/Game/Cells/0_0/Prop.kbdata", "DataTable", "cell prop", { "/Game/Props/Rock.kbdata:DataTable" } };
const SetAsset kRockPatched{ "/Game/Props/Rock.kbdata", "DataTable", "rock v2 from patch 1", {} };
const SetAsset kRockPatchedAgain{ "/Game/Props/Rock.kbdata", "DataTable", "rock v3 from patch 2", {} };
const SetAsset kNewInPatch{ "/Game/Props/Lamp.kbdata", "DataTable", "lamp added by patch 1", { "/Game/Props/Tree.kbdata:DataTable" } };

struct PackSetFixture {
    std::filesystem::path root;
    std::filesystem::path base;
    std::filesystem::path chunk;
    std::filesystem::path patch1;
    std::filesystem::path patch2;
    bake::AssetBakeDigest baseIdentity{};
};

[[nodiscard]] PackSetFixture BuildPackSet(const std::filesystem::path& root) {
    PackSetFixture fixture{};
    fixture.root = root;
    fixture.base = root / "Game.kbpack";
    fixture.chunk = root / "Game.cell_0_0.kbpack";
    fixture.patch1 = root / "Game.patch-0001.kbpack";
    fixture.patch2 = root / "Game.patch-0002.kbpack";
    std::filesystem::create_directories(root);
    SetPack base{};
    base.assets = { kStartMap, kRock, kTree };
    base.auxiliaryFiles = { { "/Game/Config/Balance.ini", "health=100" } };
    WriteSetPack(fixture.base, base);
    fixture.baseIdentity = IdentityOf(fixture.base);

    SetPack chunk{};
    chunk.role = bake::AssetPackRole::Chunk;
    chunk.label = "cell_0_0";
    chunk.baseIdentity = fixture.baseIdentity;
    chunk.assets = { kCellProp };
    WriteSetPack(fixture.chunk, chunk);

    SetPack patch1{};
    patch1.role = bake::AssetPackRole::Patch;
    patch1.label = "patch-0001";
    patch1.patchLevel = 1U;
    patch1.baseIdentity = fixture.baseIdentity;
    patch1.gameName = "Patched once";
    patch1.assets = { kRockPatched, kNewInPatch };
    patch1.auxiliaryFiles = { { "/Game/Config/Balance.ini", "health=120" } };
    WriteSetPack(fixture.patch1, patch1);

    SetPack patch2{};
    patch2.role = bake::AssetPackRole::Patch;
    patch2.label = "patch-0002";
    patch2.patchLevel = 2U;
    patch2.baseIdentity = fixture.baseIdentity;
    patch2.gameName = "Patched twice";
    patch2.assets = { kRockPatchedAgain };
    WriteSetPack(fixture.patch2, patch2);
    return fixture;
}

[[nodiscard]] std::vector<bake::RuntimeAssetPackMount> FullSet(const PackSetFixture& fixture) {
    return {
        { .path = fixture.base },
        { .path = fixture.chunk, .role = bake::AssetPackRole::Chunk, .label = "cell_0_0" },
        { .path = fixture.patch1, .role = bake::AssetPackRole::Patch, .label = "patch-0001", .patchLevel = 1U },
        { .path = fixture.patch2, .role = bake::AssetPackRole::Patch, .label = "patch-0002", .patchLevel = 2U },
    };
}

// Red when: a pack set does not read as one catalogue in which chunks add content, each patch
// replaces what it lists in mount order and supplies the project settings, and dependencies
// resolve across packs; or when the index file does not round-trip and order the mount.
void PackSetsMergeChunksAndApplyPatchesInOrder() {
    const std::filesystem::path root = Root() / "set";
    Purge(root);
    const PackSetFixture fixture = BuildPackSet(root);
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();

    bake::RuntimeAssetPack pack;
    const std::vector<bake::RuntimeAssetPackMount> mounts = FullSet(fixture);
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::Success, "A valid pack set did not mount");
    Require(pack.ContainerCount() == 4U, "A pack set did not keep every pack");
    Require(ReadAssetText(pack, kRock) == kRockPatchedAgain.content, "The last patch does not win");
    Require(pack.AssetContainer(SetAssetId(kRock)) == 3U, "A patched asset is not answered by its patch");
    Require(ReadAssetText(pack, kTree) == kTree.content && pack.AssetContainer(SetAssetId(kTree)) == 0U,
        "An unpatched base asset changed");
    Require(ReadAssetText(pack, kCellProp) == kCellProp.content && pack.AssetContainer(SetAssetId(kCellProp)) == 1U,
        "A chunk asset is not answered by its chunk");
    Require(ReadAssetText(pack, kNewInPatch) == kNewInPatch.content, "An asset a patch added is missing");
    Require(pack.FindAsset(kRock.virtualPath) != nullptr && pack.FindAsset(kRock.virtualPath)->contentHash ==
                bake::HashBakeBytes(Bytes(kRockPatchedAgain.content)),
        "Lookup by path does not see the patched entry");
    Require(pack.Manifest().settings.gameName == "Patched twice", "The project settings are not the last patch's");
    Require(pack.Manifest().assets.size() == 5U, "The merged manifest has the wrong number of assets");
    std::vector<std::uint8_t> balance;
    Require(pack.ReadAuxiliaryFile("/Game/Config/Balance.ini", balance) == bake::RuntimeAssetPackStatus::Success &&
            std::string(balance.begin(), balance.end()) == "health=120",
        "A patched auxiliary file is not the patch's");
    for (std::uint32_t container = 1U; container < pack.ContainerCount(); ++container) {
        Require(pack.ContainerHeader(container).baseIdentity == pack.ContainerCatalogIdentity(0U),
            "A set member does not name the mounted base");
    }
    pack.Unmount();

    // The index file: canonical text, strict parse, and the same mount.
    bake::AssetPackSetIndex index{};
    index.packs = {
        { .role = bake::AssetPackRole::Base, .path = "Game.kbpack" },
        { .role = bake::AssetPackRole::Chunk, .label = "cell_0_0", .path = "Game.cell_0_0.kbpack" },
        { .role = bake::AssetPackRole::Patch, .label = "patch-0001", .patchLevel = 1U, .path = "Game.patch-0001.kbpack" },
        { .role = bake::AssetPackRole::Patch, .label = "patch-0002", .patchLevel = 2U, .path = "Game.patch-0002.kbpack" },
    };
    const std::string text = bake::EncodeAssetPackSetIndex(index);
    Require(text == "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n"
                    "patch 1 patch-0001 Game.patch-0001.kbpack\npatch 2 patch-0002 Game.patch-0002.kbpack\n",
        "The pack set index text is not canonical");
    bake::AssetPackSetIndex parsed{};
    Require(bake::ParseAssetPackSetIndex(text, parsed) == bake::AssetPackSetStatus::Success && parsed == index,
        "The pack set index does not round-trip");
    WriteText(root / bake::kAssetPackSetFileName, text);
    Require(pack.MountSetIndex(root / bake::kAssetPackSetFileName, profile) == bake::RuntimeAssetPackStatus::Success &&
            ReadAssetText(pack, kRock) == kRockPatchedAgain.content,
        "A pack set index did not mount its packs in order");
    pack.Unmount();

    // Without the second patch the first one answers: patches really apply in order.
    const std::vector<bake::RuntimeAssetPackMount> onePatch(mounts.begin(), mounts.begin() + 3);
    Require(pack.MountSet(onePatch, profile) == bake::RuntimeAssetPackStatus::Success &&
            ReadAssetText(pack, kRock) == kRockPatched.content && pack.Manifest().settings.gameName == "Patched once",
        "A single patch was not applied");
    pack.Unmount();

    const auto parseStatus = [](std::string_view candidate) {
        bake::AssetPackSetIndex ignored{};
        return bake::ParseAssetPackSetIndex(candidate, ignored);
    };
    Require(parseStatus("21kb-pack-set 1\nchunk c Game.c.kbpack\nbase Game.kbpack\n") == bake::AssetPackSetStatus::OrderInvalid,
        "An index whose base is not first was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\npatch 2 b B.kbpack\npatch 1 a A.kbpack\n") ==
            bake::AssetPackSetStatus::OrderInvalid,
        "An index whose patch levels descend was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\npatch 1 a A.kbpack\nchunk c C.kbpack\n") ==
            bake::AssetPackSetStatus::OrderInvalid,
        "An index with a chunk after a patch was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase ../Game.kbpack\n") == bake::AssetPackSetStatus::Malformed,
        "An index reaching outside its directory was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nchunk a A.kbpack\nchunk a B.kbpack\n") ==
            bake::AssetPackSetStatus::Duplicate,
        "An index naming one label twice was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nchunk a game.KBPACK\n") == bake::AssetPackSetStatus::Duplicate,
        "An index naming one file twice was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\npatch 01 a A.kbpack\n") == bake::AssetPackSetStatus::Malformed,
        "A non-canonical patch level was accepted");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack") == bake::AssetPackSetStatus::Malformed,
        "An index without its final newline was accepted");
    Purge(root);
}

// Red when: a pack set mounts with a member cooked for another base, a member whose sealed header
// disagrees with the index, a chunk that redefines base content, a dependency nothing in the set
// provides, or -- under a release key -- a patch that is unsigned or signed by another key.
void PackSetsRefuseForeignAndInconsistentPacks() {
    const std::filesystem::path root = Root() / "set-refusals";
    Purge(root);
    const PackSetFixture fixture = BuildPackSet(root);
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::RuntimeAssetPack pack;

    // Cooked for another base: same content, other identity.
    const std::filesystem::path otherBase = root / "Other.kbpack";
    SetPack other{};
    other.gameName = "Other";
    other.assets = { kStartMap, kRock, kTree };
    WriteSetPack(otherBase, other);
    std::vector<bake::RuntimeAssetPackMount> mounts = FullSet(fixture);
    mounts.front().path = otherBase;
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::PackSetBaseMismatch && pack.RefusedContainer() == 1U,
        "A chunk cooked for another base was mounted");

    mounts = FullSet(fixture);
    mounts[3].patchLevel = 3U;
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::PackSetInvalid && pack.RefusedContainer() == 3U,
        "A patch whose sealed level disagrees with the index was mounted");
    mounts = FullSet(fixture);
    std::swap(mounts[2], mounts[3]);
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::PackSetInvalid,
        "Patches mounted out of order");
    mounts = FullSet(fixture);
    mounts[1].role = bake::AssetPackRole::Patch;
    mounts[1].patchLevel = 1U;
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::PackSetInvalid, "A chunk was mounted as a patch");
    mounts = FullSet(fixture);
    std::swap(mounts[0], mounts[1]);
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::PackSetInvalid, "A chunk was mounted as the base");

    // A chunk may add content, never redefine it.
    const std::filesystem::path duplicating = root / "Game.duplicate.kbpack";
    SetPack duplicate{};
    duplicate.role = bake::AssetPackRole::Chunk;
    duplicate.label = "duplicate";
    duplicate.baseIdentity = fixture.baseIdentity;
    duplicate.assets = { kTree };
    WriteSetPack(duplicating, duplicate);
    mounts = FullSet(fixture);
    mounts[1] = { .path = duplicating, .role = bake::AssetPackRole::Chunk, .label = "duplicate" };
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::ManifestDuplicate,
        "A chunk redefining a base asset was mounted");

    // Dependencies resolve across the set, and only there.
    const std::filesystem::path dangling = root / "Game.dangling.kbpack";
    SetPack danglingChunk{};
    danglingChunk.role = bake::AssetPackRole::Chunk;
    danglingChunk.label = "dangling";
    danglingChunk.baseIdentity = fixture.baseIdentity;
    danglingChunk.assets = { SetAsset{ "/Game/Cells/9_9/Prop.kbdata", "DataTable", "lost", { "/Game/Nowhere.kbdata:DataTable" } } };
    WriteSetPack(dangling, danglingChunk);
    mounts = FullSet(fixture);
    mounts[1] = { .path = dangling, .role = bake::AssetPackRole::Chunk, .label = "dangling" };
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::DependencyMissing,
        "A chunk depending on content no pack provides was mounted");
    Require(pack.MountSet(std::vector<bake::RuntimeAssetPackMount>{ { .path = fixture.base },
                { .path = fixture.chunk, .role = bake::AssetPackRole::Chunk, .label = "cell_0_0" } },
                profile) == bake::RuntimeAssetPackStatus::Success,
        "A chunk depending on its base did not mount");
    pack.Unmount();

    // Under a release key every member must be sealed by it.
    const kb::security::ReleaseSigningKey key = NewKey();
    const kb::security::ReleaseSigningKey stranger = NewKey();
    for (const std::filesystem::path& member : { fixture.base, fixture.chunk, fixture.patch1 }) {
        Seal(member, key);
    }
    mounts = FullSet(fixture);
    Require(pack.MountSet(mounts, profile, bake::AssetPackAccess::Ranged, TrustOnly(key)) ==
                bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::Unsigned && pack.RefusedContainer() == 3U,
        "An unsigned patch was mounted under a release key");
    Seal(fixture.patch2, stranger);
    Require(pack.MountSet(mounts, profile, bake::AssetPackAccess::Ranged, TrustOnly(key)) ==
                bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::UntrustedSigner && pack.RefusedContainer() == 3U,
        "A patch signed by another key was mounted under a release key");
    // Sealing changes nothing in the catalogue, so members keep naming the sealed base.
    Require(IdentityOf(fixture.base) == fixture.baseIdentity, "Sealing changed a pack's catalogue identity");
    Purge(root);
}

// Red when: the signed release manifest does not bind every pack of a set and its index: a patch
// added after signing, a patch swapped for an older but correctly signed one, an edited index, or
// a whole older release must each be refused.
void ReleaseManifestBindsEveryPackOfASet() {
    const std::filesystem::path root = Root() / "release";
    Purge(root);
    const std::filesystem::path stage = root / "stage";
    const PackSetFixture fixture = BuildPackSet(stage);
    const kb::security::ReleaseSigningKey key = NewKey();
    for (const std::filesystem::path& member : { fixture.base, fixture.chunk, fixture.patch1, fixture.patch2 }) {
        Seal(member, key);
    }
    // An older build of patch 2, correctly signed with the same key: the rollback candidate.
    const std::filesystem::path olderPatch = root / "older-patch-0002.kbpack";
    SetPack older{};
    older.role = bake::AssetPackRole::Patch;
    older.label = "patch-0002";
    older.patchLevel = 2U;
    older.baseIdentity = fixture.baseIdentity;
    older.gameName = "Patched twice";
    older.assets = { SetAsset{ kRock.virtualPath, kRock.type, "rock v3 before a fix", {} } };
    WriteSetPack(olderPatch, older);
    Seal(olderPatch, key);

    const std::string indexText =
        "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n"
        "patch 1 patch-0001 Game.patch-0001.kbpack\npatch 2 patch-0002 Game.patch-0002.kbpack\n";
    WriteText(stage / bake::kAssetPackSetFileName, indexText);
    WriteText(stage / "Game.exe", "player");
    Require(kb::security::IsCriticalReleaseFile(bake::kAssetPackSetFileName), "The pack set index is not a critical release file");

    const auto sign = [&](std::uint64_t number) {
        std::filesystem::remove(stage / kb::security::kReleaseManifestFileName);
        kb::security::ReleaseManifest manifest{};
        manifest.productId = "Publisher.PackSet";
        manifest.contentVersion = "1.0.0";
        manifest.releaseNumber = number;
        manifest.antiRollback = true;
        std::string error;
        {
            const bool succeeded = kb::security::BuildReleaseManifest(stage, manifest, error);
            Require(succeeded, error.c_str());
        }
        WriteText(stage / kb::security::kReleaseManifestFileName, kb::security::SignReleaseManifest(manifest, key));
    };
    sign(7U);
    const std::array<std::filesystem::path, 2U> hashNow{ stage / "Game.exe", stage / bake::kAssetPackSetFileName };
    kb::security::ReleaseVerification verified =
        kb::security::VerifyInstalledRelease(stage, key.publicKey, "Publisher.PackSet", hashNow);
    Require(verified.status == kb::security::ReleaseManifestStatus::Success, "A signed pack set release did not verify");

    bake::RuntimeAssetPack pack;
    Require(pack.MountSetIndex(stage / bake::kAssetPackSetFileName, bake::WindowsX64BakeTargetProfile(),
                bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::RuntimeAssetPackStatus::Success,
        "A sealed pack set did not mount under its release key");
    const auto bound = [&](const kb::security::ReleaseManifest& manifest) {
        for (std::uint32_t container = 0U; container < pack.ContainerCount(); ++container) {
            const std::string relative = pack.ContainerPath(container).lexically_relative(stage).generic_string();
            const kb::security::ReleaseManifestPackSeal* listed = manifest.FindPack(relative);
            if (listed == nullptr || !kb::security::ConstantTimeEqual(listed->sealDigest, pack.ContainerSealDigest(container))) {
                return false;
            }
        }
        return true;
    };
    Require(bound(verified.manifest), "The release manifest does not bind every pack of the set");
    pack.Unmount();

    // The older patch is a valid, sealed pack -- and not this release's.
    const std::vector<std::uint8_t> currentPatch = ReadFileBytes(fixture.patch2);
    std::filesystem::copy_file(olderPatch, fixture.patch2, std::filesystem::copy_options::overwrite_existing);
    Require(pack.MountSetIndex(stage / bake::kAssetPackSetFileName, bake::WindowsX64BakeTargetProfile(),
                bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::RuntimeAssetPackStatus::Success,
        "The rollback candidate is not a valid sealed patch");
    Require(!bound(verified.manifest), "A patch swapped for an older signed build was bound to the release");
    pack.Unmount();
    Require(kb::security::VerifyReleaseDirectory(stage, key.publicKey, "Publisher.PackSet").status ==
            kb::security::ReleaseManifestStatus::FileModified,
        "The full release check missed a swapped patch");
    WriteFileBytes(fixture.patch2, currentPatch);

    // A patch nobody signed into the release.
    std::filesystem::copy_file(olderPatch, stage / "Game.patch-0003.kbpack");
    Require(kb::security::VerifyInstalledRelease(stage, key.publicKey, "Publisher.PackSet", hashNow).status ==
            kb::security::ReleaseManifestStatus::UnlistedFile,
        "An unlisted patch pack was allowed beside the release");
    std::filesystem::remove(stage / "Game.patch-0003.kbpack");

    // The index decides what mounts; an edited one is refused at startup.
    WriteText(stage / bake::kAssetPackSetFileName,
        "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\npatch 1 patch-0001 Game.patch-0001.kbpack\n");
    Require(kb::security::VerifyInstalledRelease(stage, key.publicKey, "Publisher.PackSet", hashNow).status ==
            kb::security::ReleaseManifestStatus::FileModified,
        "An edited pack set index was allowed to drop a patch");
    WriteText(stage / bake::kAssetPackSetFileName, indexText);

    // A whole older release (with older patches) is refused once a newer one ran.
    const std::filesystem::path state = root / "state";
    Require(kb::security::EnforceReleaseAntiRollback(state, verified.manifest) == kb::security::ReleaseManifestStatus::Success,
        "The release history could not be recorded");
    sign(6U);
    verified = kb::security::VerifyInstalledRelease(stage, key.publicKey, "Publisher.PackSet", hashNow);
    Require(verified.status == kb::security::ReleaseManifestStatus::Success &&
            kb::security::EnforceReleaseAntiRollback(state, verified.manifest) == kb::security::ReleaseManifestStatus::RolledBack,
        "An older release of the pack set ran after a newer one");
    Purge(root);
}

// Red when: a cooked pack cannot be split into a base and chunk packs that mount as one set with
// the same content, when a patch cut from a new cook does not carry exactly what changed, or
// when a patch that does not raise the patch level, or changes nothing, is written.
void PackToolsSplitRepackAndPatch() {
    const std::filesystem::path root = Root() / "tools";
    Purge(root);
    std::filesystem::create_directories(root);
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();

    // One complete cook, uncompressed, as an older cooker wrote it.
    const std::filesystem::path cook = root / "cook.kbpack";
    SetPack full{};
    full.assets = { kStartMap, kRock, kTree, kCellProp };
    full.auxiliaryFiles = { { "/Game/Config/Balance.ini", "health=100" } };
    WriteSetPack(cook, full, false);

    bake::AssetPackWriterOptions compressed{};
    compressed.compression = bake::AssetPackBlockCompression::Zstd;
    bake::AssetPackToolReport repacked{};
    std::string error;
    {
        const bool succeeded = bake::RepackAssetPack(cook, root / "cook.zstd.kbpack", compressed, repacked, error);
        Require(succeeded, error.c_str());
    }
    Require(repacked.compressedBlocks == 0U || repacked.storedBytes < repacked.payloadBytes,
        "Recompressing a pack made it larger");
    {
        bake::RuntimeAssetPack pack;
        Require(pack.Mount(root / "cook.zstd.kbpack", profile) == bake::RuntimeAssetPackStatus::Success &&
                ReadAssetText(pack, kCellProp) == kCellProp.content,
            "A recompressed pack lost content");
    }

    // Split by world cell.
    const std::filesystem::path base = root / "Game.kbpack";
    const std::filesystem::path chunk = root / "Game.cell_0_0.kbpack";
    const std::vector<bake::AssetPackChunkRule> rules{ { .label = "cell_0_0", .virtualPathPrefixes = { "/Game/Cells/0_0/" },
        .output = chunk } };
    bake::AssetPackSplitReport split{};
    {
        const bool succeeded = bake::SplitRuntimeAssetPack(cook, base, rules, bake::AssetPackBlockCompression::Zstd, 9, split, error);
        Require(succeeded, error.c_str());
    }
    Require(split.chunkAssets == std::vector<std::uint64_t>{ 1U }, "The split moved the wrong number of assets");
    {
        bake::RuntimeAssetPack alone;
        Require(alone.Mount(base, profile) == bake::RuntimeAssetPackStatus::Success && alone.FindAsset(kCellProp.virtualPath) == nullptr,
            "The base still carries split-off content");
        alone.Unmount();
        bake::RuntimeAssetPack set;
        Require(set.MountSet(std::vector<bake::RuntimeAssetPackMount>{ { .path = base },
                    { .path = chunk, .role = bake::AssetPackRole::Chunk, .label = "cell_0_0" } },
                    profile) == bake::RuntimeAssetPackStatus::Success &&
                ReadAssetText(set, kCellProp) == kCellProp.content && ReadAssetText(set, kRock) == kRock.content,
            "A split pack set does not read as the original cook");
    }
    const std::vector<bake::AssetPackChunkRule> badRules{ { .label = "map", .virtualPathPrefixes = { "/Game/Maps/" },
        .output = root / "Game.map.kbpack" } };
    Require(!bake::SplitRuntimeAssetPack(cook, root / "bad.kbpack", badRules, bake::AssetPackBlockCompression::None, 9, split, error),
        "The default map was split off its base");
    WriteText(root / bake::kAssetPackSetFileName, "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n");

    // The next cook changes the rock and adds a lamp.
    const std::filesystem::path nextCook = root / "next.kbpack";
    SetPack next = full;
    next.assets = { kStartMap, kRockPatched, kTree, kCellProp, kNewInPatch };
    next.gameName = "Patched once";
    WriteSetPack(nextCook, next);
    bake::AssetPackPatchReport patchReport{};
    bake::AssetPackPatchRequest request{
        .current = root / bake::kAssetPackSetFileName,
        .next = nextCook,
        .output = root / "Game.patch-0001.kbpack",
        .label = "patch-0001",
        .patchLevel = 1U,
    };
    {
        const bool succeeded = bake::BuildAssetPackPatch(request, patchReport, error);
        Require(succeeded, error.c_str());
    }
    Require(patchReport.changedAssets == 1U && patchReport.addedAssets == 1U && patchReport.removedAssets == 0U &&
            patchReport.changedFiles == 0U && patchReport.settingsChanged,
        "The patch does not carry exactly what changed");
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(request.output) == bake::AssetPackReadStatus::Success &&
                reader.Header().role == bake::AssetPackRole::Patch && reader.Header().patchLevel == 1U &&
                reader.Header().baseIdentity == IdentityOf(base),
            "The patch does not name its base and level");
    }
    WriteText(root / bake::kAssetPackSetFileName,
        "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\npatch 1 patch-0001 Game.patch-0001.kbpack\n");
    {
        bake::RuntimeAssetPack set;
        Require(set.MountSetIndex(root / bake::kAssetPackSetFileName, profile) == bake::RuntimeAssetPackStatus::Success &&
                ReadAssetText(set, kRock) == kRockPatched.content && ReadAssetText(set, kNewInPatch) == kNewInPatch.content &&
                ReadAssetText(set, kCellProp) == kCellProp.content && set.Manifest().settings.gameName == "Patched once",
            "Base, chunk and patch do not read as the new cook");
    }
    Require(!bake::BuildAssetPackPatch(request, patchReport, error) && error.find("patch level") != std::string::npos,
        "A patch that does not raise the patch level was written");
    request.patchLevel = 2U;
    request.label = "patch-0002";
    request.output = root / "Game.patch-0002.kbpack";
    Require(!bake::BuildAssetPackPatch(request, patchReport, error) && error.find("changes nothing") != std::string::npos,
        "An empty patch was written");
    Purge(root);
}

// Red when: a patch cannot take an asset or an auxiliary file out of a set -- the removed asset
// must be gone by id and by path, a later patch must be able to bring it back, and the patch tool
// must write a tombstone for what a new cook dropped -- or when tombstones are accepted where they
// cannot be honoured (a base or chunk, a removal of something the set does not hold, a removal
// that strands a dependency), or when a sealed patch whose tombstones were edited mounts.
void PatchesRemoveAssetsThroughSignedTombstones() {
    const std::filesystem::path root = Root() / "tombstones";
    Purge(root);
    std::filesystem::create_directories(root);
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();

    // The manifest encoding: tombstones round-trip in a partial manifest and nowhere else.
    {
        bake::RuntimeAssetManifest manifest{};
        manifest.targetProfileId = std::string{ profile.identifier };
        manifest.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
        manifest.partial = true;
        manifest.descriptor.targetPlatforms = { "Windows" };
        manifest.settings.name = "PackSet";
        manifest.settings.defaultMap = kStartMap.virtualPath;
        manifest.removedAssets = { SetAssetId(kTree), SetAssetId(kRock) };
        manifest.removedAuxiliaryFiles = { "/Game/Config/Balance.ini" };
        std::vector<std::uint8_t> bytes;
        Require(bake::EncodeRuntimeAssetManifest(manifest, bytes) == bake::RuntimeAssetManifestStatus::Success,
            "A manifest with tombstones did not encode");
        bake::RuntimeAssetManifest decoded{};
        Require(bake::DecodeRuntimeAssetManifest(bytes, decoded) == bake::RuntimeAssetManifestStatus::Success &&
                decoded.removedAuxiliaryFiles == manifest.removedAuxiliaryFiles && decoded.removedAssets.size() == 2U &&
                std::ranges::is_sorted(decoded.removedAssets, {}, &kb::assets::AssetId::value),
            "Tombstones did not round-trip");
        // Truncating the tombstone lists leaves a manifest that promises them and holds none.
        Require(bake::DecodeRuntimeAssetManifest(std::span{ bytes }.first(bytes.size() - 1U), decoded) ==
                    bake::RuntimeAssetManifestStatus::Malformed,
            "A truncated tombstone list decoded");
        bake::RuntimeAssetManifest complete = manifest;
        complete.partial = false;
        complete.removedAssets.clear();
        Require(bake::EncodeRuntimeAssetManifest(complete, bytes) == bake::RuntimeAssetManifestStatus::InvalidAsset,
            "A complete manifest carried a tombstone");
        bake::RuntimeAssetManifest contradictory = manifest;
        contradictory.removedAuxiliaryFiles.clear();
        contradictory.auxiliaryFiles.push_back(bake::RuntimeAuxiliaryFileEntry{
            .virtualPath = "/Game/Config/Balance.ini", .contentHash = 1U, .artifactDigest = { 1U, 1U } });
        contradictory.removedAuxiliaryFiles = { "/Game/Config/Balance.ini" };
        Require(bake::EncodeRuntimeAssetManifest(contradictory, bytes) == bake::RuntimeAssetManifestStatus::InvalidAsset,
            "A manifest both removed and listed one file");
        bake::RuntimeAssetManifest duplicate = manifest;
        duplicate.removedAssets.push_back(SetAssetId(kTree));
        Require(bake::EncodeRuntimeAssetManifest(duplicate, bytes) == bake::RuntimeAssetManifestStatus::DuplicateEntry,
            "A manifest removed one asset twice");
    }

    // A set: base, a chunk depending on the rock, a patch removing the tree and a file.
    const PackSetFixture fixture = BuildPackSet(root);
    const std::filesystem::path removal = root / "Game.patch-0003.kbpack";
    SetPack removing{};
    removing.role = bake::AssetPackRole::Patch;
    removing.label = "patch-0003";
    removing.patchLevel = 3U;
    removing.baseIdentity = fixture.baseIdentity;
    removing.gameName = "Tree removed";
    removing.removedAssets = { kTree, kNewInPatch };
    removing.removedFiles = { "/Game/Config/Balance.ini" };
    WriteSetPack(removal, removing);
    std::vector<bake::RuntimeAssetPackMount> mounts = FullSet(fixture);
    mounts.push_back({ .path = removal, .role = bake::AssetPackRole::Patch, .label = "patch-0003", .patchLevel = 3U });
    bake::RuntimeAssetPack pack;
    Require(pack.MountSet(mounts, profile) == bake::RuntimeAssetPackStatus::Success, "A patch with tombstones did not mount");
    std::vector<std::uint8_t> file;
    Require(pack.FindAsset(SetAssetId(kTree)) == nullptr && pack.FindAsset(kTree.virtualPath) == nullptr &&
            pack.FindAsset(SetAssetId(kNewInPatch)) == nullptr &&
            pack.ReadAuxiliaryFile("/Game/Config/Balance.ini", file) != bake::RuntimeAssetPackStatus::Success,
        "A removed asset or file is still visible after its patch mounted");
    Require(ReadAssetText(pack, kRockPatchedAgain) == kRockPatchedAgain.content && ReadAssetText(pack, kCellProp) == kCellProp.content &&
            pack.Manifest().assets.size() == 3U,
        "A patch's tombstones took more than they name");
    pack.Unmount();

    // A later patch may bring a removed asset back.
    const std::filesystem::path restore = root / "Game.patch-0004.kbpack";
    SetPack restoring{};
    restoring.role = bake::AssetPackRole::Patch;
    restoring.label = "patch-0004";
    restoring.patchLevel = 4U;
    restoring.baseIdentity = fixture.baseIdentity;
    restoring.assets = { kTree };
    WriteSetPack(restore, restoring);
    std::vector<bake::RuntimeAssetPackMount> restored = mounts;
    restored.push_back({ .path = restore, .role = bake::AssetPackRole::Patch, .label = "patch-0004", .patchLevel = 4U });
    Require(pack.MountSet(restored, profile) == bake::RuntimeAssetPackStatus::Success && ReadAssetText(pack, kTree) == kTree.content,
        "A later patch could not bring a removed asset back");
    pack.Unmount();

    // Tombstones the set cannot honour.
    const auto refusedWith = [&](const SetPack& description, bake::RuntimeAssetPackStatus expected, const char* message) {
        const std::filesystem::path path = root / ("Game." + description.label + ".kbpack");
        WriteSetPack(path, description);
        std::vector<bake::RuntimeAssetPackMount> candidate = FullSet(fixture);
        const bool chunk = description.role == bake::AssetPackRole::Chunk;
        candidate.insert(chunk ? candidate.begin() + 2 : candidate.end(),
            { .path = path, .role = description.role, .label = description.label, .patchLevel = description.patchLevel });
        Require(pack.MountSet(candidate, profile) == expected, message);
    };
    SetPack unknown = removing;
    unknown.label = "patch-0005";
    unknown.patchLevel = 5U;
    unknown.removedAssets = { SetAsset{ "/Game/Props/Never.kbdata", "DataTable", "", {} } };
    unknown.removedFiles.clear();
    refusedWith(unknown, bake::RuntimeAssetPackStatus::PackSetInvalid, "A tombstone for an asset the set lacks was accepted");
    SetPack stranding = unknown;
    stranding.removedAssets = { kRock };
    refusedWith(stranding, bake::RuntimeAssetPackStatus::DependencyMissing, "A removal that strands a dependency was accepted");
    SetPack chunkRemoving{};
    chunkRemoving.role = bake::AssetPackRole::Chunk;
    chunkRemoving.label = "cell_9_9";
    chunkRemoving.baseIdentity = fixture.baseIdentity;
    chunkRemoving.assets = { SetAsset{ "/Game/Cells/9_9/Prop.kbdata", "DataTable", "far prop", {} } };
    chunkRemoving.removedAssets = { kTree };
    refusedWith(chunkRemoving, bake::RuntimeAssetPackStatus::PackSetInvalid, "A chunk carrying a tombstone was accepted");

    // Sealed: the tombstones are part of the signed manifest block.
    const kb::security::ReleaseSigningKey key = NewKey();
    for (const std::filesystem::path& member : { fixture.base, fixture.chunk, fixture.patch1, fixture.patch2, removal }) {
        Seal(member, key);
    }
    Require(pack.MountSet(mounts, profile, bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::RuntimeAssetPackStatus::Success &&
            pack.FindAsset(kTree.virtualPath) == nullptr,
        "A sealed patch with tombstones did not mount under its release key");
    pack.Unmount();
    std::vector<std::uint8_t> sealed = ReadFileBytes(removal);
    std::uint64_t manifestOffset = 0U;
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(removal) == bake::AssetPackReadStatus::Success, "The sealed removal patch did not mount");
        for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
            if (artifact.assetTypeId == bake::kRuntimeManifestAssetTypeId) {
                manifestOffset = artifact.blocks.front().offset;
            }
        }
    }
    Require(manifestOffset != 0U, "The removal patch has no manifest block");
    sealed[static_cast<std::size_t>(manifestOffset) + 4U] ^= 0x20U;
    WriteFileBytes(removal, sealed);
    Require(pack.MountSet(mounts, profile, bake::AssetPackAccess::Ranged, TrustOnly(key)) != bake::RuntimeAssetPackStatus::Success &&
            pack.RefusedContainer() == 4U,
        "A sealed patch whose manifest block was edited mounted");

    // The patch tool writes the tombstones: the next cook drops the tree and the balance file.
    const std::filesystem::path cook = root / "cook.kbpack";
    SetPack full{};
    full.assets = { kStartMap, kRock, kTree };
    full.auxiliaryFiles = { { "/Game/Config/Balance.ini", "health=100" } };
    WriteSetPack(cook, full);
    const std::filesystem::path nextCook = root / "next.kbpack";
    SetPack next{};
    next.assets = { kStartMap, kRock };
    WriteSetPack(nextCook, next);
    bake::AssetPackPatchReport report{};
    std::string error;
    const bake::AssetPackPatchRequest request{
        .current = cook, .next = nextCook, .output = root / "Game.patch-0010.kbpack", .label = "patch-0010", .patchLevel = 10U };
    {
        const bool succeeded = bake::BuildAssetPackPatch(request, report, error);
        Require(succeeded, error.c_str());
    }
    Require(report.removedAssets == 1U && report.removedFiles == 1U && report.addedAssets == 0U && report.changedAssets == 0U,
        "A patch of a cook that only dropped content does not carry the tombstones");
    Require(pack.MountSet(std::vector<bake::RuntimeAssetPackMount>{ { .path = cook },
                { .path = request.output, .role = bake::AssetPackRole::Patch, .label = "patch-0010", .patchLevel = 10U } },
                profile) == bake::RuntimeAssetPackStatus::Success &&
            pack.FindAsset(kTree.virtualPath) == nullptr && ReadAssetText(pack, kRock) == kRock.content &&
            pack.ReadAuxiliaryFile("/Game/Config/Balance.ini", file) != bake::RuntimeAssetPackStatus::Success,
        "A patch cut from a cook that dropped an asset does not remove it");
    pack.Unmount();
    Purge(root);
}

[[nodiscard]] kb::security::AeadKey NewContentKey() {
    kb::security::AeadKey key{};
    Require(kb::security::SecureRandom(key.Span()), "A test content key could not be generated");
    return key;
}

// Red when: the packs an earlier release encrypted under its own content key cannot be read in a
// patch release whose anchor carries a fresh key -- the index's wrapped keys must open exactly
// their own packs, and the key tool must write them -- or when a key line that was altered, made
// under another release's key, or belongs to another pack is accepted, or a tampered block of a
// re-shipped encrypted pack reads.
void EncryptedPackSetsKeepEachPacksKey() {
    const std::filesystem::path root = Root() / "set-keys";
    Purge(root);
    const PackSetFixture fixture = BuildPackSet(root);
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const kb::security::ReleaseSigningKey signer = NewKey();
    const kb::security::AeadKey oldKey = NewContentKey();
    const kb::security::AeadKey newKey = NewContentKey();
    const kb::security::AeadKey strangerKey = NewContentKey();

    // Wrapping: only the release key that wrapped a pack key unwraps it, and only unaltered.
    bake::WrappedAssetPackKey wrapped{};
    Require(bake::WrapAssetPackContentKey(newKey, oldKey, wrapped), "A pack key could not be wrapped");
    kb::security::AeadKey unwrapped{};
    Require(bake::UnwrapAssetPackContentKey(newKey, wrapped, unwrapped) &&
            bake::AssetPackContentKeyId(unwrapped) == bake::AssetPackContentKeyId(oldKey),
        "A wrapped pack key did not unwrap to itself");
    Require(!bake::UnwrapAssetPackContentKey(strangerKey, wrapped, unwrapped), "A pack key unwrapped under another key");
    bake::WrappedAssetPackKey altered = wrapped;
    altered[30] ^= 0x01U;
    Require(!bake::UnwrapAssetPackContentKey(newKey, altered, unwrapped), "An altered wrapped key unwrapped");

    // Release 1 sealed the base, the chunk and patch 1 under its key; release 2 adds patch 2 under
    // a fresh one.
    for (const std::filesystem::path& member : { fixture.base, fixture.chunk, fixture.patch1 }) {
        Seal(member, signer, &oldKey);
    }
    Seal(fixture.patch2, signer, &newKey);
    const std::filesystem::path index = root / bake::kAssetPackSetFileName;
    const std::string packLines = "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n"
                                  "patch 1 patch-0001 Game.patch-0001.kbpack\npatch 2 patch-0002 Game.patch-0002.kbpack\n";
    WriteText(index, packLines);
    bake::AssetPackTrust trust = TrustOnly(signer);
    trust.contentKey = newKey;
    bake::RuntimeAssetPack pack;
    Require(pack.MountSetIndex(index, profile, bake::AssetPackAccess::Ranged, trust) ==
                bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::ContentKeyMismatch && pack.RefusedContainer() == 0U,
        "Packs of an earlier release opened without their keys");

    // The key tool: nothing to wrap with, nothing known, then the earlier release's key.
    bake::AssetPackSetKeyReport report{};
    std::string error;
    Require(!bake::RewrapAssetPackSetKeys(index, &newKey, {}, report, error) && std::ranges::equal(ReadFileBytes(index), Bytes(packLines)),
        "Keys were written without the key that opens the packs");
    const std::array<kb::security::AeadKey, 1U> known{ oldKey };
    Require(!bake::RewrapAssetPackSetKeys(index, nullptr, known, report, error),
        "Keys were written for a release without a content key");
    {
        const bool succeeded = bake::RewrapAssetPackSetKeys(index, &newKey, known, report, error);
        Require(succeeded, error.c_str());
    }
    Require(report.packs == 4U && report.encryptedPacks == 4U && report.wrappedKeys == 3U,
        "The key tool did not wrap exactly the earlier release's packs");
    bake::AssetPackSetIndex parsed{};
    Require(bake::ReadAssetPackSetIndex(index, parsed) == bake::AssetPackSetStatus::Success &&
            parsed.packs[0].wrappedContentKey.has_value() && parsed.packs[2].wrappedContentKey.has_value() &&
            !parsed.packs[3].wrappedContentKey.has_value() &&
            std::ranges::equal(Bytes(bake::EncodeAssetPackSetIndex(parsed)), ReadFileBytes(index)),
        "The index with key lines does not round-trip");
    Require(pack.MountSetIndex(index, profile, bake::AssetPackAccess::Ranged, trust) == bake::RuntimeAssetPackStatus::Success &&
            ReadAssetText(pack, kTree) == kTree.content && ReadAssetText(pack, kCellProp) == kCellProp.content &&
            ReadAssetText(pack, kNewInPatch) == kNewInPatch.content &&
            ReadAssetText(pack, kRockPatchedAgain) == kRockPatchedAgain.content,
        "A patch release did not read the packs it ships again under their own keys");
    pack.Unmount();
    bake::AssetPackTrust keyless = TrustOnly(signer);
    Require(pack.MountSetIndex(index, profile, bake::AssetPackAccess::Ranged, keyless) ==
                bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::ContentKeyMissing,
        "Wrapped keys were used without the release's key");

    // Key lines that are not the release's own, or not their pack's.
    const auto mountWith = [&](const bake::AssetPackSetIndex& edited) {
        WriteText(index, bake::EncodeAssetPackSetIndex(edited));
        return pack.MountSetIndex(index, profile, bake::AssetPackAccess::Ranged, trust);
    };
    bake::AssetPackSetIndex edited = parsed;
    (*edited.packs[0].wrappedContentKey)[40] ^= 0x01U;
    Require(mountWith(edited) == bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::ContentKeyMismatch && pack.RefusedContainer() == 0U,
        "An altered key line opened its pack");
    edited = parsed;
    Require(bake::WrapAssetPackContentKey(strangerKey, oldKey, *edited.packs[0].wrappedContentKey), "A stranger wrap failed");
    Require(mountWith(edited) == bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::ContentKeyMismatch,
        "A key wrapped under another release's key opened a pack");
    edited = parsed;
    Require(bake::WrapAssetPackContentKey(newKey, strangerKey, *edited.packs[1].wrappedContentKey), "A wrap failed");
    Require(mountWith(edited) == bake::RuntimeAssetPackStatus::ContainerRejected &&
            pack.ContainerStatus() == bake::AssetPackReadStatus::ContentKeyMismatch && pack.RefusedContainer() == 1U,
        "A key that is not its pack's opened the pack");
    edited = parsed;
    edited.packs[3].wrappedContentKey = parsed.packs[0].wrappedContentKey;
    Require(mountWith(edited) == bake::RuntimeAssetPackStatus::ContainerRejected && pack.RefusedContainer() == 3U,
        "Another pack's key line opened the newest patch");

    // The index's own rules for key lines.
    const auto parseStatus = [](std::string_view text) {
        bake::AssetPackSetIndex ignored{};
        return bake::ParseAssetPackSetIndex(text, ignored);
    };
    const std::string keyHex = kb::security::ToHex(*parsed.packs[0].wrappedContentKey);
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nkey " + keyHex + " Game.kbpack\n") == bake::AssetPackSetStatus::Success,
        "A key line for the base did not parse");
    Require(parseStatus("21kb-pack-set 1\nkey " + keyHex + " Game.kbpack\nbase Game.kbpack\n") == bake::AssetPackSetStatus::Malformed,
        "A key line before its pack parsed");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nkey " + keyHex + " Other.kbpack\n") == bake::AssetPackSetStatus::Malformed,
        "A key line for an unlisted pack parsed");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nkey " + keyHex + " Game.kbpack\nkey " + keyHex + " Game.kbpack\n") ==
                bake::AssetPackSetStatus::Duplicate,
        "Two key lines for one pack parsed");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nkey " + keyHex.substr(2U) + " Game.kbpack\n") ==
                bake::AssetPackSetStatus::Malformed,
        "A short key parsed");
    Require(parseStatus("21kb-pack-set 1\nbase Game.kbpack\nkey " + keyHex + " Game.kbpack\nchunk a Game.a.kbpack\n") ==
                bake::AssetPackSetStatus::Malformed,
        "A pack line after a key line parsed");

    // A tampered block of a pack shipped again is refused when it is read.
    WriteText(index, bake::EncodeAssetPackSetIndex(parsed));
    std::vector<std::uint8_t> base = ReadFileBytes(fixture.base);
    {
        bake::AssetPackTrust own = TrustOnly(signer);
        own.contentKey = oldKey;
        bake::AssetPackReader reader;
        Require(reader.Mount(fixture.base, bake::AssetPackAccess::Ranged, own) == bake::AssetPackReadStatus::Success,
            "The encrypted base did not mount under its own key");
        for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
            if (artifact.assetTypeId != bake::kRuntimeManifestAssetTypeId) {
                const bake::AssetPackBlockEntry& block = artifact.blocks.front();
                base[static_cast<std::size_t>(block.offset)] ^= 0x01U;
            }
        }
    }
    WriteFileBytes(fixture.base, base);
    Require(pack.MountSetIndex(index, profile, bake::AssetPackAccess::Ranged, trust) == bake::RuntimeAssetPackStatus::Success,
        "Payload damage stopped the catalogue from mounting");
    bake::RuntimeAssetPayload payload{};
    Require(pack.ReadAssetPayload(SetAssetId(kTree), bake::RuntimeArtifactEncoding::SourceBytes, {}, payload) !=
                bake::RuntimeAssetPackStatus::Success,
        "A tampered encrypted block of a re-shipped pack was read");
    pack.Unmount();
    Purge(root);
}

// Red when: the text form of a world region a chunk takes does not read back as written -- corners,
// layers, the base layer's token, the whole-world default -- or a malformed one is accepted.
void WorldRegionsParseForChunkRules() {
    bake::AssetPackWorldRegion region{};
    std::string error;
    Require(bake::ParseAssetPackWorldRegion("/Game/Worlds/Forest.21kbworld@-2:-3..1:4#night,(base),night", region, error) &&
            region.world == "/Game/Worlds/Forest.21kbworld" && region.min == kb::world::WorldCellCoord{ -2, -3 } &&
            region.max == kb::world::WorldCellCoord{ 1, 4 } && region.dataLayers == std::vector<std::string>{ "", "night" },
        "A world region did not parse as written");
    Require(bake::ParseAssetPackWorldRegion("/Game/Worlds/Forest.21kbworld", region, error) && region.dataLayers.empty() &&
            region.min.x == std::numeric_limits<std::int64_t>::min() && region.max.z == std::numeric_limits<std::int64_t>::max(),
        "A bare world did not name the whole world");
    for (const char* refused : { "Game/Worlds/Forest.21kbworld", "/Game/Worlds/Forest.21kbscene", "/Game/Worlds/Forest.21kbworld@2:0..1:0",
             "/Game/Worlds/Forest.21kbworld@1:0", "/Game/Worlds/Forest.21kbworld@a:0..1:0", "/Game/Worlds/Forest.21kbworld#",
             "/Game/Worlds/Forest.21kbworld#bad layer" }) {
        Require(!bake::ParseAssetPackWorldRegion(refused, region, error), (std::string{ "A malformed world region parsed: " } + refused).c_str());
    }
}

// ---- Asynchronous I/O -------------------------------------------------------------------------

// Red when: queued reads are not served highest priority first, a re-prioritised read keeps its
// old place, a cancelled read runs, reads come back with the wrong bytes at unaligned offsets,
// or a worker does not keep several reads in flight.
void AsyncReadsFollowPriorityAndReturnExactRanges() {
    const std::filesystem::path root = Root() / "io";
    Purge(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "data.bin";
    const std::vector<std::uint8_t> content = Noise(41U, 3U * 1024U * 1024U + 777U);
    WriteFileBytes(path, content);

    streaming::BackgroundLoadService io{ streaming::BackgroundLoadServiceOptions{ .workerCount = 1U, .requestsInFlightPerWorker = 4U } };
    std::mutex orderMutex;
    std::vector<int> order;
    std::atomic<bool> gateOpen{ false };
    std::atomic<bool> gateEntered{ false };
    const streaming::BackgroundRequestHandle gate = io.Read(path, 0U, 16U, 1000,
        [&](std::vector<std::uint8_t>&, std::string&) {
            gateEntered = true;
            const Clock::time_point deadline = Clock::now() + std::chrono::seconds{ 30 };
            while (!gateOpen && Clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            }
            return true;
        });
    const Clock::time_point enterDeadline = Clock::now() + std::chrono::seconds{ 30 };
    while (!gateEntered && Clock::now() < enterDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }
    Require(gateEntered, "The I/O worker never started the gate read");

    const auto tagged = [&](int tag) {
        return [&, tag](std::vector<std::uint8_t>&, std::string&) {
            std::scoped_lock lock{ orderMutex };
            order.push_back(tag);
            return true;
        };
    };
    std::vector<streaming::BackgroundRequestHandle> requests;
    const std::array<int, 6U> priorities{ 1, 5, 3, 10, 2, 4 };
    for (std::size_t index = 0U; index < priorities.size(); ++index) {
        requests.push_back(io.Read(path, index * 4096U, 4096U, priorities[index], tagged(priorities[index])));
    }
    Require(io.QueuedCount() == priorities.size(), "Queued reads are not all waiting behind the gate");
    Require(io.Reprioritize(requests[0], 20), "A queued read could not be re-prioritised");   // 1 -> first
    Require(io.Cancel(requests[4]), "A queued read could not be cancelled");                  // 2 never runs
    Require(!io.Cancel(gate), "A read already in flight was cancelled");
    gateOpen = true;
    for (const streaming::BackgroundRequestHandle& request : requests) {
        Require(streaming::BackgroundLoadService::WaitUntilDone(request, Clock::now() + std::chrono::seconds{ 30 }),
            "A queued read never finished");
    }
    Require(order == std::vector<int>{ 1, 10, 5, 4, 3 }, "Queued reads were not served by priority");
    Require(requests[4]->State() == streaming::BackgroundRequestState::Cancelled, "A cancelled read did not stay cancelled");
    Require(io.Stats().peakInFlight >= 4U, "The worker did not keep several reads in flight");

    // Exact ranges at awkward offsets, through the unbuffered path where the platform has one.
    std::vector<streaming::BackgroundRequestHandle> ranges;
    const std::array<std::pair<std::uint64_t, std::uint64_t>, 6U> windows{ {
        { 0U, 1U }, { 1U, 511U }, { 4095U, 2U }, { 12345U, 99999U }, { content.size() - 3U, 3U }, { 700000U, 1500000U },
    } };
    for (const auto& [offset, length] : windows) {
        ranges.push_back(io.Read(path, offset, length, 0));
    }
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        Require(streaming::BackgroundLoadService::WaitUntilDone(ranges[index], Clock::now() + std::chrono::seconds{ 30 }) &&
                ranges[index]->State() == streaming::BackgroundRequestState::Completed,
            ("An exact-range read failed: " + ranges[index]->Error()).c_str());
        const auto [offset, length] = windows[index];
        Require(std::equal(ranges[index]->Bytes().begin(), ranges[index]->Bytes().end(),
                    content.begin() + static_cast<std::ptrdiff_t>(offset)) &&
                ranges[index]->Bytes().size() == length,
            "An asynchronous read returned the wrong bytes");
    }
    const streaming::BackgroundRequestHandle pastEnd = io.Read(path, content.size() - 10U, 20U, 0);
    Require(streaming::BackgroundLoadService::WaitUntilDone(pastEnd, Clock::now() + std::chrono::seconds{ 30 }) &&
            pastEnd->State() == streaming::BackgroundRequestState::Failed && pastEnd->Bytes().empty(),
        "A read past the end of the file succeeded");
    const streaming::BackgroundRequestHandle missing = io.Read(root / "missing.bin", 0U, 1U, 0);
    Require(streaming::BackgroundLoadService::WaitUntilDone(missing, Clock::now() + std::chrono::seconds{ 30 }) &&
            missing->State() == streaming::BackgroundRequestState::Failed,
        "A read of a missing file succeeded");
#if defined(_WIN32)
    Require(io.Stats().unbufferedReads > 0U, "No read took the unbuffered path on Windows");
#endif
    io.Forget(path);
    Purge(root);
}

// Red when: jobs of a lane overlap or ignore their priority, a blocked job starves reads, more
// jobs run than the job limit allows, a lane's queued jobs survive CancelLane, WaitForLane returns
// while its job still runs, a closed lane accepts work, a failing job reads as completed, or the
// shared service is not one instance that goes away with its last user.
void BackgroundJobsShareThePoolWithReads() {
    const std::filesystem::path root = Root() / "jobs";
    Purge(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "data.bin";
    const std::vector<std::uint8_t> content = Noise(43U, 64U * 1024U);
    WriteFileBytes(path, content);
    const auto spinUntil = [](const std::function<bool()>& done) {
        const Clock::time_point deadline = Clock::now() + std::chrono::seconds{ 30 };
        while (!done() && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        }
        return done();
    };

    {
        // Two workers: one may run jobs, the other is kept for reads.
        streaming::BackgroundLoadService service{ streaming::BackgroundLoadServiceOptions{ .workerCount = 2U, .jobWorkers = 2U } };
        Require(service.WorkerCount() == 2U && service.JobWorkerLimit() == 1U,
            "The job limit did not keep a worker for reads");
        std::atomic<bool> blockedEntered{ false };
        std::atomic<bool> release{ false };
        std::atomic<bool> secondRan{ false };
        const streaming::BackgroundRequestHandle blocked = service.Run([&](std::string&) {
            blockedEntered = true;
            return spinUntil([&] { return release.load(); });
        }, 0);
        Require(spinUntil([&] { return blockedEntered.load(); }), "A job never started");
        const streaming::BackgroundRequestHandle second = service.Run([&](std::string&) {
            secondRan = true;
            return true;
        }, 100);
        const streaming::BackgroundRequestHandle read = service.Read(path, 100U, 4000U, 0);
        Require(streaming::BackgroundLoadService::WaitUntilDone(read, Clock::now() + std::chrono::seconds{ 30 }) &&
                read->State() == streaming::BackgroundRequestState::Completed &&
                std::equal(read->Bytes().begin(), read->Bytes().end(), content.begin() + 100) && read->Bytes().size() == 4000U,
            "A read waited behind a blocked job");
        Require(!secondRan && second->State() == streaming::BackgroundRequestState::Queued,
            "A job ran past the job limit");
        release = true;
        Require(streaming::BackgroundLoadService::WaitUntilDone(second, Clock::now() + std::chrono::seconds{ 30 }) && secondRan &&
                blocked->State() == streaming::BackgroundRequestState::Completed,
            "A queued job did not run once the limit freed");
        Require(service.Stats().peakJobsRunning == 1U && service.Stats().jobsRun == 2U,
            "The job counters do not match the jobs run");

        const streaming::BackgroundRequestHandle failing = service.Run([](std::string& error) {
            error = "decoder refused";
            return false;
        }, 0);
        const streaming::BackgroundRequestHandle throwing = service.Run([](std::string&) -> bool {
            throw std::runtime_error{ "loader threw" };
        }, 0);
        Require(streaming::BackgroundLoadService::WaitUntilDone(failing, Clock::now() + std::chrono::seconds{ 30 }) &&
                failing->State() == streaming::BackgroundRequestState::Failed && failing->Error() == "decoder refused" &&
                streaming::BackgroundLoadService::WaitUntilDone(throwing, Clock::now() + std::chrono::seconds{ 30 }) &&
                throwing->State() == streaming::BackgroundRequestState::Failed && throwing->Error() == "loader threw",
            "A failing job did not report its failure");
    }

    {
        // Four workers, three of which may run jobs: lanes keep their own jobs serial and in
        // priority order while another lane runs beside them.
        const auto service = std::make_shared<streaming::BackgroundLoadService>(
            streaming::BackgroundLoadServiceOptions{ .workerCount = 4U, .jobWorkers = 3U });
        std::mutex orderMutex;
        std::vector<int> order;
        std::atomic<int> laneRunning{ 0 };
        std::atomic<bool> overlapped{ false };
        std::atomic<bool> gateEntered{ false };
        std::atomic<bool> gateOpen{ false };
        auto lane = std::make_unique<streaming::BackgroundLane>(service);
        const auto laneJob = [&](int tag) {
            return [&, tag](std::string&) {
                if (laneRunning.fetch_add(1) != 0) {
                    overlapped = true;
                }
                {
                    std::scoped_lock lock{ orderMutex };
                    order.push_back(tag);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{ 2 });
                laneRunning.fetch_sub(1);
                return true;
            };
        };
        const streaming::BackgroundRequestHandle gate = lane->Run([&](std::string&) {
            gateEntered = true;
            return spinUntil([&] { return gateOpen.load(); });
        });
        Require(spinUntil([&] { return gateEntered.load(); }), "The lane's first job never started");
        std::vector<streaming::BackgroundRequestHandle> queued;
        for (const int priority : { 1, 7, 3, 9, 5 }) {
            queued.push_back(lane->Run(laneJob(priority), priority));
        }
        // The lane is busy, so another lane's job, and a job of no lane, run right away.
        streaming::BackgroundLane other{ service };
        const streaming::BackgroundRequestHandle beside = other.Run([](std::string&) { return true; });
        const streaming::BackgroundRequestHandle loose = service->Run([](std::string&) { return true; }, 0);
        Require(streaming::BackgroundLoadService::WaitUntilDone(beside, Clock::now() + std::chrono::seconds{ 30 }) &&
                streaming::BackgroundLoadService::WaitUntilDone(loose, Clock::now() + std::chrono::seconds{ 30 }),
            "A busy lane held up jobs of other lanes");
        Require(std::ranges::all_of(queued, [](const auto& job) { return job->State() == streaming::BackgroundRequestState::Queued; }),
            "A lane started a second job while its first was running");
        Require(service->Reprioritize(queued[0], 20), "A queued lane job could not be re-prioritised");   // 1 -> first
        Require(service->Cancel(queued[2]), "A queued lane job could not be cancelled");                     // 3 never runs
        gateOpen = true;
        for (const streaming::BackgroundRequestHandle& job : queued) {
            Require(streaming::BackgroundLoadService::WaitUntilDone(job, Clock::now() + std::chrono::seconds{ 30 }),
                "A lane job never finished");
        }
        Require(!overlapped && order == std::vector<int>{ 1, 9, 7, 5 }, "A lane did not run its jobs one at a time by priority");

        // CancelAndWait: the running job finishes before it returns, queued jobs never run.
        gateEntered = false;
        gateOpen = false;
        std::atomic<bool> gateFinished{ false };
        const streaming::BackgroundRequestHandle running = lane->Run([&](std::string&) {
            gateEntered = true;
            const bool opened = spinUntil([&] { return gateOpen.load(); });
            gateFinished = true;
            return opened;
        });
        Require(spinUntil([&] { return gateEntered.load(); }), "The lane's job never started");
        std::atomic<bool> cancelledRan{ false };
        const streaming::BackgroundRequestHandle cancelled = lane->Run([&](std::string&) {
            cancelledRan = true;
            return true;
        });
        std::atomic<bool> waited{ false };
        std::thread owner{ [&] {
            lane->CancelAndWait();
            waited = true;
        } };
        Require(spinUntil([&] { return cancelled->State() == streaming::BackgroundRequestState::Cancelled; }),
            "CancelAndWait did not cancel the lane's queued job");
        std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
        const bool returnedEarly = waited.load();
        gateOpen = true;
        owner.join();
        Require(!returnedEarly && gateFinished && !cancelledRan && running->State() == streaming::BackgroundRequestState::Completed,
            "CancelAndWait returned while the lane's job was running, or ran a cancelled job");
        Require(lane->Run([](std::string&) { return true; }) != nullptr, "A lane was not usable after CancelAndWait");
        lane.reset();
        Require(service->QueuedCount() == 0U, "A closed lane left jobs queued");
        const streaming::BackgroundLaneId closed = service->OpenLane();
        service->CloseLane(closed);
        std::atomic<bool> lateRan{ false };
        const streaming::BackgroundRequestHandle late = service->Run([&](std::string&) {
            lateRan = true;
            return true;
        }, 0, closed);
        Require(late->State() == streaming::BackgroundRequestState::Cancelled && !lateRan,
            "A closed lane accepted a job");
    }

    {
        std::shared_ptr<streaming::BackgroundLoadService> first = streaming::BackgroundLoadService::Shared();
        std::shared_ptr<streaming::BackgroundLoadService> second = streaming::BackgroundLoadService::Shared();
        Require(first != nullptr && first == second, "The shared background load service is not one instance");
        const std::weak_ptr<streaming::BackgroundLoadService> watched = first;
        first.reset();
        second.reset();
        Require(watched.expired(), "The shared background load service outlived its last user");
    }
    Purge(root);
}

// ---- Residency under a budget -----------------------------------------------------------------

// Red when: resident plus in-flight bytes ever exceed the budget; when loads do not go highest
// priority and coarsest first; when space is not made by evicting levels nobody wants before
// wanted levels of less important resources; when a load evicts something as important as
// itself; or when a smaller budget is not honoured at once.
void StreamingStaysWithinItsBudget() {
    namespace s = streaming;
    // Two resources, 4 levels each: 1000, 250, 60, 15 bytes; the 15-byte level is the floor.
    const s::StreamingResourceDesc desc{ .levelBytes = { 1000U, 250U, 60U, 15U }, .residentFloor = 3U };
    {
        s::StreamingResidencyManager manager{ 30U + 1000U + 250U + 60U };
        Require(manager.Register(1U, desc) && manager.Register(2U, desc) && !manager.Register(1U, desc),
            "Resources did not register exactly once");
        Require(manager.Stats().residentBytes == 30U && manager.Stats().floorBytes == 30U,
            "Floors were not resident at registration");
        manager.Request(1U, 0U, 1.0F, 1U);
        manager.Request(2U, 0U, 0.1F, 1U);
        s::StreamingPlan plan = manager.Plan(1U);
        // Priority first, coarse first: 1's 60, 250 and 1000, then nothing of 2's fits.
        Require(plan.loads.size() == 3U && plan.loads[0].resource == 1U && plan.loads[0].level == 2U &&
                plan.loads[1].level == 1U && plan.loads[2].level == 0U,
            "Loads were not ordered by priority and coarseness");
        Require(manager.Stats().residentBytes + manager.Stats().inFlightBytes <= manager.Budget(),
            "Starting loads exceeded the budget");
        // The fine level lands first; it must wait for the coarser ones.
        manager.CompleteLoad(1U, 0U, true, 2U);
        Require(manager.ResidentLevel(1U) == 3U, "A level became resident before the coarser ones");
        manager.CompleteLoad(1U, 2U, true, 2U);
        manager.CompleteLoad(1U, 1U, true, 2U);
        Require(manager.ResidentLevel(1U) == 0U, "Arrived levels were not promoted in order");

        // Resource 2 is now more important: the load evicts 1's finer levels, finest first.
        manager.Request(1U, 0U, 0.2F, 3U);
        manager.Request(2U, 2U, 0.9F, 3U);
        plan = manager.Plan(3U);
        Require(plan.loads.size() == 1U && plan.loads[0].resource == 2U && plan.loads[0].level == 2U &&
                plan.evictions.size() == 1U && plan.evictions[0].resource == 1U && plan.evictions[0].newResidentLevel == 1U,
            "A more important load did not evict the least important finest level");
        manager.CompleteLoad(2U, 2U, true, 3U);

        // Equal importance never evicts: no thrashing between two resources.
        manager.Request(1U, 0U, 0.5F, 4U);
        manager.Request(2U, 0U, 0.5F, 4U);
        plan = manager.Plan(4U);
        Require(plan.evictions.empty(), "A load evicted a resource as important as itself");
        for (const s::StreamingLoad& load : plan.loads) {
            manager.CompleteLoad(load.resource, load.level, true, 4U);
        }
        Require(manager.Stats().residentBytes <= manager.Budget(), "Equal-priority loads exceeded the budget");

        // Nobody wants 1's fine levels any more: they go before anything wanted.
        manager.Request(1U, 3U, 0.9F, 5U);
        manager.Request(2U, 0U, 0.1F, 5U);
        plan = manager.Plan(5U);
        Require(!plan.evictions.empty() && plan.evictions.front().resource == 1U,
            "Unwanted levels were not the first to go");

        // A smaller budget is honoured before anything starts.
        for (const s::StreamingLoad& load : plan.loads) {
            manager.CompleteLoad(load.resource, load.level, true, 5U);
        }
        manager.SetBudget(30U + 60U);
        static_cast<void>(manager.Plan(6U));
        Require(manager.Stats().residentBytes + manager.Stats().inFlightBytes <= 90U, "A shrunken budget was not honoured");
        manager.Unregister(1U);
        manager.Unregister(2U);
        Require(manager.Stats().residentBytes == 0U && manager.Stats().inFlightBytes == 0U,
            "Unregistering left bytes accounted");
    }

    // A long randomized run with asynchronous completion: the budget holds every frame, and
    // what is resident is what matters most.
    {
        constexpr std::uint64_t kBudget = 40U * 15U + 6000U;
        s::StreamingResidencyManager manager{ kBudget };
        for (std::uint64_t id = 1U; id <= 40U; ++id) {
            Require(manager.Register(id, desc), "A resource did not register");
        }
        std::vector<std::pair<std::uint64_t, s::StreamingLoad>> inFlight;
        std::uint64_t random = 0x1234567U;
        const auto next = [&random] {
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;
            return random >> 33U;
        };
        for (std::uint64_t frame = 1U; frame <= 2000U; ++frame) {
            for (std::uint64_t id = 1U; id <= 40U; ++id) {
                if (next() % 4U == 0U) {
                    manager.Request(id, static_cast<std::uint32_t>(next() % 4U), static_cast<float>(next() % 1000U) / 1000.0F, frame);
                }
            }
            const s::StreamingPlan plan = manager.Plan(frame, { .maxLoadsInFlight = 8U });
            for (const s::StreamingLoad& load : plan.loads) {
                inFlight.emplace_back(frame + 1U + next() % 3U, load);
            }
            for (auto pending = inFlight.begin(); pending != inFlight.end();) {
                if (pending->first <= frame) {
                    manager.CompleteLoad(pending->second.resource, pending->second.level, next() % 50U != 0U, frame);
                    pending = inFlight.erase(pending);
                } else {
                    ++pending;
                }
            }
            const s::StreamingResidencyStats stats = manager.Stats();
            Require(stats.residentBytes + stats.inFlightBytes <= kBudget, "The streaming budget was exceeded");
        }
        const s::StreamingResidencyStats stats = manager.Stats();
        Require(stats.peakCommittedBytes <= kBudget && stats.loadsCompleted > 100U && stats.evictions > 10U,
            "The randomized run did not exercise loads and evictions");
        std::cout << "content-streaming: budget " << kBudget << " bytes, peak committed " << stats.peakCommittedBytes
                  << " bytes over 2000 frames, " << stats.loadsCompleted << " loads, " << stats.evictions << " evictions\n";
    }

    // Floors alone over budget: nothing streams, and the stats say why.
    {
        s::StreamingResidencyManager manager{ 20U };
        Require(manager.Register(1U, desc) && manager.Register(2U, desc), "Floor-only resources did not register");
        manager.Request(1U, 0U, 1.0F, 1U);
        const s::StreamingPlan plan = manager.Plan(1U);
        Require(plan.loads.empty() && manager.Stats().floorBytes > manager.Budget(),
            "A load started although the floors alone exceed the budget");
    }
}

// Red when: a block of a sealed, compressed pack set read through the asynchronous pack path
// does not arrive verified and decoded, or a tampered one is not refused there. Also reports the
// latency of one streamed block from request to decoded bytes.
void PackBlocksStreamAsynchronously() {
    const std::filesystem::path root = Root() / "pack-stream";
    Purge(root);
    std::filesystem::create_directories(root);
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path path = root / "Game.kbpack";
    // A runtime pack whose one asset carries a 2 MiB streamed block, as a large texture mip does.
    const std::vector<std::uint8_t> mip = Compressible(51U, 2U * 1024U * 1024U);
    const std::string sceneText = "streaming scene";
    bake::AssetBakeDigest mipArtifact{};
    {
        bake::AssetPackWriterOptions options{};
        options.compression = bake::AssetPackBlockCompression::Zstd;
        bake::AssetPackWriter writer{ path, profile, options };
        bake::AssetBakeKey key = ArtifactKey(profile, 51U);
        std::vector<std::uint8_t> blob;
        Require(bake::EncodeRuntimeSourceBlob(Bytes(sceneText), blob), "The streaming scene did not encode");
        Require(writer.BeginAsset({ .key = key, .assetTypeId = std::string{ bake::kSourceAssetTypeId } }) ==
                    bake::BakedAssetSinkStatus::Success &&
                writer.WritePrimaryBlock(blob, profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success,
            "The streaming scene could not be stored");
        bake::BakedAssetBlock block{};
        block.name = "mip0";
        block.residency = bake::BakedAssetBlockResidency::Streaming;
        block.alignmentBytes = profile.packageBlockAlignmentBytes;
        Require(writer.WriteAuxiliaryBlock(block, mip) == bake::BakedAssetSinkStatus::Success &&
                writer.CommitAsset() == bake::BakedAssetSinkStatus::Success,
            "The streamed block could not be stored");
        mipArtifact = key.Digest();
        bake::RuntimeAssetManifest manifest{};
        manifest.targetProfileId = std::string{ profile.identifier };
        manifest.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
        manifest.descriptor.targetPlatforms = { "Windows" };
        manifest.settings.name = "Streaming";
        manifest.settings.defaultMap = "/Game/Start.21kbscene";
        bake::RuntimeAssetManifestEntry scene{};
        scene.id = kb::assets::MakeAssetId("/Game/Start.21kbscene:Scene");
        scene.type = "Scene";
        scene.name = "Start";
        scene.virtualPath = "/Game/Start.21kbscene";
        scene.sourceExtension = ".21kbscene";
        scene.contentHash = bake::HashBakeBytes(Bytes(sceneText));
        scene.artifacts.push_back({ .digest = mipArtifact, .encoding = bake::RuntimeArtifactEncoding::SourceBytes });
        manifest.assets.push_back(scene);
        std::vector<std::uint8_t> manifestBytes;
        Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) == bake::RuntimeAssetManifestStatus::Success,
            "The streaming manifest did not encode");
        bake::AssetBakeKey manifestKey = ArtifactKey(profile, 52U);
        Require(writer.BeginAsset({ .key = manifestKey, .assetTypeId = std::string{ bake::kRuntimeManifestAssetTypeId } }) ==
                    bake::BakedAssetSinkStatus::Success &&
                writer.WritePrimaryBlock(manifestBytes, profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success &&
                writer.CommitAsset() == bake::BakedAssetSinkStatus::Success && writer.Finish() == bake::BakedAssetSinkStatus::Success,
            "The streaming pack could not be published");
    }
    const kb::security::ReleaseSigningKey key = NewKey();
    Seal(path, key);

    streaming::BackgroundLoadService io{ {} };
    for (const bake::AssetPackAccess access : { bake::AssetPackAccess::Ranged, bake::AssetPackAccess::WholeFile }) {
        auto pack = std::make_shared<bake::RuntimeAssetPack>();
        Require(pack->Mount(path, profile, access, TrustOnly(key)) == bake::RuntimeAssetPackStatus::Success,
            "The streaming pack did not mount");
        bake::AssetPackReadStatus status = bake::AssetPackReadStatus::NotMounted;
        const Clock::time_point started = Clock::now();
        const streaming::BackgroundRequestHandle request =
            streaming::ReadPackBlockAsync(io, pack, mipArtifact, "mip0", 10, status);
        Require(status == bake::AssetPackReadStatus::Success && request != nullptr, "A streamed block could not be requested");
        Require(streaming::BackgroundLoadService::WaitUntilDone(request, started + std::chrono::seconds{ 30 }) &&
                request->State() == streaming::BackgroundRequestState::Completed && request->Bytes() == mip,
            ("A streamed block did not arrive decoded: " + request->Error()).c_str());
        const double milliseconds =
            std::chrono::duration<double, std::milli>(request->FinishedAt() - request->SubmittedAt()).count();
        std::cout << "content-streaming: " << mip.size() << "-byte compressed sealed block streamed in " << milliseconds
                  << " ms (" << (access == bake::AssetPackAccess::Ranged ? "file" : "memory") << ")\n";
        Require(streaming::ReadPackBlockAsync(io, pack, mipArtifact, "missing", 10, status) == nullptr &&
                status == bake::AssetPackReadStatus::BlockNotFound,
            "A block that does not exist was requested");
        pack->Unmount();
    }
    io.Forget(path);

    std::vector<std::uint8_t> bytes = ReadFileBytes(path);
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(path, bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::AssetPackReadStatus::Success,
            "The streaming pack did not mount for tampering");
        const bake::AssetPackBlockEntry& block = BlockOf(reader, "mip0");
        bytes[static_cast<std::size_t>(block.offset + block.storedBytes / 2U)] ^= 0x10U;
    }
    const std::filesystem::path tampered = root / "Tampered.kbpack";
    WriteFileBytes(tampered, bytes);
    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(tampered, profile, bake::AssetPackAccess::Ranged, TrustOnly(key)) == bake::RuntimeAssetPackStatus::Success,
        "A tampered streamed block damaged the catalogue");
    bake::AssetPackReadStatus status = bake::AssetPackReadStatus::NotMounted;
    const streaming::BackgroundRequestHandle request = streaming::ReadPackBlockAsync(io, pack, mipArtifact, "mip0", 10, status);
    Require(request != nullptr && streaming::BackgroundLoadService::WaitUntilDone(request, Clock::now() + std::chrono::seconds{ 30 }) &&
            request->State() == streaming::BackgroundRequestState::Failed && request->Bytes().empty() &&
            request->Error().find("PayloadCorrupt") != std::string::npos,
        "A tampered streamed block was handed out");
    pack->Unmount();
    io.Forget(tampered);
    Purge(root);
}

} // namespace

void RunContentStreamingTests() {
    CompressedBlocksRoundTripWithRandomAccess();
    TamperedCompressedBlocksAreRefused();
    PreviousFormatStillMountsAndRolesAreChecked();
    BlocksBeyondFourGigabytesAreAddressed();
    PackSetsMergeChunksAndApplyPatchesInOrder();
    PackSetsRefuseForeignAndInconsistentPacks();
    ReleaseManifestBindsEveryPackOfASet();
    PackToolsSplitRepackAndPatch();
    PatchesRemoveAssetsThroughSignedTombstones();
    EncryptedPackSetsKeepEachPacksKey();
    WorldRegionsParseForChunkRules();
    AsyncReadsFollowPriorityAndReturnExactRanges();
    BackgroundJobsShareThePoolWithReads();
    StreamingStaysWithinItsBudget();
    PackBlocksStreamAsynchronously();
    Purge(Root());
}

} // namespace kb::tests
