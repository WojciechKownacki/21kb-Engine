#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/bake/AssetPack.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"

#include "assets/bake/BakeStorePath.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::tests {
namespace {

namespace bake = kb::assets::bake;

// The fixed-header field offsets, written out here INDEPENDENTLY of the production encoder.
// A fixture that patches the wrong offset is the failure mode that makes a test pass at the
// first guard while claiming to cover a rule, so every doctored pack below is asserted to
// produce its OWN status, not merely to fail.
constexpr std::size_t kHeaderMagicOffset = 0U;
constexpr std::size_t kHeaderFormatVersionOffset = 8U;
constexpr std::size_t kHeaderIndexOffsetOffset = 24U;
constexpr std::size_t kHeaderIndexBytesOffset = 32U;
constexpr std::size_t kHeaderIndexChecksumOffset = 40U;
constexpr std::size_t kHeaderArtifactCountOffset = 48U;
constexpr std::size_t kHeaderProfileHashOffset = 16U;
constexpr std::size_t kHeaderFileBytesOffset = 64U;
constexpr std::size_t kHeaderProfileIdLengthOffset = 96U;
constexpr std::size_t kHeaderFragmentIndexOffsetOffset = 72U;
constexpr std::size_t kHeaderHeaderBytesOffset = 12U;
constexpr std::size_t kHeaderPackageAlignmentOffset = 52U;
constexpr std::size_t kHeaderMappedAlignmentOffset = 56U;
constexpr std::size_t kHeaderReservedWordOffset = 60U;
constexpr std::uint64_t kHeaderBytes = 256U;

// Windows refuses to delete a file something still has open, and the throwing overload of
// remove_all turns that into an aborted process rather than a failed assertion. The path is
// normalised through the same helper the store uses, because a tree the writer created past
// MAX_PATH cannot be walked through an ordinary path - and a run that could not clean up after
// itself would poison every run after it.
void PurgeDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(bake::store::Normalize(directory), error);
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_asset_pack_tests";
}

[[nodiscard]] std::vector<std::uint8_t> Bytes(std::string_view text) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(text.data());
    return { first, first + text.size() };
}

// Deterministic filler that is not a constant, so a block written at the wrong offset shows up
// as wrong bytes rather than as bytes that happen to match its neighbour.
[[nodiscard]] std::vector<std::uint8_t> Filler(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint8_t> bytes(count);
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ULL + 1U;
    for (std::uint8_t& value : bytes) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        value = static_cast<std::uint8_t>((state >> 33U) & 0xFFU);
    }
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

[[nodiscard]] std::uint32_t PeekUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    Require(offset + 4U <= bytes.size(), "Pack fixture read a 32-bit field past the end of the file");
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t PeekUInt64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    Require(offset + 8U <= bytes.size(), "Pack fixture read a 64-bit field past the end of the file");
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

void PokeUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    Require(offset + 4U <= bytes.size(), "Pack fixture wrote a 32-bit field past the end of the file");
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void PokeUInt64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    Require(offset + 8U <= bytes.size(), "Pack fixture wrote a 64-bit field past the end of the file");
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::size_t SkipRuntimeManifestString(
    std::span<const std::uint8_t> bytes,
    std::size_t cursor) {
    const std::uint32_t length = PeekUInt32(bytes, cursor);
    cursor += 4U;
    Require(length <= bytes.size() - cursor,
        "Runtime-manifest fixture string extends past the encoded manifest");
    return cursor + length;
}

struct RuntimeManifestCountOffsets {
    std::size_t targetPlatforms = 0U;
    std::size_t modules = 0U;
    std::size_t plugins = 0U;
    std::size_t assets = 0U;
    std::size_t firstAssetDependencies = 0U;
    std::size_t firstAssetArtifacts = 0U;
    std::size_t auxiliaryFiles = 0U;
};

// Independent walk of the runtime-manifest wire layout. The hostile-count tests must not ask
// the production decoder where its own count fields live, because a shared offset bug would
// otherwise mutate an unrelated field and still appear to exercise the intended guard.
[[nodiscard]] RuntimeManifestCountOffsets ObserveRuntimeManifestCountOffsets(
    std::span<const std::uint8_t> bytes) {
    Require(bytes.size() >= 16U, "Runtime-manifest fixture is shorter than its fixed prefix");
    std::size_t cursor = 16U; // magic, version and reserved word
    cursor = SkipRuntimeManifestString(bytes, cursor);
    cursor += 8U; // target profile hash

    cursor += 4U; // descriptor file version
    cursor = SkipRuntimeManifestString(bytes, cursor); // engine association
    cursor = SkipRuntimeManifestString(bytes, cursor); // content root

    RuntimeManifestCountOffsets offsets{};
    offsets.targetPlatforms = cursor;
    const std::uint32_t targetCount = PeekUInt32(bytes, cursor);
    cursor += 4U;
    for (std::uint32_t index = 0U; index < targetCount; ++index) {
        cursor = SkipRuntimeManifestString(bytes, cursor);
    }

    offsets.modules = cursor;
    const std::uint32_t moduleCount = PeekUInt32(bytes, cursor);
    cursor += 4U;
    for (std::uint32_t index = 0U; index < moduleCount; ++index) {
        cursor = SkipRuntimeManifestString(bytes, cursor);
        cursor = SkipRuntimeManifestString(bytes, cursor);
        cursor = SkipRuntimeManifestString(bytes, cursor);
    }

    offsets.plugins = cursor;
    const std::uint32_t pluginCount = PeekUInt32(bytes, cursor);
    cursor += 4U;
    for (std::uint32_t index = 0U; index < pluginCount; ++index) {
        cursor = SkipRuntimeManifestString(bytes, cursor);
        cursor = SkipRuntimeManifestString(bytes, cursor);
        cursor += 1U; // enabled
    }
    cursor += 1U; // disableEnginePluginsByDefault

    for (std::uint32_t index = 0U; index < 6U; ++index) {
        cursor = SkipRuntimeManifestString(bytes, cursor);
    }
    cursor += 4U; // lighting path
    cursor += 1U; // input enabled
    cursor = SkipRuntimeManifestString(bytes, cursor);
    cursor = SkipRuntimeManifestString(bytes, cursor);

    offsets.assets = cursor;
    const std::uint32_t assetCount = PeekUInt32(bytes, cursor);
    Require(assetCount != 0U, "Runtime-manifest hostile-count fixture has no asset");
    cursor += 4U;
    for (std::uint32_t assetIndex = 0U; assetIndex < assetCount; ++assetIndex) {
        cursor += 8U; // asset id
        for (std::uint32_t stringIndex = 0U; stringIndex < 6U; ++stringIndex) {
            cursor = SkipRuntimeManifestString(bytes, cursor);
        }
        cursor += 8U; // content hash
        cursor += 1U; // runtime loadable

        if (assetIndex == 0U) {
            offsets.firstAssetDependencies = cursor;
        }
        const std::uint32_t dependencyCount = PeekUInt32(bytes, cursor);
        cursor += 4U + static_cast<std::size_t>(dependencyCount) * 8U;

        if (assetIndex == 0U) {
            offsets.firstAssetArtifacts = cursor;
        }
        const std::uint32_t artifactCount = PeekUInt32(bytes, cursor);
        cursor += 4U;
        for (std::uint32_t artifactIndex = 0U; artifactIndex < artifactCount; ++artifactIndex) {
            cursor += 20U; // digest, encoding and three reserved bytes
            cursor = SkipRuntimeManifestString(bytes, cursor);
        }
    }

    offsets.auxiliaryFiles = cursor;
    const std::uint32_t auxiliaryCount = PeekUInt32(bytes, cursor);
    cursor += 4U;
    for (std::uint32_t index = 0U; index < auxiliaryCount; ++index) {
        cursor = SkipRuntimeManifestString(bytes, cursor);
        cursor += 24U; // content hash and artifact digest
    }
    Require(cursor == bytes.size(), "Runtime-manifest fixture walk did not consume the manifest");
    return offsets;
}

[[nodiscard]] bake::RuntimeAssetManifest RuntimeManifestOutputSentinel() {
    bake::RuntimeAssetManifest sentinel{};
    sentinel.targetProfileId = "unchanged-profile";
    sentinel.targetProfileHash = 0x1122334455667788ULL;
    sentinel.descriptor.fileVersion = 3U;
    sentinel.descriptor.engineAssociation = "unchanged-engine";
    sentinel.descriptor.contentRoot = "UnchangedAssets";
    sentinel.descriptor.targetPlatforms = { "UnchangedTarget" };
    sentinel.descriptor.modules = { { .name = "UnchangedModule", .type = "Editor",
        .loadingPhase = "PostDefault" } };
    sentinel.descriptor.plugins = { { .name = "UnchangedPlugin", .binaryPath = "Unchanged.dll",
        .enabled = false } };
    sentinel.descriptor.disableEnginePluginsByDefault = true;
    sentinel.settings.name = "UnchangedSettings";
    sentinel.settings.gameName = "UnchangedGame";
    sentinel.settings.defaultMap = "/Game/Unchanged.21kbscene";
    sentinel.assets = { bake::RuntimeAssetManifestEntry{
        .id = kb::assets::AssetId{ 77U },
        .type = "UnchangedAsset",
        .name = "Unchanged",
        .virtualPath = "/Game/Unchanged.asset",
        .contentHash = 88U,
    } };
    sentinel.auxiliaryFiles = { bake::RuntimeAuxiliaryFileEntry{
        .virtualPath = "/Engine/Unchanged.bin",
        .contentHash = 99U,
        .artifactDigest = { 100U, 101U },
    } };
    return sentinel;
}

[[nodiscard]] bool IsRuntimeManifestOutputSentinel(const bake::RuntimeAssetManifest& value) {
    return value.targetProfileId == "unchanged-profile" &&
        value.targetProfileHash == 0x1122334455667788ULL && value.descriptor.fileVersion == 3U &&
        value.descriptor.engineAssociation == "unchanged-engine" &&
        value.descriptor.contentRoot == "UnchangedAssets" &&
        value.descriptor.targetPlatforms == std::vector<std::string>{ "UnchangedTarget" } &&
        value.descriptor.modules.size() == 1U &&
        value.descriptor.modules.front().name == "UnchangedModule" &&
        value.descriptor.modules.front().type == "Editor" &&
        value.descriptor.modules.front().loadingPhase == "PostDefault" &&
        value.descriptor.plugins.size() == 1U &&
        value.descriptor.plugins.front().name == "UnchangedPlugin" &&
        value.descriptor.plugins.front().binaryPath == "Unchanged.dll" &&
        !value.descriptor.plugins.front().enabled && value.descriptor.disableEnginePluginsByDefault &&
        value.settings.name == "UnchangedSettings" && value.settings.gameName == "UnchangedGame" &&
        value.settings.defaultMap == "/Game/Unchanged.21kbscene" && value.assets.size() == 1U &&
        value.assets.front().id.value == 77U && value.assets.front().type == "UnchangedAsset" &&
        value.assets.front().contentHash == 88U && value.auxiliaryFiles.size() == 1U &&
        value.auxiliaryFiles.front().virtualPath == "/Engine/Unchanged.bin" &&
        value.auxiliaryFiles.front().contentHash == 99U &&
        value.auxiliaryFiles.front().artifactDigest == bake::AssetBakeDigest{ 100U, 101U };
}

// Where every field of one block entry lies inside the file, walked by the TEST's own reading
// of the layout. Nothing here comes from the writer's report of what it did.
struct ObservedBlock {
    std::string name;
    std::uint8_t residency = 0U;
    std::uint8_t compression = 0U;
    std::uint32_t alignmentBytes = 0U;
    std::uint64_t offset = 0U;
    std::uint64_t storedBytes = 0U;
    std::uint64_t uncompressedBytes = 0U;
    bake::AssetBakeDigest payloadDigest{};
    std::size_t alignmentFieldOffset = 0U;
    std::size_t offsetFieldOffset = 0U;
    std::size_t storedBytesFieldOffset = 0U;
};

struct ObservedArtifact {
    bake::AssetBakeDigest key{};
    std::string assetTypeId;
    std::size_t blockCountFieldOffset = 0U;
    std::vector<ObservedBlock> blocks;
};

// Independent walk of the index region, so "the file really is laid out this way" is not the
// writer's own claim repeated back.
[[nodiscard]] std::vector<ObservedArtifact> ObservePack(std::span<const std::uint8_t> file) {
    const std::uint64_t indexOffset = PeekUInt64(file, kHeaderIndexOffsetOffset);
    const std::uint32_t artifactCount = PeekUInt32(file, kHeaderArtifactCountOffset);
    std::size_t cursor = static_cast<std::size_t>(indexOffset);

    std::vector<ObservedArtifact> artifacts;
    artifacts.reserve(artifactCount);
    for (std::uint32_t artifactIndex = 0U; artifactIndex < artifactCount; ++artifactIndex) {
        ObservedArtifact artifact{};
        artifact.key.high = PeekUInt64(file, cursor);
        cursor += 8U;
        artifact.key.low = PeekUInt64(file, cursor);
        cursor += 8U;
        const std::uint8_t typeLength = file[cursor];
        cursor += 1U;
        artifact.assetTypeId.assign(reinterpret_cast<const char*>(file.data() + cursor), typeLength);
        cursor += typeLength;
        artifact.blockCountFieldOffset = cursor;
        const std::uint32_t blockCount = PeekUInt32(file, cursor);
        cursor += 4U;
        for (std::uint32_t blockIndex = 0U; blockIndex < blockCount; ++blockIndex) {
            ObservedBlock block{};
            const std::uint8_t nameLength = file[cursor];
            cursor += 1U;
            block.name.assign(reinterpret_cast<const char*>(file.data() + cursor), nameLength);
            cursor += nameLength;
            block.residency = file[cursor];
            cursor += 1U;
            block.compression = file[cursor];
            cursor += 1U;
            Require(file[cursor] == 0U, "A block entry's reserved byte was not zero");
            cursor += 1U;
            block.alignmentFieldOffset = cursor;
            block.alignmentBytes = PeekUInt32(file, cursor);
            cursor += 4U;
            block.offsetFieldOffset = cursor;
            block.offset = PeekUInt64(file, cursor);
            cursor += 8U;
            block.storedBytesFieldOffset = cursor;
            block.storedBytes = PeekUInt64(file, cursor);
            cursor += 8U;
            block.uncompressedBytes = PeekUInt64(file, cursor);
            cursor += 8U;
            block.payloadDigest.high = PeekUInt64(file, cursor);
            cursor += 8U;
            block.payloadDigest.low = PeekUInt64(file, cursor);
            cursor += 8U;
            artifact.blocks.push_back(std::move(block));
        }
        artifacts.push_back(std::move(artifact));
    }
    Require(cursor == static_cast<std::size_t>(indexOffset + PeekUInt64(file, kHeaderIndexBytesOffset)),
        "The index does not end where the header says it does");
    return artifacts;
}

// Re-checksums a doctored index so a fixture reaches the rule it is aimed at instead of
// stopping at the checksum. A fixture that skipped this would prove nothing but that the
// checksum works.
void ResealIndex(std::vector<std::uint8_t>& file) {
    const std::uint64_t indexOffset = PeekUInt64(file, kHeaderIndexOffsetOffset);
    const std::uint64_t indexBytes = PeekUInt64(file, kHeaderIndexBytesOffset);
    const std::span<const std::uint8_t> index{ file.data() + indexOffset, static_cast<std::size_t>(indexBytes) };
    PokeUInt64(file, kHeaderIndexChecksumOffset, bake::HashBakeBytes(index));
}

[[nodiscard]] bake::AssetBakeKey MakeKey(std::uint64_t seed, std::string_view bakerId = "SkeletalMesh") {
    bake::AssetBakeKey key;
    key.sourceContentHash = 0x0123456789ABCDEFULL + seed;
    key.bakerId = std::string{ bakerId };
    key.bakerVersion = "3";
    key.targetProfileId = "Windows.x64";
    key.targetProfileHash = bake::BakeTargetProfileFingerprint(bake::WindowsX64BakeTargetProfile());
    key.settingsHash = 0xFEDCBA9876543210ULL ^ seed;
    return key;
}

[[nodiscard]] bake::BakedAssetDescriptor MakeDescriptor(std::uint64_t seed, std::string_view assetType = "SkeletalMesh") {
    bake::BakedAssetDescriptor descriptor;
    descriptor.key = MakeKey(seed);
    descriptor.assetTypeId = std::string{ assetType };
    return descriptor;
}

struct WrittenBlock {
    std::string name;
    bake::BakedAssetBlockResidency residency = bake::BakedAssetBlockResidency::Resident;
    std::uint32_t alignmentBytes = 1U;
    std::vector<std::uint8_t> bytes;
};

struct WrittenArtifact {
    bake::AssetBakeDigest key{};
    std::string assetTypeId;
    std::vector<WrittenBlock> blocks;
};

// A pack with the whole variety the format has to carry: several artifacts, several asset
// types, all three residencies, an alignment finer than the profile's and one coarser.
[[nodiscard]] std::vector<WrittenArtifact> BakeSamplePack(const std::filesystem::path& packPath,
                                                          const bake::BakeTargetProfile& profile) {
    std::vector<WrittenArtifact> expected;
    bake::AssetPackWriter writer{ packPath, profile };

    const auto bakeOne = [&](const bake::BakedAssetDescriptor& descriptor, std::vector<WrittenBlock> blocks) {
        Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
            "The pack writer refused an artifact the sample pack needs");
        Require(writer.WritePrimaryBlock(blocks.front().bytes, blocks.front().alignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "The pack writer refused a primary block the sample pack needs");
        for (std::size_t index = 1U; index < blocks.size(); ++index) {
            const WrittenBlock& block = blocks[index];
            Require(writer.WriteAuxiliaryBlock(
                        bake::BakedAssetBlock{
                            .name = block.name,
                            .residency = block.residency,
                            .alignmentBytes = block.alignmentBytes,
                        },
                        block.bytes) == bake::BakedAssetSinkStatus::Success,
                "The pack writer refused an auxiliary block the sample pack needs");
        }
        Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success,
            "The pack writer refused to commit an artifact the sample pack needs");
        expected.push_back(WrittenArtifact{
            .key = descriptor.key.Digest(),
            .assetTypeId = descriptor.assetTypeId,
            .blocks = std::move(blocks),
        });
    };

    bakeOne(MakeDescriptor(1U),
        { WrittenBlock{ .name = "primary",
              .alignmentBytes = profile.packageBlockAlignmentBytes,
              .bytes = Filler(1U, 300U) },
            WrittenBlock{ .name = "lod-tail",
                .residency = bake::BakedAssetBlockResidency::Streaming,
                .alignmentBytes = profile.packageBlockAlignmentBytes,
                .bytes = Filler(2U, 1U) },
            // Asks only for the PACKAGE alignment while declaring itself mapped: the writer has
            // to raise it to the mapping granularity on its own, which is the rule. Asking for
            // the granularity here would satisfy the placement without the rule ever running.
            WrittenBlock{ .name = "mapped-pages",
                .residency = bake::BakedAssetBlockResidency::Mapped,
                .alignmentBytes = profile.packageBlockAlignmentBytes,
                .bytes = Filler(3U, 5000U) } });
    // An alignment FINER than the profile's: the profile is a floor, so the block still has to
    // land on a package-aligned offset.
    bakeOne(MakeDescriptor(2U, "Texture2D"),
        { WrittenBlock{ .name = "primary", .alignmentBytes = 1U, .bytes = Filler(4U, 4096U) } });
    bakeOne(MakeDescriptor(3U, "Texture2D"),
        { WrittenBlock{ .name = "primary",
              .alignmentBytes = profile.packageBlockAlignmentBytes,
              .bytes = Filler(5U, 17U) },
            WrittenBlock{ .name = "coarse",
                .alignmentBytes = 4096U,
                .bytes = Filler(6U, 33U) } });

    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "The pack writer did not publish the sample pack");
    std::ranges::sort(expected, {}, &WrittenArtifact::key);
    return expected;
}

void RequireMountedPackMatches(bake::AssetPackReader& reader,
                               const std::vector<WrittenArtifact>& expected,
                               const char* message) {
    Require(reader.Artifacts().size() == expected.size(), message);
    for (const WrittenArtifact& artifact : expected) {
        const bake::AssetPackArtifactEntry* entry = reader.FindArtifact(artifact.key);
        Require(entry != nullptr && entry->assetTypeId == artifact.assetTypeId, message);
        Require(entry->blocks.size() == artifact.blocks.size(), message);
        for (const WrittenBlock& block : artifact.blocks) {
            std::vector<std::uint8_t> read;
            Require(reader.ReadBlock(*entry, block.name, read) == bake::AssetPackReadStatus::Success, message);
            Require(read == block.bytes, message);
        }
    }
}

// Red when: any block loses or gains a byte on the way through the container, a block is read
// from the wrong place, an auxiliary block's residency or name is dropped, or the two access
// modes disagree. The whole-file mode is not an optimisation to skip - a server may refuse
// range requests outright (RFC 9110 14.2), so it is the only mode some hosts leave us.
void EveryBlockSurvivesTheContainer() {
    const std::filesystem::path root = TestRoot() / "roundtrip";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("sample" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<WrittenArtifact> expected = BakeSamplePack(packPath, profile);

    bake::AssetPackReader ranged;
    Require(ranged.Mount(packPath, bake::AssetPackAccess::Ranged) == bake::AssetPackReadStatus::Success,
        "A pack this writer just published did not mount");
    Require(ranged.Header().targetProfileId == "Windows.x64" &&
            ranged.Header().targetProfileHash == bake::BakeTargetProfileFingerprint(profile),
        "The pack header does not carry the profile it was baked for");
    Require(ranged.MatchesTargetProfile(profile),
        "The mounted pack did not match the exact profile that wrote it");
    Require(!ranged.MatchesTargetProfile(bake::AndroidAstcArm64BakeTargetProfile()),
        "A Windows package was accepted as an Android package");
    Require(ranged.Header().packageBlockAlignmentBytes == profile.packageBlockAlignmentBytes &&
            ranged.Header().mappedBlockAlignmentBytes == profile.mappedBlockAlignmentBytes,
        "The pack header does not carry the alignments its blocks were placed at");
    Require(ranged.Header().fragmentIndexOffset == 0U && ranged.Header().fragmentIndexBytes == 0U &&
            ranged.Header().fragmentCount == 0U && ranged.Header().fragmentAlignmentBytes == 0U,
        "A pack claimed a streaming fragment index that nothing writes yet");
    RequireMountedPackMatches(ranged, expected, "A block did not survive the round trip through the container");

    bake::AssetPackReader wholeFile;
    Require(wholeFile.Mount(packPath, bake::AssetPackAccess::WholeFile) == bake::AssetPackReadStatus::Success,
        "A pack did not mount through the whole-file fallback");
    RequireMountedPackMatches(
        wholeFile, expected, "The whole-file fallback handed back different bytes than the ranged reader");

    // The same pack read twice must be the same bytes: a reader that consumed its own stream,
    // or that handed back a buffer it later reused, passes the first read and fails here.
    bake::AssetPackReader second;
    Require(second.Mount(packPath) == bake::AssetPackReadStatus::Success, "A pack did not mount a second time");
    RequireMountedPackMatches(second, expected, "The same pack read twice did not give identical bytes");
    for (const WrittenArtifact& artifact : expected) {
        const bake::AssetPackArtifactEntry* entry = second.FindArtifact(artifact.key);
        std::vector<std::uint8_t> first;
        std::vector<std::uint8_t> again;
        Require(second.ReadBlock(*entry, "primary", first) == bake::AssetPackReadStatus::Success &&
                second.ReadBlock(*entry, "primary", again) == bake::AssetPackReadStatus::Success && first == again,
            "Reading the same block twice from one mount did not give identical bytes");
    }

    // Nothing in the pack is addressable except through the pack: an artifact that was never
    // baked has no entry, and an entry that belongs to a different mount is not trusted.
    Require(ranged.FindArtifact(MakeKey(99U).Digest()) == nullptr,
        "The reader answered with an artifact the pack does not hold");
    std::vector<std::uint8_t> scratch;
    const bake::AssetPackArtifactEntry foreign{};
    Require(ranged.ReadBlock(foreign, "primary", scratch) == bake::AssetPackReadStatus::ArtifactNotFound,
        "The reader read through an index entry that was never validated against this file");
    Require(ranged.ReadBlock(ranged.Artifacts().front(), "no-such-block", scratch) ==
            bake::AssetPackReadStatus::BlockNotFound,
        "The reader answered for a block the artifact does not carry");

    // A mounted pack holds the file open; unmounting has to give it back, or a build that
    // re-bakes over its own output would fail on Windows for a reason nobody could see.
    ranged.Unmount();
    wholeFile.Unmount();
    second.Unmount();
    Require(!ranged.IsMounted() && ranged.Artifacts().empty(), "An unmounted reader still reports a pack");
    std::error_code removeError;
    Require(std::filesystem::remove(packPath, removeError) && !removeError,
        "A pack could not be deleted after every reader that mounted it was unmounted");

    PurgeDirectory(root);
}

// The fingerprint describes the target recipe, while these fields describe the actual file
// layout. A copied fingerprint must not make a pack with doctored alignment claims compatible.
void ProfileMatchIncludesRecordedAlignments() {
    const std::filesystem::path root = TestRoot() / "profile-layout";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path source = root / "source.kbpack";
    const std::filesystem::path doctored = root / "doctored.kbpack";
    static_cast<void>(BakeSamplePack(source, profile));

    std::vector<std::uint8_t> bytes = ReadFileBytes(source);
    const std::uint32_t doctoredPackageAlignment = profile.packageBlockAlignmentBytes / 2U;
    const std::uint32_t doctoredMappedAlignment = profile.mappedBlockAlignmentBytes / 2U;
    Require(doctoredPackageAlignment != 0U && doctoredMappedAlignment != 0U,
        "Profile-alignment fixture cannot produce smaller valid alignments");
    PokeUInt32(bytes, kHeaderPackageAlignmentOffset, doctoredPackageAlignment);
    PokeUInt32(bytes, kHeaderMappedAlignmentOffset, doctoredMappedAlignment);
    WriteFileBytes(doctored, bytes);

    bake::AssetPackReader reader;
    Require(reader.Mount(doctored) == bake::AssetPackReadStatus::Success,
        "A valid pack with independently valid doctored alignment claims did not mount");
    Require(reader.Header().targetProfileId == profile.identifier &&
            reader.Header().targetProfileHash == bake::BakeTargetProfileFingerprint(profile),
        "Profile-alignment fixture accidentally changed the profile identity");
    Require(reader.Header().packageBlockAlignmentBytes != profile.packageBlockAlignmentBytes &&
            reader.Header().mappedBlockAlignmentBytes != profile.mappedBlockAlignmentBytes,
        "Profile-alignment fixture did not reach the fields under test");
    Require(!reader.MatchesTargetProfile(profile),
        "A pack with different recorded alignments matched the target profile by fingerprint alone");

    PurgeDirectory(root);
}

// Red when: payload bytes can change without changing the catalogue. The index checksum
// protects metadata; each block digest independently protects the bytes served to runtime.
void CorruptPayloadIsNeverReturned() {
    const std::filesystem::path root = TestRoot() / "payload-integrity";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / "corrupt.kbpack";
    static_cast<void>(BakeSamplePack(packPath, profile));

    std::vector<std::uint8_t> file = ReadFileBytes(packPath);
    const std::vector<ObservedArtifact> observed = ObservePack(file);
    Require(!observed.empty() && !observed.front().blocks.empty(),
        "The payload-integrity fixture contains no block");
    const ObservedBlock& damaged = observed.front().blocks.front();
    Require(damaged.storedBytes != 0U && damaged.offset < file.size(),
        "The payload-integrity fixture points outside its file");
    file[static_cast<std::size_t>(damaged.offset)] ^= 0x80U;
    WriteFileBytes(packPath, file);

    bake::AssetPackReader reader;
    Require(reader.Mount(packPath, bake::AssetPackAccess::Ranged) == bake::AssetPackReadStatus::Success,
        "Changing payload bytes unexpectedly damaged the front catalogue");
    const bake::AssetPackArtifactEntry* artifact = reader.FindArtifact(observed.front().key);
    Require(artifact != nullptr, "The damaged block's artifact disappeared from the catalogue");
    std::vector<std::uint8_t> payload{ 0xAAU };
    Require(reader.ReadBlock(*artifact, damaged.name, payload) == bake::AssetPackReadStatus::PayloadCorrupt,
        "A block whose payload disagrees with its digest was returned as valid");
    Require(payload.empty(), "Bytes from a corrupt payload escaped through the reader");

    reader.Unmount();
    PurgeDirectory(root);
}

// Red when: thread scheduling changes the published file. A content-addressed package is a
// build product, so identical inputs must produce identical bytes in any completion order.
void PackageBytesDoNotDependOnArtifactOrder() {
    const std::filesystem::path root = TestRoot() / "deterministic-order";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const bake::BakedAssetDescriptor first = MakeDescriptor(801U, "Texture2D");
    const bake::BakedAssetDescriptor second = MakeDescriptor(802U, "SkeletalMesh");
    const std::vector<std::uint8_t> firstBytes = Filler(801U, 701U);
    const std::vector<std::uint8_t> secondBytes = Filler(802U, 1703U);

    const auto publish = [&](const std::filesystem::path& path, bool reverse) {
        bake::AssetPackWriter writer{ path, profile };
        const auto write = [&](const bake::BakedAssetDescriptor& descriptor,
                               const std::vector<std::uint8_t>& payload) {
            Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
                "The deterministic pack refused an artifact");
            Require(writer.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
                    bake::BakedAssetSinkStatus::Success,
                "The deterministic pack refused an artifact payload");
            Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success,
                "The deterministic pack refused an artifact commit");
        };
        if (reverse) {
            write(second, secondBytes);
            write(first, firstBytes);
        } else {
            write(first, firstBytes);
            write(second, secondBytes);
        }
        Require(writer.Finish() == bake::BakedAssetSinkStatus::Success,
            "The deterministic pack was not published");
    };

    const std::filesystem::path forwardPath = root / "forward.kbpack";
    const std::filesystem::path reversePath = root / "reverse.kbpack";
    publish(forwardPath, false);
    publish(reversePath, true);
    Require(ReadFileBytes(forwardPath) == ReadFileBytes(reversePath),
        "Identical artifacts produced different package bytes when their completion order changed");

    PurgeDirectory(root);
}

void RuntimeManifestIsCanonicalAndHostileInputSafe() {
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::RuntimeAssetManifest first{};
    first.targetProfileId = std::string{ profile.identifier };
    first.targetProfileHash = bake::BakeTargetProfileFingerprint(profile);
    first.descriptor.targetPlatforms = { "Windows" };
    first.descriptor.modules = { { .name = "Game" } };
    first.descriptor.plugins = { { .name = "Physics", .binaryPath = "Physics.dll", .enabled = true } };
    first.settings.name = "CookedProject";
    first.settings.gameName = "Cooked Game";
    first.settings.defaultMap = "/Game/Scenes/Main.21kbscene";
    first.assets = {
        bake::RuntimeAssetManifestEntry{
            .id = kb::assets::MakeAssetId("/Game/Textures/Albedo.png:RenderTexture"),
            .type = "RenderTexture",
            .name = "Albedo",
            .virtualPath = "/Game/Textures/Albedo.png",
            .sourceExtension = ".png",
            .contentHash = 202U,
            .dependencies = { kb::assets::AssetId{ 9U }, kb::assets::AssetId{ 3U } },
            .artifacts = {
                { .digest = { 9U, 8U },
                    .encoding = bake::RuntimeArtifactEncoding::BakedTexture,
                    .qualifier = "bc-baseline" },
                { .digest = { 7U, 6U },
                    .encoding = bake::RuntimeArtifactEncoding::SourceBytes },
            },
        },
        bake::RuntimeAssetManifestEntry{
            .id = kb::assets::MakeAssetId("/Game/Scenes/Main.21kbscene:Scene"),
            .type = "Scene",
            .name = "Main",
            .virtualPath = "/Game/Scenes/Main.21kbscene",
            .sourceExtension = ".21kbscene",
            .contentHash = 101U,
            .artifacts = { { .digest = { 5U, 4U },
                .encoding = bake::RuntimeArtifactEncoding::SourceBytes } },
        },
    };
    first.auxiliaryFiles = {
        { .virtualPath = "/Game/Scenes/Main.21kbscene.meta",
            .contentHash = 303U,
            .artifactDigest = { 3U, 2U } },
    };

    bake::RuntimeAssetManifest second = first;
    std::ranges::reverse(second.assets);
    std::ranges::reverse(second.assets.back().dependencies);
    std::ranges::reverse(second.assets.back().artifacts);

    std::vector<std::uint8_t> firstBytes;
    std::vector<std::uint8_t> secondBytes;
    Require(bake::EncodeRuntimeAssetManifest(first, firstBytes) == bake::RuntimeAssetManifestStatus::Success &&
            bake::EncodeRuntimeAssetManifest(second, secondBytes) == bake::RuntimeAssetManifestStatus::Success,
        "A valid runtime manifest could not be encoded");
    Require(firstBytes == secondBytes,
        "Runtime manifest bytes depend on discovery or task completion order");

    bake::RuntimeAssetManifest decoded{};
    Require(bake::DecodeRuntimeAssetManifest(firstBytes, decoded) == bake::RuntimeAssetManifestStatus::Success,
        "A runtime manifest could not be decoded after encoding");
    Require(decoded.targetProfileId == first.targetProfileId &&
            decoded.targetProfileHash == first.targetProfileHash && decoded.settings == first.settings &&
            decoded.assets.size() == 2U &&
            std::ranges::any_of(decoded.assets, [](const bake::RuntimeAssetManifestEntry& asset) {
                return asset.virtualPath == "/Game/Textures/Albedo.png" &&
                    asset.dependencies.front().value == 3U;
            }) &&
            decoded.auxiliaryFiles.size() == 1U,
        "A runtime manifest changed its project or asset catalogue during the round trip");

    std::vector<std::uint8_t> unsupported = firstBytes;
    PokeUInt32(unsupported, 8U, bake::kRuntimeAssetManifestVersion + 1U);
    Require(bake::DecodeRuntimeAssetManifest(unsupported, decoded) ==
            bake::RuntimeAssetManifestStatus::UnsupportedVersion,
        "A runtime manifest from an unsupported version was accepted");
    std::vector<std::uint8_t> trailing = firstBytes;
    trailing.push_back(0U);
    Require(bake::DecodeRuntimeAssetManifest(trailing, decoded) == bake::RuntimeAssetManifestStatus::Malformed,
        "A runtime manifest with trailing bytes was accepted");

    const RuntimeManifestCountOffsets countOffsets = ObserveRuntimeManifestCountOffsets(firstBytes);
    struct HostileCountCase {
        std::string_view name;
        std::size_t offset = 0U;
        std::uint32_t count = 0U;
    };
    // These values are independently one above the decoder's production budgets. Descriptor
    // limits mirror the persisted project format; runtime assets/dependencies/auxiliary files
    // have separate aggregate budgets, while one asset has at most 64 artifact variants.
    const std::array hostileCounts{
        HostileCountCase{ "descriptor target platforms", countOffsets.targetPlatforms, 257U },
        HostileCountCase{ "descriptor modules", countOffsets.modules, 4'097U },
        HostileCountCase{ "descriptor plugins", countOffsets.plugins, 4'097U },
        HostileCountCase{ "runtime assets", countOffsets.assets, 65'537U },
        HostileCountCase{ "asset dependencies", countOffsets.firstAssetDependencies, 65'537U },
        HostileCountCase{ "asset artifacts", countOffsets.firstAssetArtifacts, 65U },
        HostileCountCase{ "auxiliary files", countOffsets.auxiliaryFiles, 131'073U },
    };
    for (const HostileCountCase& hostileCount : hostileCounts) {
        std::vector<std::uint8_t> hostile = firstBytes;
        PokeUInt32(hostile, hostileCount.offset, hostileCount.count);
        bake::RuntimeAssetManifest transactionalOutput = RuntimeManifestOutputSentinel();
        bake::RuntimeAssetManifestStatus status = bake::RuntimeAssetManifestStatus::Success;
        bool threw = false;
        try {
            status = bake::DecodeRuntimeAssetManifest(hostile, transactionalOutput);
        } catch (...) {
            threw = true;
        }
        const std::string exceptionMessage = std::string{ "A hostile " } +
            std::string{ hostileCount.name } +
            " count escaped the runtime-manifest decoder as an exception";
        Require(!threw, exceptionMessage.c_str());
        const std::string budgetMessage = std::string{ "A hostile " } +
            std::string{ hostileCount.name } +
            " count was not refused by its explicit resource budget";
        Require(status == bake::RuntimeAssetManifestStatus::TooLarge, budgetMessage.c_str());
        const std::string outputMessage = std::string{ "Refusing a hostile " } +
            std::string{ hostileCount.name } + " count changed the caller's output manifest";
        Require(IsRuntimeManifestOutputSentinel(transactionalOutput), outputMessage.c_str());
    }

    bake::RuntimeAssetManifest duplicate = first;
    duplicate.auxiliaryFiles.front().virtualPath = duplicate.assets.front().virtualPath;
    std::vector<std::uint8_t> ignored;
    Require(bake::EncodeRuntimeAssetManifest(duplicate, ignored) ==
            bake::RuntimeAssetManifestStatus::DuplicateEntry,
        "Two runtime files were allowed to claim one virtual path");

    bake::RuntimeAssetManifest wrongDefaultMapType = first;
    wrongDefaultMapType.settings.defaultMap = "/Game/Textures/Albedo.png";
    Require(bake::EncodeRuntimeAssetManifest(wrongDefaultMapType, ignored) ==
            bake::RuntimeAssetManifestStatus::InvalidProject,
        "A runtime manifest accepted a default map that is not a Scene asset");

    bake::RuntimeAssetManifest editorOnlyDefaultMap = first;
    const auto scene = std::ranges::find(
        editorOnlyDefaultMap.assets,
        editorOnlyDefaultMap.settings.defaultMap,
        &bake::RuntimeAssetManifestEntry::virtualPath);
    Require(scene != editorOnlyDefaultMap.assets.end(), "The default-map fixture lost its scene");
    scene->runtimeLoadable = false;
    Require(bake::EncodeRuntimeAssetManifest(editorOnlyDefaultMap, ignored) ==
            bake::RuntimeAssetManifestStatus::InvalidProject,
        "A runtime manifest accepted an editor-only default Scene");
}

// Red when: the writer reports an alignment it did not honour, a block is placed at an offset
// its alignment forbids, a Mapped block gets only the package alignment, or an under-aligned
// request is passed through instead of being raised to the profile's floor. Every number here
// is measured from the file's own bytes, never taken from the writer.
void AlignmentIsMeasuredInTheFileNotReported() {
    const std::filesystem::path root = TestRoot() / "alignment";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("aligned" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<WrittenArtifact> expected = BakeSamplePack(packPath, profile);

    const std::vector<std::uint8_t> file = ReadFileBytes(packPath);
    Require(file.size() == PeekUInt64(file, kHeaderFileBytesOffset),
        "The pack does not record its own length");
    for (std::size_t index = 0U; index < bake::kAssetPackMagic.size(); ++index) {
        Require(file[kHeaderMagicOffset + index] == static_cast<std::uint8_t>(bake::kAssetPackMagic[index]),
            "A published pack does not begin with the pack magic");
    }
    Require(PeekUInt64(file, kHeaderIndexOffsetOffset) == kHeaderBytes,
        "The index is not at the front of the file, which is the whole reason for this layout");

    const std::uint64_t indexEnd = kHeaderBytes + PeekUInt64(file, kHeaderIndexBytesOffset);
    const std::vector<ObservedArtifact> observed = ObservePack(file);
    Require(observed.size() == expected.size(), "The file holds a different number of artifacts than were baked");

    std::uint64_t previousEnd = indexEnd;
    for (std::size_t artifactIndex = 0U; artifactIndex < observed.size(); ++artifactIndex) {
        const ObservedArtifact& artifact = observed[artifactIndex];
        const WrittenArtifact& source = expected[artifactIndex];
        Require(artifact.key == source.key && artifact.assetTypeId == source.assetTypeId,
            "The index names a different artifact than the one baked into that slot");
        Require(artifact.blocks.size() == source.blocks.size(), "The index lost a block");
        for (std::size_t blockIndex = 0U; blockIndex < artifact.blocks.size(); ++blockIndex) {
            const ObservedBlock& block = artifact.blocks[blockIndex];
            const WrittenBlock& sourceBlock = source.blocks[blockIndex];
            Require(block.name == sourceBlock.name, "The index lost a block name");
            Require(block.compression == static_cast<std::uint8_t>(bake::AssetPackBlockCompression::None),
                "A block was recorded with a compression method nothing implements");
            Require(block.residency == static_cast<std::uint8_t>(sourceBlock.residency),
                "The index lost a block's residency");
            Require(block.storedBytes == sourceBlock.bytes.size() && block.uncompressedBytes == block.storedBytes,
                "The index lost a block's length");

            // The profile is a FLOOR. A request finer than 256 must still land on 256.
            Require(block.alignmentBytes >= profile.packageBlockAlignmentBytes,
                "A block was placed below the profile's package alignment");
            Require(block.alignmentBytes >= sourceBlock.alignmentBytes,
                "A block was placed below the alignment its baker asked for");
            Require(block.offset % profile.packageBlockAlignmentBytes == 0U,
                "A block offset is not a multiple of the profile's package alignment");
            Require(block.offset % block.alignmentBytes == 0U,
                "A block offset is not a multiple of the alignment the index reports for it");
            if (sourceBlock.residency == bake::BakedAssetBlockResidency::Mapped) {
                Require(block.alignmentBytes >= profile.mappedBlockAlignmentBytes,
                    "A mapped block was recorded below the mapping granularity");
                Require(block.offset % profile.mappedBlockAlignmentBytes == 0U,
                    "A mapped block is not on the mapping granularity, so its offset is one mmap would refuse");
            }
            Require(block.offset >= previousEnd, "Two blocks overlap inside the pack");
            previousEnd = block.offset + block.storedBytes;

            // The bytes really are AT that offset in the file, not merely described as being
            // there: a writer that reported one placement and performed another fails here.
            Require(block.offset + block.storedBytes <= file.size(), "A block runs past the end of the file");
            Require(std::equal(sourceBlock.bytes.begin(),
                        sourceBlock.bytes.end(),
                        file.begin() + static_cast<std::ptrdiff_t>(block.offset)),
                "A block's bytes are not at the offset the index gives for it");
        }
    }
    Require(previousEnd == file.size(), "The pack has bytes past its last block");

    PurgeDirectory(root);
}

// A doctored copy of a good pack, re-checksummed unless the point IS the checksum.
void WriteDoctoredPack(const std::filesystem::path& goodPack,
                       const std::filesystem::path& doctoredPack,
                       const std::function<void(std::vector<std::uint8_t>&)>& doctor,
                       bool reseal) {
    std::vector<std::uint8_t> file = ReadFileBytes(goodPack);
    doctor(file);
    if (reseal) {
        ResealIndex(file);
    }
    WriteFileBytes(doctoredPack, file);
}

// Red when: the reader trusts the index. Every case here is a pack a hostile or corrupt build
// could hand the runtime, and every one is asserted to produce ITS OWN status - a fixture that
// stopped at an earlier guard would report the wrong one and fail.
void AHostilePackIsRefusedRatherThanTrusted() {
    const std::filesystem::path root = TestRoot() / "hostile";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path goodPack = root / ("good" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<WrittenArtifact> expected = BakeSamplePack(goodPack, profile);

    // The fixture pipeline itself has to be sound: an untouched copy must still mount, or every
    // refusal below would be evidence about the copy rather than about the rule.
    {
        const std::filesystem::path copied = root / "untouched.kbpack";
        WriteDoctoredPack(goodPack, copied, [](std::vector<std::uint8_t>&) {}, true);
        bake::AssetPackReader reader;
        Require(reader.Mount(copied) == bake::AssetPackReadStatus::Success,
            "An untouched copy of a good pack did not mount, so no fixture below proves anything");
        RequireMountedPackMatches(reader, expected, "An untouched copy of a good pack lost bytes");
    }

    const auto requireRefusal = [&](std::string_view name,
                                    const std::function<void(std::vector<std::uint8_t>&)>& doctor,
                                    bool reseal,
                                    bake::AssetPackReadStatus want,
                                    const char* message) {
        const std::filesystem::path doctored = root / (std::string{ name } + ".kbpack");
        WriteDoctoredPack(goodPack, doctored, doctor, reseal);
        bake::AssetPackReader reader;
        const bake::AssetPackReadStatus status = reader.Mount(doctored);
        Require(status == want, message);
        Require(!reader.IsMounted(), "A refused pack left the reader mounted");
        Require(reader.Artifacts().empty(), "A refused pack left artifacts behind on the reader");
    };

    requireRefusal(
        "bad-magic", [](std::vector<std::uint8_t>& file) { file[kHeaderMagicOffset] ^= 0xFFU; }, false,
        bake::AssetPackReadStatus::NotAnAssetPack, "A file that is not a pack was mounted as one");
    requireRefusal(
        "bad-version",
        [](std::vector<std::uint8_t>& file) { PokeUInt32(file, kHeaderFormatVersionOffset, 0xFFFFU); }, false,
        bake::AssetPackReadStatus::UnsupportedVersion, "A pack in a format this build cannot read was mounted");
    requireRefusal(
        "index-bit-flip",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            file[observed.front().blocks.front().offsetFieldOffset] ^= 0x01U;
        },
        false, bake::AssetPackReadStatus::IndexCorrupt, "An index that disagrees with its own checksum was trusted");
    requireRefusal(
        "offset-past-end",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            PokeUInt64(file, observed.front().blocks.front().offsetFieldOffset, PeekUInt64(file, kHeaderFileBytesOffset));
        },
        true, bake::AssetPackReadStatus::BlockOutOfRange, "A block whose offset runs past the end of the file was trusted");
    requireRefusal(
        "length-past-end",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const ObservedBlock& block = observed.front().blocks.front();
            PokeUInt64(file, block.storedBytesFieldOffset, PeekUInt64(file, kHeaderFileBytesOffset));
            PokeUInt64(file, block.storedBytesFieldOffset + 8U, PeekUInt64(file, kHeaderFileBytesOffset));
        },
        true, bake::AssetPackReadStatus::BlockOutOfRange, "A block whose length runs past the end of the file was trusted");
    // The four-gigabyte block from the brief: believed, it is a four-gigabyte allocation on a
    // wasm32 heap that caps out at two.
    requireRefusal(
        "four-gigabyte-block",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const ObservedBlock& block = observed.front().blocks.front();
            PokeUInt64(file, block.storedBytesFieldOffset, 4ULL * 1024ULL * 1024ULL * 1024ULL);
            PokeUInt64(file, block.storedBytesFieldOffset + 8U, 4ULL * 1024ULL * 1024ULL * 1024ULL);
        },
        true, bake::AssetPackReadStatus::BlockTooLarge, "A block claiming four gigabytes was believed");
    requireRefusal(
        "offset-overflow",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const ObservedBlock& block = observed.front().blocks.front();
            PokeUInt64(file, block.offsetFieldOffset, 0xFFFFFFFFFFFFFF00ULL);
        },
        true, bake::AssetPackReadStatus::BlockOutOfRange,
        "An offset that wraps when its length is added to it was trusted");
    requireRefusal(
        "misaligned-offset",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const ObservedBlock& block = observed.front().blocks.front();
            PokeUInt64(file, block.offsetFieldOffset, block.offset + 1U);
        },
        true, bake::AssetPackReadStatus::BlockOutOfRange, "A block placed off its own alignment was trusted");
    requireRefusal(
        "mapped-block-understates-alignment",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const auto artifact = std::ranges::find_if(observed, [](const ObservedArtifact& candidate) {
                return std::ranges::any_of(candidate.blocks, [](const ObservedBlock& block) {
                    return block.name == "mapped-pages";
                });
            });
            Require(artifact != observed.end(), "The mapped-alignment fixture needs a mapped block");
            const auto mapped = std::ranges::find_if(artifact->blocks, [](const ObservedBlock& block) {
                return block.name == "mapped-pages";
            });
            PokeUInt32(file, mapped->alignmentFieldOffset,
                PeekUInt32(file, kHeaderPackageAlignmentOffset));
        },
        true, bake::AssetPackReadStatus::BlockOutOfRange,
        "A mapped block that claimed less than the mapping granularity was trusted");
    requireRefusal(
        "offset-inside-index",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            PokeUInt64(file, observed.front().blocks.front().offsetFieldOffset, 0U);
        },
        true, bake::AssetPackReadStatus::BlockOutOfRange, "A block that begins inside the header was trusted");
    requireRefusal(
        "zero-block-artifact",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            // Truncate the index to just this artifact's header, so the count and the bytes
            // agree about an artifact that owns nothing.
            const std::size_t blockCountField = observed.front().blockCountFieldOffset;
            PokeUInt32(file, blockCountField, 0U);
            const std::uint64_t newIndexBytes = blockCountField + 4U - kHeaderBytes;
            PokeUInt32(file, kHeaderArtifactCountOffset, 1U);
            PokeUInt64(file, kHeaderIndexBytesOffset, newIndexBytes);
        },
        true, bake::AssetPackReadStatus::IndexCorrupt, "An artifact with no blocks at all was accepted");
    requireRefusal(
        "unknown-compression",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            // The compression byte sits directly before the reserved byte and the alignment.
            file[observed.front().blocks.front().alignmentFieldOffset - 2U] = 7U;
        },
        true, bake::AssetPackReadStatus::IndexCorrupt,
        "A block stored with a method this build cannot undo was handed back as a payload");
    requireRefusal(
        "reserved-fragment-index",
        [](std::vector<std::uint8_t>& file) { PokeUInt64(file, kHeaderFragmentIndexOffsetOffset, kHeaderBytes); },
        true, bake::AssetPackReadStatus::HeaderCorrupt,
        "A pack claiming a streaming fragment index this build cannot read was mounted anyway");
    requireRefusal(
        "artifact-count-too-high",
        [](std::vector<std::uint8_t>& file) {
            PokeUInt32(file, kHeaderArtifactCountOffset, PeekUInt32(file, kHeaderArtifactCountOffset) + 1U);
        },
        true, bake::AssetPackReadStatus::IndexCorrupt, "An index that runs out before its own count was accepted");
    requireRefusal(
        "artifact-count-too-low",
        [](std::vector<std::uint8_t>& file) {
            PokeUInt32(file, kHeaderArtifactCountOffset, PeekUInt32(file, kHeaderArtifactCountOffset) - 1U);
        },
        true, bake::AssetPackReadStatus::IndexCorrupt, "An index with bytes past its own count was accepted");
    // The fixed layout is a rule, not a convention: everything downstream computes against
    // "the index starts at kAssetPackHeaderBytes and the header is that long", including the
    // floor ValidateBlockRange uses to keep a block out of the catalogue that describes it.
    // Removing both header checks left the whole suite green.
    requireRefusal(
        "index-somewhere-else",
        [](std::vector<std::uint8_t>& file) { PokeUInt64(file, kHeaderIndexOffsetOffset, kHeaderBytes * 2U); },
        false, bake::AssetPackReadStatus::HeaderCorrupt,
        "A pack whose index does not start where the format puts it was mounted");
    requireRefusal(
        "header-a-different-size",
        [](std::vector<std::uint8_t>& file) { PokeUInt32(file, kHeaderHeaderBytesOffset, kHeaderBytes * 2U); },
        false, bake::AssetPackReadStatus::HeaderCorrupt,
        "A pack claiming a header this build does not lay out was mounted");
    // The alignments are what every block offset in the file is checked against, so a pack
    // that declares nonsense for them disarms the block checks rather than only itself.
    requireRefusal(
        "package-alignment-not-a-power-of-two",
        [](std::vector<std::uint8_t>& file) { PokeUInt32(file, kHeaderPackageAlignmentOffset, 96U); }, true,
        bake::AssetPackReadStatus::HeaderCorrupt, "A pack declaring an impossible block alignment was mounted");
    requireRefusal(
        "mapped-alignment-below-the-package-alignment",
        [](std::vector<std::uint8_t>& file) { PokeUInt32(file, kHeaderMappedAlignmentOffset, 128U); }, true,
        bake::AssetPackReadStatus::HeaderCorrupt,
        "A pack whose mapping granularity is not a multiple of its block alignment was mounted");
    // Reserved means reserved on both sides. The fragment fields have a fixture; the spare
    // word beside them did not, and deleting its check changed nothing.
    requireRefusal(
        "reserved-header-word",
        [](std::vector<std::uint8_t>& file) { PokeUInt32(file, kHeaderReservedWordOffset, 1U); }, true,
        bake::AssetPackReadStatus::HeaderCorrupt,
        "A pack using a header field this build does not know was mounted anyway");
    // Only the None method exists, so the two sizes must agree. If they may disagree, a block
    // is handed back at its stored length while the caller was told a different one.
    requireRefusal(
        "stored-length-disagrees-with-uncompressed",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const ObservedBlock& block = observed.front().blocks.front();
            PokeUInt64(file, block.storedBytesFieldOffset + 8U, block.storedBytes + 1U);
        },
        true, bake::AssetPackReadStatus::IndexCorrupt,
        "A block whose stored and uncompressed lengths disagree was accepted under a method that cannot resize it");
    // Two blocks on one range. Memory-safe -- both lie inside the file -- and semantically
    // ruinous: whichever of them a caller asks for, it is handed the other one's payload, so a
    // content-addressed key answers with bytes that were baked under a different key.
    requireRefusal(
        "overlapping-blocks",
        [&](std::vector<std::uint8_t>& file) {
            const std::vector<ObservedArtifact> observed = ObservePack(file);
            const auto artifact = std::ranges::find_if(observed, [](const ObservedArtifact& candidate) {
                return std::ranges::any_of(candidate.blocks, [](const ObservedBlock& block) {
                    return block.name == "lod-tail";
                });
            });
            Require(artifact != observed.end(), "The overlap fixture needs an artifact with two blocks");
            const auto overlapping = std::ranges::find_if(artifact->blocks, [](const ObservedBlock& block) {
                return block.name == "lod-tail";
            });
            PokeUInt64(file, overlapping->offsetFieldOffset, artifact->blocks.front().offset);
        },
        true, bake::AssetPackReadStatus::IndexCorrupt, "Two blocks pointed at one range were trusted");
    // A count is a 32-bit field a hostile header owns outright, and it sizes a reservation
    // before a single byte of the index has been looked at. kMaxAssetPackIndexBytes exists to
    // stop exactly that, and it does not cover this field.
    requireRefusal(
        "artifact-count-enormous",
        [](std::vector<std::uint8_t>& file) {
            PokeUInt32(file, kHeaderArtifactCountOffset, 0xFFFFFFFFU);
        },
        true, bake::AssetPackReadStatus::IndexCorrupt,
        "A header claiming four billion artifacts was allowed to size an allocation");
    requireRefusal(
        "zero-profile-fingerprint",
        [](std::vector<std::uint8_t>& file) { PokeUInt64(file, kHeaderProfileHashOffset, 0U); }, true,
        bake::AssetPackReadStatus::HeaderCorrupt,
        "A pack claiming a profile whose content nobody can invalidate was mounted");
    requireRefusal(
        "unnamed-profile",
        [](std::vector<std::uint8_t>& file) { file[kHeaderProfileIdLengthOffset] = 0U; }, true,
        bake::AssetPackReadStatus::HeaderCorrupt, "A pack that names no target profile was mounted");
    requireRefusal(
        "enormous-index",
        [](std::vector<std::uint8_t>& file) {
            PokeUInt64(file, kHeaderIndexBytesOffset, bake::kMaxAssetPackIndexBytes + 1U);
        },
        false, bake::AssetPackReadStatus::IndexCorrupt, "An index big enough to be an allocation attack was believed");

    // Truncation, from both sides of the length the pack records about itself.
    {
        std::vector<std::uint8_t> file = ReadFileBytes(goodPack);
        file.pop_back();
        const std::filesystem::path truncated = root / "truncated.kbpack";
        WriteFileBytes(truncated, file);
        bake::AssetPackReader reader;
        Require(reader.Mount(truncated) == bake::AssetPackReadStatus::SizeMismatch,
            "A pack shorter than its own recorded length was mounted");
    }
    {
        // Truncated AND re-stamped with its new length, so the size check cannot be what
        // catches it: the last block now runs past the end.
        std::vector<std::uint8_t> file = ReadFileBytes(goodPack);
        file.resize(file.size() - 16U);
        PokeUInt64(file, kHeaderFileBytesOffset, file.size());
        ResealIndex(file);
        const std::filesystem::path truncated = root / "truncated-restamped.kbpack";
        WriteFileBytes(truncated, file);
        bake::AssetPackReader reader;
        Require(reader.Mount(truncated) == bake::AssetPackReadStatus::BlockOutOfRange,
            "A truncated pack that re-stamped its own length was mounted");
    }
    {
        std::vector<std::uint8_t> file = ReadFileBytes(goodPack);
        file.resize(64U);
        const std::filesystem::path stub = root / "stub.kbpack";
        WriteFileBytes(stub, file);
        bake::AssetPackReader reader;
        Require(reader.Mount(stub) == bake::AssetPackReadStatus::Unreadable,
            "A file too short to hold a header was mounted");
    }
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(root / "no-such-file.kbpack") == bake::AssetPackReadStatus::Unreadable,
            "A pack that does not exist was mounted");
    }

    PurgeDirectory(root);
}

// Red when: publication writes through the destination, or copies instead of moving. Two
// independent observations, because a file-by-file copy once replaced an atomic rename and
// passed a whole suite unnoticed:
//   1. the published pack must BE the staging file object a hard link was taken on, which no
//      copy can satisfy;
//   2. a hard link taken on whatever sat at the destination beforehand must still read the OLD
//      bytes, which an in-place overwrite cannot satisfy.
void PublicationIsOneRenameOfAFinishedPack() {
    const std::filesystem::path root = TestRoot() / "atomic";
    const std::filesystem::path linkDirectory = TestRoot() / "atomic-links";
    PurgeDirectory(root);
    PurgeDirectory(linkDirectory);
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(linkDirectory);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("published" + std::string{ bake::kAssetPackFileExtension });

    // Something is already sitting on the destination, and something else holds a link to it.
    const std::vector<std::uint8_t> decoy = Bytes("a pack that was here before this one");
    WriteFileBytes(packPath, decoy);
    const std::filesystem::path decoyLink = linkDirectory / "decoy.hardlink";
    std::filesystem::create_hard_link(packPath, decoyLink);

    bake::AssetPackWriter writer{ packPath, profile };
    const bake::BakedAssetDescriptor descriptor = MakeDescriptor(11U);
    const std::vector<std::uint8_t> payload = Filler(11U, 900U);
    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Atomic pack BeginAsset failed");
    Require(writer.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
            bake::BakedAssetSinkStatus::Success,
        "Atomic pack WritePrimaryBlock failed");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Atomic pack CommitAsset failed");

    Require(std::filesystem::exists(writer.StagingPackPath()),
        "The writer has no staging file to publish, so publication cannot be a rename");
    Require(writer.StagingPackPath().filename() == "staging.kbpack" &&
            writer.StagingPackPath().parent_path().filename().generic_string().starts_with(".kbpack-work-"),
        "The writer did not place its staging file in an atomically-created private directory");
    const std::filesystem::path stagingLink = linkDirectory / "staging.hardlink";
    std::filesystem::create_hard_link(writer.StagingPackPath(), stagingLink);

    Require(ReadFileBytes(packPath) == decoy, "The destination changed before the pack was finished");

    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "Atomic pack Finish failed");

    Require(!std::filesystem::exists(writer.StagingPackPath()),
        "The staging pack survived publication, so it was copied rather than moved");
    // Observation one: whatever was at the destination was REPLACED, not written into. A
    // publisher that opened the destination and filled it would leave a reader holding a name
    // that claims to be a pack while it is still being written.
    Require(ReadFileBytes(decoyLink) == decoy,
        "The file that was at the destination was written through, so a reader could observe a half-written pack");
    // Observation two, independent of the first: the published pack IS the file that was
    // staged. A publisher that unlinked the destination and copied onto it satisfies the
    // first observation and fails this one.
    Require(std::filesystem::equivalent(stagingLink, packPath),
        "The published pack is a different file object than the staging file, so publication copied instead of moving");
    Require(ReadFileBytes(stagingLink) == ReadFileBytes(packPath),
        "The published pack and the staged file it came from do not hold the same bytes");

    bake::AssetPackReader reader;
    Require(reader.Mount(packPath) == bake::AssetPackReadStatus::Success, "The published pack does not mount");
    std::vector<std::uint8_t> read;
    Require(reader.ReadBlock(*reader.FindArtifact(descriptor.key.Digest()), "primary", read) ==
            bake::AssetPackReadStatus::Success && read == payload,
        "The published pack lost its payload");
    reader.Unmount();

    // No staging debris may outlive a successful publication. POSIX intentionally keeps the
    // destination lock inode so another writer cannot lock a replacement inode while an older
    // writer still holds the original; that coordination file is not staging debris.
    std::size_t strays = 0U;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ root }) {
        const bool persistentPosixLock =
            entry.path().filename().generic_string().ends_with(".kbpacklock");
        strays += (entry.path() != packPath && !persistentPosixLock) ? 1U : 0U;
    }
    Require(strays == 0U, "A published pack left staging files behind");

    PurgeDirectory(linkDirectory);
    PurgeDirectory(root);
}

// Red when the writer returns to deterministic staging leaves beside the destination. Such a
// leaf may be a stale or hostile symlink; opening it with truncation overwrites its target.
void PreexistingStagingSymlinksNeverReachTheirSentinel() {
    const std::filesystem::path root = TestRoot() / "staging-symlink";
    const std::filesystem::path sentinelRoot = TestRoot() / "staging-symlink-sentinel";
    PurgeDirectory(root);
    PurgeDirectory(sentinelRoot);
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(sentinelRoot);

    const std::filesystem::path packPath = root / "published.kbpack";
    const std::filesystem::path sentinel = sentinelRoot / "must-remain-unchanged.bin";
    const std::vector<std::uint8_t> sentinelBytes = Bytes("staging symlink sentinel");
    WriteFileBytes(sentinel, sentinelBytes);
    const std::string packFileName = packPath.filename().generic_string();
    const std::filesystem::path legacyPayload = root / (packFileName + ".kbpackpayload");
    const std::filesystem::path legacyPack = root / (packFileName + ".kbpackstaging");

    std::error_code linkError;
    std::filesystem::create_symlink(sentinel, legacyPayload, linkError);
    bool symbolicLinksAvailable = !linkError;
    if (symbolicLinksAvailable) {
        std::filesystem::create_symlink(sentinel, legacyPack, linkError);
        symbolicLinksAvailable = !linkError;
    }
    if (!symbolicLinksAvailable) {
        // Windows without Developer Mode cannot create a symlink in a normal test process.
        // A hard link exercises the same forbidden truncation without privilege, so the test
        // remains red against the old deterministic staging leaves on every test host.
        linkError.clear();
        std::filesystem::remove(legacyPayload, linkError);
        linkError.clear();
        std::filesystem::remove(legacyPack, linkError);
        linkError.clear();
        std::filesystem::create_hard_link(sentinel, legacyPayload, linkError);
        Require(!linkError, "Private-staging regression could not create its payload link");
        std::filesystem::create_hard_link(sentinel, legacyPack, linkError);
        Require(!linkError, "Private-staging regression could not create its pack link");
    }

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::AssetPackWriter writer{ packPath, profile };
    const bake::BakedAssetDescriptor descriptor = MakeDescriptor(12U);
    const std::vector<std::uint8_t> payload = Filler(12U, 256U);
    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
        "Private-staging regression could not begin an artifact");
    Require(writer.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
            bake::BakedAssetSinkStatus::Success,
        "Private-staging regression could not write its payload");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success &&
            writer.Finish() == bake::BakedAssetSinkStatus::Success,
        "Private-staging regression could not publish its package");
    Require(ReadFileBytes(sentinel) == sentinelBytes,
        "The pack writer followed a pre-existing staging symlink and overwrote its sentinel");
    if (symbolicLinksAvailable) {
        Require(std::filesystem::is_symlink(std::filesystem::symlink_status(legacyPayload)) &&
                std::filesystem::is_symlink(std::filesystem::symlink_status(legacyPack)),
            "The pack writer consumed deterministic staging symlinks it does not own");
    } else {
        Require(std::filesystem::equivalent(legacyPayload, sentinel) &&
                std::filesystem::equivalent(legacyPack, sentinel),
            "The pack writer consumed deterministic staging links it does not own");
    }

    PurgeDirectory(sentinelRoot);
    PurgeDirectory(root);
}

// Red when: a failure or an abandonment reaches the destination. A pack that is never finished
// must leave the store exactly as it was - including a pack that was already there.
void AnUnfinishedPackNeverReachesTheDestination() {
    const std::filesystem::path root = TestRoot() / "unfinished";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("kept" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<WrittenArtifact> expected = BakeSamplePack(packPath, profile);
    const std::vector<std::uint8_t> published = ReadFileBytes(packPath);

    {
        bake::AssetPackWriter writer{ packPath, profile };
        Require(writer.BeginAsset(MakeDescriptor(21U)) == bake::BakedAssetSinkStatus::Success,
            "Unfinished pack BeginAsset failed");
        Require(writer.WritePrimaryBlock(Filler(21U, 64U), profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "Unfinished pack WritePrimaryBlock failed");
        Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Unfinished pack CommitAsset failed");
        // No Finish: the writer goes out of scope with a whole artifact taken.
    }

    Require(ReadFileBytes(packPath) == published,
        "A pack that was never finished replaced the one that was already published");
    std::size_t strays = 0U;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ root }) {
        const bool persistentPosixLock =
            entry.path().filename().generic_string().ends_with(".kbpacklock");
        strays += (entry.path() != packPath && !persistentPosixLock) ? 1U : 0U;
    }
    Require(strays == 0U, "An abandoned pack writer left its staging files behind");

    // An aborted artifact leaves no trace in a pack that IS finished: the bytes it wrote must
    // not appear in the published file and its key must not be in the catalogue.
    const std::filesystem::path secondPack = root / ("aborted" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<std::uint8_t> abandoned = Filler(31U, 777U);
    {
        bake::AssetPackWriter writer{ secondPack, profile };
        Require(writer.BeginAsset(MakeDescriptor(31U)) == bake::BakedAssetSinkStatus::Success,
            "Aborted pack BeginAsset failed");
        Require(writer.WritePrimaryBlock(abandoned, profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "Aborted pack WritePrimaryBlock failed");
        writer.AbortAsset();
        Require(writer.BeginAsset(MakeDescriptor(32U)) == bake::BakedAssetSinkStatus::Success,
            "A pack writer would not take an artifact after an abort");
        Require(writer.WritePrimaryBlock(Filler(32U, 128U), profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "Aborted pack second WritePrimaryBlock failed");
        Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Aborted pack CommitAsset failed");
        Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "Aborted pack Finish failed");
    }
    bake::AssetPackReader reader;
    Require(reader.Mount(secondPack) == bake::AssetPackReadStatus::Success, "The pack after an abort does not mount");
    Require(reader.Artifacts().size() == 1U && reader.FindArtifact(MakeKey(31U).Digest()) == nullptr,
        "An aborted artifact reached the published catalogue");
    const std::vector<std::uint8_t> file = ReadFileBytes(secondPack);
    Require(std::search(file.begin(), file.end(), abandoned.begin(), abandoned.end()) == file.end(),
        "An aborted artifact's bytes reached the published pack");

    PurgeDirectory(root);
}

// Red when: the writer takes work it cannot honour, or takes work after it has published.
// Same protocol as the loose sink, because a baker must not be able to tell the two apart.
void TheWriterRefusesProtocolViolations() {
    const std::filesystem::path root = TestRoot() / "protocol";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("protocol" + std::string{ bake::kAssetPackFileExtension });
    bake::AssetPackWriter writer{ packPath, profile };

    const std::vector<std::uint8_t> payload = Filler(41U, 40U);
    Require(writer.WritePrimaryBlock(payload, 256U) == bake::BakedAssetSinkStatus::NoAssetOpen,
        "The writer took a primary block with no artifact open");
    Require(writer.WriteAuxiliaryBlock({ .name = "extra", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::NoAssetOpen,
        "The writer took an auxiliary block with no artifact open");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::NoAssetOpen,
        "The writer committed with no artifact open");

    bake::BakedAssetDescriptor invalidKey = MakeDescriptor(51U);
    invalidKey.key.targetProfileHash = 0U;
    Require(writer.BeginAsset(invalidKey) == bake::BakedAssetSinkStatus::InvalidKey,
        "The writer took an artifact whose key carries no profile fingerprint");
    bake::BakedAssetDescriptor invalidType = MakeDescriptor(51U);
    invalidType.assetTypeId = "not/a/name";
    Require(writer.BeginAsset(invalidType) == bake::BakedAssetSinkStatus::InvalidAssetType,
        "The writer took an artifact whose type is not a portable name");
    bake::BakedAssetDescriptor wrongProfile = MakeDescriptor(51U);
    wrongProfile.key.targetProfileId = "Android.ASTC.arm64";
    wrongProfile.key.targetProfileHash =
        bake::BakeTargetProfileFingerprint(bake::AndroidAstcArm64BakeTargetProfile());
    Require(writer.BeginAsset(wrongProfile) == bake::BakedAssetSinkStatus::InvalidProfile,
        "A Windows pack accepted an artifact baked for Android");

    const bake::BakedAssetDescriptor descriptor = MakeDescriptor(51U);
    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Protocol pack BeginAsset failed");
    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::AssetAlreadyOpen,
        "The writer opened a second artifact while one was still open");
    Require(writer.Finish() == bake::BakedAssetSinkStatus::AssetAlreadyOpen,
        "The writer published a pack while an artifact was still open, silently dropping it");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::MissingPrimaryBlock,
        "The writer committed an artifact with no payload");
    Require(writer.WritePrimaryBlock({}, 256U) == bake::BakedAssetSinkStatus::EmptyBlock,
        "The writer took an empty primary block");
    Require(writer.WritePrimaryBlock(payload, 0U) == bake::BakedAssetSinkStatus::InvalidAlignment,
        "The writer took a zero alignment");
    Require(writer.WritePrimaryBlock(payload, 96U) == bake::BakedAssetSinkStatus::InvalidAlignment,
        "The writer took an alignment that is not a power of two");
    Require(writer.WritePrimaryBlock(payload, 256U) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack WritePrimaryBlock failed");
    Require(writer.WritePrimaryBlock(payload, 256U) == bake::BakedAssetSinkStatus::DuplicateBlock,
        "The writer took a second primary block");
    Require(writer.WriteAuxiliaryBlock({ .name = "primary", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::InvalidBlockName,
        "The writer took an auxiliary block under the reserved primary name");
    Require(writer.WriteAuxiliaryBlock({ .name = "PRIMARY", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::InvalidBlockName,
        "The writer took the reserved primary name in a different case");
    Require(writer.WriteAuxiliaryBlock({ .name = "aux", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::InvalidBlockName,
        "The writer took a reserved Win32 device name as a block name");
    Require(writer.WriteAuxiliaryBlock({ .name = "extra", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::Success,
        "Protocol pack WriteAuxiliaryBlock failed");
    Require(writer.WriteAuxiliaryBlock({ .name = "EXTRA", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::DuplicateBlock,
        "The writer took two block names that differ only in case, which is one file name on a real target");
    Require(writer.WriteAuxiliaryBlock({ .name = "", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::InvalidBlockName,
        "The writer took an unnamed block");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Protocol pack CommitAsset failed");

    // A block bigger than one range request may be: refused BEFORE a byte of it is written.
    Require(writer.BeginAsset(MakeDescriptor(52U)) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack BeginAsset for the oversized block failed");
    {
        const std::vector<std::uint8_t> oversized(static_cast<std::size_t>(bake::kMaxAssetPackBlockBytes) + 1U, 0xAB);
        Require(writer.WritePrimaryBlock(oversized, 256U) == bake::BakedAssetSinkStatus::BlockTooLarge,
            "The writer took a block larger than one range request can carry");
    }
    writer.AbortAsset();

    // The same key may be deduplicated only when it describes the same complete artifact.
    // Otherwise caller order would decide which layout or bytes are published under the digest.
    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Protocol pack re-bake failed");
    Require(writer.WritePrimaryBlock(payload, 256U) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack re-bake WritePrimaryBlock failed");
    Require(writer.WriteAuxiliaryBlock({ .name = "extra", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::Success,
        "Protocol pack re-bake WriteAuxiliaryBlock failed");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success,
        "The writer refused the same artifact baked twice");

    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack layout-collision BeginAsset failed");
    Require(writer.WritePrimaryBlock(payload, 256U) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack layout-collision WritePrimaryBlock failed");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::InvalidKey,
        "The writer deduplicated one digest and type with a different block set");
    writer.AbortAsset();

    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack alignment-collision BeginAsset failed");
    Require(writer.WritePrimaryBlock(payload, 512U) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack alignment-collision WritePrimaryBlock failed");
    Require(writer.WriteAuxiliaryBlock({ .name = "extra", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::Success,
        "Protocol pack alignment-collision WriteAuxiliaryBlock failed");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::InvalidKey,
        "The writer deduplicated one digest and type with a different effective alignment");
    writer.AbortAsset();

    Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack payload-collision BeginAsset failed");
    Require(writer.WritePrimaryBlock(Filler(42U, payload.size()), 256U) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack payload-collision WritePrimaryBlock failed");
    Require(writer.WriteAuxiliaryBlock({ .name = "extra", .alignmentBytes = 256U }, payload) ==
            bake::BakedAssetSinkStatus::Success,
        "Protocol pack payload-collision WriteAuxiliaryBlock failed");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::InvalidKey,
        "The writer deduplicated one digest and type with different block bytes");
    writer.AbortAsset();

    bake::BakedAssetDescriptor clashing = descriptor;
    clashing.assetTypeId = "Texture2D";
    Require(writer.BeginAsset(clashing) == bake::BakedAssetSinkStatus::Success, "Protocol pack clash BeginAsset failed");
    Require(writer.WritePrimaryBlock(payload, 256U) == bake::BakedAssetSinkStatus::Success,
        "Protocol pack clash WritePrimaryBlock failed");
    Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::InvalidAssetType,
        "The writer let one key stand for two different asset types");
    writer.AbortAsset();

    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "Protocol pack Finish failed");
    Require(writer.Finish() == bake::BakedAssetSinkStatus::PackAlreadyFinished,
        "The writer published the same pack twice");
    Require(writer.BeginAsset(MakeDescriptor(53U)) == bake::BakedAssetSinkStatus::PackAlreadyFinished,
        "The writer took an artifact after it had published its pack");

    bake::AssetPackReader reader;
    Require(reader.Mount(packPath) == bake::AssetPackReadStatus::Success, "The protocol pack does not mount");
    Require(reader.Artifacts().size() == 1U, "The same artifact baked twice became two entries in the catalogue");

    // A profile the bake rules reject must not become a pack whose placement nobody can check.
    bake::BakeTargetProfile broken = profile;
    broken.packageBlockAlignmentBytes = 96U;
    bake::AssetPackWriter brokenWriter{ root / "broken.kbpack", broken };
    Require(brokenWriter.BeginAsset(MakeDescriptor(54U)) == bake::BakedAssetSinkStatus::InvalidProfile,
        "A pack writer accepted work for a profile that is not bakeable");
    Require(brokenWriter.Finish() == bake::BakedAssetSinkStatus::InvalidProfile,
        "A pack writer published a pack for a profile that is not bakeable");
    Require(!std::filesystem::exists(root / "broken.kbpack"), "A refused profile still produced a pack");

    PurgeDirectory(root);
}

// Red when: the path budget is checked after something has been created, or not at all. A pack
// path that the platform cannot open must be refused before the first byte, because a file
// that cannot be opened cannot be repaired either.
void AnOverlongPackPathIsRefusedBeforeAnythingIsWritten() {
    const std::filesystem::path root = TestRoot() / "long";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    std::filesystem::path overlong = root;
    while (overlong.native().size() < bake::kMaxBakeStorePathLength) {
        overlong /= std::string(60U, 'p');
    }
    overlong /= "pack.kbpack";

    bake::AssetPackWriter writer{ overlong, profile };
    Require(writer.BeginAsset(MakeDescriptor(61U)) == bake::BakedAssetSinkStatus::PathTooLong,
        "A pack path past the portable ceiling was accepted");
    Require(writer.Finish() == bake::BakedAssetSinkStatus::PathTooLong,
        "A pack path past the portable ceiling was published to");
    std::error_code error;
    Require(std::filesystem::is_empty(root, error) && !error,
        "A refused pack path still created something on disk");

    // A long but legal path is published and read back, so the budget is a budget and not a
    // blanket refusal -- and the path clears Win32's MAX_PATH, so it is also the proof that the
    // writer addresses its store through an extended-length path.
    std::filesystem::path longButLegal = root;
    std::size_t appendedComponents = 0U;
    while (longButLegal.native().size() < 320U) {
        longButLegal /= std::string(60U, 'q');
        ++appendedComponents;
    }
    longButLegal /= "pack.kbpack";
    Require(longButLegal.native().size() > 260U,
        "Test setup: the long pack path no longer clears the Win32 MAX_PATH budget");
    bake::AssetPackWriter legalWriter{ longButLegal, profile };
    const bake::BakedAssetDescriptor descriptor = MakeDescriptor(62U);
    const std::vector<std::uint8_t> payload = Filler(62U, 512U);
    Require(legalWriter.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success,
        "A long but legal pack path was refused");
    Require(legalWriter.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
            bake::BakedAssetSinkStatus::Success,
        "A long but legal pack path could not take a block");
    Require(legalWriter.CommitAsset() == bake::BakedAssetSinkStatus::Success,
        "A long but legal pack path could not commit");
    Require(legalWriter.Finish() == bake::BakedAssetSinkStatus::Success,
        "A long but legal pack path could not be published");

    bake::AssetPackReader reader;
    Require(reader.Mount(legalWriter.PackPath()) == bake::AssetPackReadStatus::Success,
        "A pack published under a long path could not be opened again");
    std::vector<std::uint8_t> read;
    Require(reader.ReadBlock(*reader.FindArtifact(descriptor.key.Digest()), "primary", read) ==
            bake::AssetPackReadStatus::Success && read == payload,
        "A pack published under a long path could not be read back");
    reader.Unmount();

    // Only the writer's own paths are extended-length enough to delete a tree this deep, so
    // the cleanup walks back up from the one it published to.
    std::filesystem::path cleanupRoot = legalWriter.PackPath();
    for (std::size_t index = 0U; index <= appendedComponents; ++index) {
        cleanupRoot = cleanupRoot.parent_path();
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(cleanupRoot, cleanupError);
    Require(!cleanupError && !std::filesystem::exists(cleanupRoot),
        "A pack published under a long path cannot be deleted again");
    PurgeDirectory(root);
}

// Red when: anything in the reader scales with the number of assets. Android caps a process at
// 4096 memory allocations, so one handle or one buffer per PACK is a hard rule, not a
// preference - and an empty pack has to be a pack too.
void ManyArtifactsShareOneHandle() {
    const std::filesystem::path root = TestRoot() / "many";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("many" + std::string{ bake::kAssetPackFileExtension });
    constexpr std::uint64_t kArtifactCount = 300U;

    {
        bake::AssetPackWriter writer{ packPath, profile };
        for (std::uint64_t index = 0U; index < kArtifactCount; ++index) {
            Require(writer.BeginAsset(MakeDescriptor(1000U + index)) == bake::BakedAssetSinkStatus::Success,
                "The pack writer refused one of many artifacts");
            Require(writer.WritePrimaryBlock(Filler(1000U + index, 100U + static_cast<std::size_t>(index % 37U)),
                        profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success,
                "The pack writer refused one of many primary blocks");
            Require(writer.CommitAsset() == bake::BakedAssetSinkStatus::Success,
                "The pack writer refused to commit one of many artifacts");
        }
        Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "The many-artifact pack was not published");
    }

    for (const bake::AssetPackAccess access : { bake::AssetPackAccess::Ranged, bake::AssetPackAccess::WholeFile }) {
        bake::AssetPackReader reader;
        Require(reader.Mount(packPath, access) == bake::AssetPackReadStatus::Success,
            "The many-artifact pack does not mount");
        Require(reader.Artifacts().size() == kArtifactCount, "The many-artifact pack lost artifacts");
        for (std::uint64_t index = 0U; index < kArtifactCount; ++index) {
            const bake::AssetPackArtifactEntry* entry = reader.FindArtifact(MakeKey(1000U + index).Digest());
            Require(entry != nullptr, "The many-artifact pack lost one of its keys");
            std::vector<std::uint8_t> read;
            Require(reader.ReadBlock(*entry, "primary", read) == bake::AssetPackReadStatus::Success &&
                    read == Filler(1000U + index, 100U + static_cast<std::size_t>(index % 37U)),
                "The many-artifact pack handed back the wrong bytes for one of its artifacts");
        }
        Require(reader.OpenCount() == 1U,
            "The reader opened the pack more than once, so its handle count scales with the asset count");
    }

    // A pack with nothing in it is still a pack: the header and an empty index.
    const std::filesystem::path emptyPack = root / ("empty" + std::string{ bake::kAssetPackFileExtension });
    {
        bake::AssetPackWriter writer{ emptyPack, profile };
        Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "An empty pack was not published");
    }
    bake::AssetPackReader emptyReader;
    Require(emptyReader.Mount(emptyPack) == bake::AssetPackReadStatus::Success, "An empty pack does not mount");
    Require(emptyReader.Artifacts().empty(), "An empty pack claims to hold something");
    emptyReader.Unmount();

    PurgeDirectory(root);
}

// Red when: the whole-file fallback is not one, i.e. when WholeFile quietly answers through
// the ranged path. Both modes hand back the same bytes from a file that is sitting still, so
// "the bytes match" cannot tell them apart and a reader that dropped the buffer entirely
// passes every other test in this file. The property that actually distinguishes them is that
// a WholeFile mount does not touch the file again: the pack is already in the reader.
//
// This is not an optimisation to be relaxed. HTTP range support is a MAY (RFC 9110 14.2), and
// a packaged Android build whose AAsset_openFileDescriptor refuses has the same problem, so
// this mode is the only one some hosts leave us. If it silently seeks, it works on a desktop
// and fails on exactly the hosts it exists for.
void TheWholeFileFallbackReallyReadsTheWholeFile() {
    const std::filesystem::path root = TestRoot() / "wholefile";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("fallback" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<WrittenArtifact> expected = BakeSamplePack(packPath, profile);
    const std::vector<std::uint8_t> good = ReadFileBytes(packPath);

    bake::AssetPackReader wholeFile;
    Require(wholeFile.Mount(packPath, bake::AssetPackAccess::WholeFile) == bake::AssetPackReadStatus::Success,
        "A pack did not mount through the whole-file fallback");

    // The file underneath is replaced with something that is not a pack at all. A reader that
    // took the whole file at mount does not notice; a reader that seeks per block hands back
    // the rubbish.
    WriteFileBytes(packPath, std::vector<std::uint8_t>(good.size(), 0xEEU));
    RequireMountedPackMatches(wholeFile, expected,
        "The whole-file fallback went back to the file for a block, so it is the ranged path wearing another name");
    Require(wholeFile.OpenCount() == 1U, "The whole-file fallback opened the pack more than once");

    // ...and it does not hold the file either, which is the same fact from the other side and
    // the one an Android build needs: the descriptor is gone once the bytes are in.
    std::error_code removeError;
    Require(std::filesystem::remove(packPath, removeError) && !removeError,
        "A whole-file mount still holds the pack file open, so it never read the whole file");
    RequireMountedPackMatches(wholeFile, expected,
        "A whole-file mount stopped answering once its file was gone");
    wholeFile.Unmount();

    // The ranged reader is the other half of the contract: it DOES go back to the file, which
    // is what makes the two modes two modes. Asserted so that a change collapsing them into
    // one is visible from both sides rather than only from the fallback's.
    WriteFileBytes(packPath, good);
    bake::AssetPackReader ranged;
    Require(ranged.Mount(packPath, bake::AssetPackAccess::Ranged) == bake::AssetPackReadStatus::Success,
        "A pack did not mount through the ranged reader");
#if defined(_WIN32)
    std::error_code busyError;
    Require(!std::filesystem::remove(packPath, busyError),
        "A ranged mount does not hold the pack open, so it is not reading through a handle at all");
#endif
    ranged.Unmount();

    PurgeDirectory(root);
}

// Red when: the Android package path copies the whole container into a second engine-owned
// buffer, reopens a filesystem path that does not exist inside an APK, or skips the same
// hostile-input validation the file-backed mounts use.
void BorrowedPackageMemoryUsesTheValidatedReaderPath() {
    const std::filesystem::path root = TestRoot() / "borrowed-memory";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("mapped" + std::string{ bake::kAssetPackFileExtension });
    const std::vector<WrittenArtifact> expected = BakeSamplePack(packPath, profile);
    const std::vector<std::uint8_t> mappedBytes = ReadFileBytes(packPath);

    bake::AssetPackReader reader;
    Require(reader.MountMemory(mappedBytes) == bake::AssetPackReadStatus::Success,
        "A valid package memory view did not mount");
    Require(reader.OpenCount() == 0U,
        "Mounting an existing package memory view opened a second handle");
    RequireMountedPackMatches(reader, expected,
        "A package mounted from borrowed memory did not return the bytes the writer published");

    // APK assets do not have ordinary filesystem paths. Removing the temporary file proves
    // that every block is now served from the caller-owned view rather than by reopening it.
    std::error_code removeError;
    Require(std::filesystem::remove(packPath, removeError) && !removeError,
        "The borrowed-memory mount unexpectedly kept the source file open");
    RequireMountedPackMatches(reader, expected,
        "A borrowed-memory mount went back to the removed source file");
    reader.Unmount();

    std::vector<std::uint8_t> hostile = mappedBytes;
    hostile[kHeaderMagicOffset] ^= 0xFFU;
    Require(reader.MountMemory(hostile) == bake::AssetPackReadStatus::NotAnAssetPack,
        "A hostile memory view bypassed the normal package-header validation");
    Require(!reader.IsMounted(), "A refused memory view remained mounted");

    PurgeDirectory(root);
}

// Red when: the alignment helper wraps instead of refusing. The overflow branch is
// unreachable through any pack that fits kMaxAssetPackBytes -- it would take a file near 2^64
// -- so it is tested here directly, against the arithmetic rather than against a fixture.
void AligningAnOffsetRefusesToWrap() {
    constexpr std::uint64_t kMax = ~static_cast<std::uint64_t>(0U);
    std::uint64_t aligned = 0U;

    Require(bake::TryAlignAssetPackOffset(0U, 256U, aligned) && aligned == 0U,
        "Aligning zero moved it");
    Require(bake::TryAlignAssetPackOffset(1U, 256U, aligned) && aligned == 256U,
        "Aligning one did not reach the first boundary");
    Require(bake::TryAlignAssetPackOffset(256U, 256U, aligned) && aligned == 256U,
        "Aligning a value already on its boundary moved it");
    Require(bake::TryAlignAssetPackOffset(257U, 65536U, aligned) && aligned == 65536U,
        "Aligning to the mapping granularity did not reach it");
    Require(bake::TryAlignAssetPackOffset(kMax, 1U, aligned) && aligned == kMax,
        "Aligning to one is not the identity");

    // One past the last 256-boundary below 2^64: rounding up wraps to zero, and a wrapped
    // offset passes every "is it inside the file?" test there is.
    aligned = 0xDEADBEEFU;
    Require(!bake::TryAlignAssetPackOffset(kMax, 256U, aligned),
        "An offset one below 2^64 was rounded up past the end of the address space");
    Require(aligned == 0xDEADBEEFU, "A refused alignment wrote to its output anyway");
    Require(!bake::TryAlignAssetPackOffset(kMax - 254U, 256U, aligned),
        "The last offset that cannot be aligned without wrapping was aligned anyway");
    Require(bake::TryAlignAssetPackOffset(kMax - 255U, 256U, aligned) && aligned == kMax - 255U,
        "The last offset that CAN be aligned was refused");

    // Not a power of two, and zero, are refusals rather than divisions by zero.
    Require(!bake::TryAlignAssetPackOffset(0U, 0U, aligned), "Zero was accepted as an alignment");
    Require(!bake::TryAlignAssetPackOffset(0U, 96U, aligned), "A non-power-of-two was accepted as an alignment");
}

// Red when: a second writer aimed at the same package can share or truncate the first one's
// staging files. Destination ownership is exclusive from the first staged byte through the
// publication rename.
void TwoWritersOnOnePackDoNotForgeEachOthersBytes() {
    const std::filesystem::path root = TestRoot() / "two-writers";
    PurgeDirectory(root);
    std::filesystem::create_directories(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = root / ("shared" + std::string{ bake::kAssetPackFileExtension });

    // Large enough that neither writer's bytes can be sitting unflushed in a stream buffer:
    // with a small block the first writer's own flush happened to repair the damage, which is
    // luck, not a guarantee.
    const std::vector<std::uint8_t> alphaBytes = Filler(701U, 300000U);
    const std::vector<std::uint8_t> betaBytes = Filler(702U, 300000U);
    const bake::BakedAssetDescriptor alphaDescriptor = MakeDescriptor(71U);
    const bake::BakedAssetDescriptor betaDescriptor = MakeDescriptor(72U);

    {
        bake::AssetPackWriter alpha{ packPath, profile };
        Require(alpha.BeginAsset(alphaDescriptor) == bake::BakedAssetSinkStatus::Success,
            "The first writer could not begin its artifact");
        Require(alpha.WritePrimaryBlock(alphaBytes, profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "The first writer could not write its primary block");
        Require(alpha.CommitAsset() == bake::BakedAssetSinkStatus::Success,
            "The first writer could not commit its artifact");

        {
            bake::AssetPackWriter beta{ packPath, profile };
            Require(beta.BeginAsset(betaDescriptor) == bake::BakedAssetSinkStatus::StagingConflict,
                "A second writer acquired a destination already owned by the first writer");
        }

        Require(alpha.Finish() == bake::BakedAssetSinkStatus::Success,
            "The destination owner could not publish after a competing writer was refused");
    }

    bake::AssetPackReader reader;
    Require(reader.Mount(packPath) == bake::AssetPackReadStatus::Success,
        "The package owner published a package that does not mount");
    Require(reader.Artifacts().size() == 1U && reader.FindArtifact(betaDescriptor.key.Digest()) == nullptr,
        "A refused writer still put its artifact into the published package");
    const bake::AssetPackArtifactEntry* entry = reader.FindArtifact(alphaDescriptor.key.Digest());
    Require(entry != nullptr, "The package lost its owner's artifact");
    std::vector<std::uint8_t> read;
    Require(reader.ReadBlock(*entry, "primary", read) == bake::AssetPackReadStatus::Success,
        "The published block could not be read back");
    // The assertion that actually catches the defect: whatever is under this key must be the
    // bytes baked under it, not the other writer's and not a mixture of the two.
    Require(read == alphaBytes, "A published block holds bytes that were never baked under its key");
    Require(read != betaBytes, "A published block holds the refused writer's bytes");
    reader.Unmount();

    // A writer that refused must not have left its staging files behind either.
    std::size_t strays = 0U;
    for (const std::filesystem::directory_entry& stray : std::filesystem::directory_iterator{ root }) {
        const bool persistentPosixLock =
            stray.path().filename().generic_string().ends_with(".kbpacklock");
        strays += (stray.path() != packPath && !persistentPosixLock) ? 1U : 0U;
    }
    Require(strays == 0U, "A refused writer left its staging files behind");

    PurgeDirectory(root);
}

} // namespace

void RunAssetPackTests() {
    EveryBlockSurvivesTheContainer();
    ProfileMatchIncludesRecordedAlignments();
    CorruptPayloadIsNeverReturned();
    PackageBytesDoNotDependOnArtifactOrder();
    RuntimeManifestIsCanonicalAndHostileInputSafe();
    AlignmentIsMeasuredInTheFileNotReported();
    AHostilePackIsRefusedRatherThanTrusted();
    PublicationIsOneRenameOfAFinishedPack();
    PreexistingStagingSymlinksNeverReachTheirSentinel();
    AnUnfinishedPackNeverReachesTheDestination();
    TheWriterRefusesProtocolViolations();
    AnOverlongPackPathIsRefusedBeforeAnythingIsWritten();
    ManyArtifactsShareOneHandle();
    TheWholeFileFallbackReallyReadsTheWholeFile();
    BorrowedPackageMemoryUsesTheValidatedReaderPath();
    AligningAnOffsetRefusesToWrap();
    TwoWritersOnOnePackDoNotForgeEachOthersBytes();
    PurgeDirectory(TestRoot());
}

} // namespace kb::tests
