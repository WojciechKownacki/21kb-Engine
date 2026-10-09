// The content streaming readers: zstd-compressed .kbpack blocks (one at a time, as the async
// read pool decodes them), pack set indexes with the base, chunk and patch packs they mount, and
// the streamed texture mips and mesh levels of detail those packs carry.
#include "FuzzSupport.hpp"

#include "assets/bake/AssetPackCompression.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSet.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace bake = kb::assets::bake;

// Up to `maxRecords` records of [u32 little-endian length][bytes]; a length past the end takes
// what is left.
[[nodiscard]] std::vector<std::span<const std::uint8_t>> Records(
    std::span<const std::uint8_t> bytes,
    std::size_t maxRecords) {
    std::vector<std::span<const std::uint8_t>> records;
    while (bytes.size() >= 4U && records.size() < maxRecords) {
        const std::size_t length = static_cast<std::size_t>(bytes[0]) | (static_cast<std::size_t>(bytes[1]) << 8U) |
            (static_cast<std::size_t>(bytes[2]) << 16U) | (static_cast<std::size_t>(bytes[3]) << 24U);
        bytes = bytes.subspan(4U);
        const std::size_t taken = std::min(length, bytes.size());
        records.push_back(bytes.first(taken));
        bytes = bytes.subspan(taken);
    }
    return records;
}

// Every block of one pack: the stored bytes, then decoded both as the async pool does it (stored
// bytes handed back to the reader) and as a synchronous read does it.
void ReadEveryBlock(bake::AssetPackReader& reader) {
    std::vector<std::uint8_t> stored;
    std::vector<std::uint8_t> block;
    for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const bake::AssetPackBlockEntry& entry : artifact.blocks) {
            if (reader.ReadStoredBlock(artifact, entry.name, stored) == bake::AssetPackReadStatus::Success) {
                if (const bake::AssetPackBlockEntry* found = reader.FindBlock(artifact, entry.name); found != nullptr) {
                    static_cast<void>(reader.DecodeStoredBlock(*found, stored));
                }
            }
            static_cast<void>(reader.ReadBlock(artifact, entry.name, block));
        }
    }
}

// Mode 0: the input is one pack; its first three bytes also claim an uncompressed size for the
// raw block decoder, which sees the rest as one stored block.
void FuzzPack(std::span<const std::uint8_t> bytes) {
    bake::AssetPackReader reader;
    if (reader.MountMemory(bytes) == bake::AssetPackReadStatus::Success) {
        ReadEveryBlock(reader);
    }
    if (bytes.size() > 3U) {
        const std::uint64_t claimed =
            bytes[0] | (static_cast<std::uint64_t>(bytes[1]) << 8U) | (static_cast<std::uint64_t>(bytes[2]) << 16U);
        std::vector<std::uint8_t> decoded;
        static_cast<void>(bake::DecompressAssetPackBlock(bytes.subspan(3U), claimed, decoded));
    }
}

// Mode 1: records; the first is a pack set index, the rest are written beside it as
// pack0.kbpack, pack1.kbpack, ... for the index to name.
void FuzzPackSet(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaxPacks = 4U;
    const std::vector<std::span<const std::uint8_t>> records = Records(bytes, kMaxPacks + 1U);
    if (records.empty()) {
        return;
    }
    const std::string_view indexText{ reinterpret_cast<const char*>(records[0].data()), records[0].size() };
    bake::AssetPackSetIndex parsed{};
    if (bake::ParseAssetPackSetIndex(indexText, parsed) == bake::AssetPackSetStatus::Success) {
        static_cast<void>(bake::EncodeAssetPackSetIndex(parsed));
    }

    const std::filesystem::path index =
        kb::fuzz::WriteScratchFile(records[0].data(), records[0].size(), bake::kAssetPackSetFileName);
    for (std::size_t pack = 0U; pack < kMaxPacks; ++pack) {
        const std::string name = "pack" + std::to_string(pack) + ".kbpack";
        if (pack + 1U < records.size()) {
            static_cast<void>(kb::fuzz::WriteScratchFile(records[pack + 1U].data(), records[pack + 1U].size(), name));
        } else {
            std::error_code error;
            std::filesystem::remove(index.parent_path() / name, error);
        }
    }
    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::RuntimeAssetPack set;
    if (set.MountSetIndex(index, profile) != bake::RuntimeAssetPackStatus::Success) {
        return;
    }
    std::vector<std::uint8_t> block;
    for (std::uint32_t container = 0U; container < set.ContainerCount(); ++container) {
        for (const bake::AssetPackArtifactEntry& artifact : set.ContainerArtifacts(container)) {
            for (const bake::AssetPackBlockEntry& entry : artifact.blocks) {
                static_cast<void>(set.ReadContainerBlock(container, artifact, entry.name, block));
                bake::RuntimeAssetBlockLocation location{};
                if (set.LocateArtifactBlock(artifact.key, entry.name, location) == bake::AssetPackReadStatus::Success) {
                    // The stored bytes, read the way the async pool reads them: straight from the
                    // pack file at the located range.
                    std::vector<std::uint8_t> stored(static_cast<std::size_t>(location.block->storedBytes));
                    std::ifstream file(set.ContainerPath(location.container), std::ios::binary);
                    file.seekg(static_cast<std::streamoff>(location.block->offset));
                    file.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
                    if (file.good()) {
                        static_cast<void>(set.DecodeStoredArtifactBlock(location, stored));
                    }
                }
            }
        }
    }
    set.Unmount();
}

// Mode 2: one byte naming the first level of detail, then records: a baked primary block and up
// to eight more blocks, read as the streamer reads them -- a mesh's level layout and its levels
// from the named one down, and a texture's resident tail composed with streamed mips.
void FuzzStreamedPayloads(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaxBlocks = 8U;
    if (bytes.empty()) {
        return;
    }
    const std::uint32_t firstLod = bytes[0] % 8U;
    const std::vector<std::span<const std::uint8_t>> records = Records(bytes.subspan(1U), kMaxBlocks + 1U);
    if (records.empty()) {
        return;
    }
    const std::span<const std::uint8_t> primary = records[0];
    std::vector<std::vector<std::uint8_t>> blocks;
    for (std::size_t index = 1U; index < records.size(); ++index) {
        blocks.emplace_back(records[index].begin(), records[index].end());
    }

    kb::render::bake::BakedMeshLayout layout{};
    static_cast<void>(kb::render::bake::ReadBakedMeshLayout(primary, layout));
    kb::render::RenderMeshAssetData mesh;
    static_cast<void>(kb::render::bake::ReadBakedMeshLods(primary, firstLod, blocks, mesh));

    kb::render::RenderTextureAssetData tail;
    if (kb::render::bake::ReadBakedTexture(primary, tail) && tail.streaming.has_value()) {
        const std::vector<std::span<const std::uint8_t>> levels(records.begin() + 1, records.end());
        kb::render::RenderTextureAssetData composed;
        static_cast<void>(kb::render::bake::ComposeBakedTextureLevels(tail, levels, composed));
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    const std::span<const std::uint8_t> rest = kb::fuzz::Bytes(data + 1, size - 1U);
    switch (data[0] % 3U) {
    case 0U:
        FuzzPack(rest);
        break;
    case 1U:
        FuzzPackSet(rest);
        break;
    default:
        FuzzStreamedPayloads(rest);
        break;
    }
    return 0;
}
