#pragma once

#include "engine/assets/bake/AssetPack.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Offline operations on cooked packs, for packaging: recompress a pack, split a game's content
// into a base pack and chunk packs, and cut a patch pack out of a new cook. Every artifact that
// is copied is verified on the way out of its source pack; every result is an unsealed pack that
// packaging seals with the release key like any cooked pack.
namespace kb::assets::bake {

class AssetPackReader;

struct AssetPackToolReport {
    std::uint64_t artifacts = 0U;
    std::uint64_t blocks = 0U;
    std::uint64_t compressedBlocks = 0U;
    // What the blocks hold, and what they occupy in the file.
    std::uint64_t payloadBytes = 0U;
    std::uint64_t storedBytes = 0U;
    std::uint64_t fileBytes = 0U;
};

[[nodiscard]] AssetPackToolReport DescribeAssetPack(const AssetPackReader& reader);

// Rewrites every artifact of `input` into `output` as `options` says (compression, role, label,
// patch level, base identity). `input` may be sealed; `output` is not.
[[nodiscard]] bool RepackAssetPack(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const AssetPackWriterOptions& options,
    AssetPackToolReport& report,
    std::string& error);

// One chunk of a split: every asset whose virtual path starts with one of the prefixes (a world
// cell's or a data layer's folder, say) moves, with its artifacts, into the chunk pack.
struct AssetPackChunkRule {
    std::string label;
    std::vector<std::string> virtualPathPrefixes;
    std::filesystem::path output;
};

struct AssetPackSplitReport {
    AssetPackToolReport base;
    std::vector<AssetPackToolReport> chunks;
    std::vector<std::uint64_t> chunkAssets;
};

// Splits a complete runtime pack into a base pack and one chunk pack per rule. An asset goes to
// the first rule that matches it; everything else, the project's auxiliary files and the default
// map stay in the base (a rule matching the default map is an error, as is a rule matching
// nothing). Chunks name the base's catalogue identity.
[[nodiscard]] bool SplitRuntimeAssetPack(
    const std::filesystem::path& input,
    const std::filesystem::path& baseOutput,
    std::span<const AssetPackChunkRule> rules,
    AssetPackBlockCompression compression,
    int compressionLevel,
    AssetPackSplitReport& report,
    std::string& error);

struct AssetPackPatchRequest {
    // What players have: a pack set index (.kbpackset) or a single base pack.
    std::filesystem::path current;
    // A complete cook of the new content.
    std::filesystem::path next;
    std::filesystem::path output;
    std::string label;
    // Must be higher than every patch level in `current`.
    std::uint32_t patchLevel = 0U;
    AssetPackBlockCompression compression = AssetPackBlockCompression::Zstd;
    int compressionLevel = 9;
};

struct AssetPackPatchReport {
    std::uint64_t changedAssets = 0U;
    std::uint64_t addedAssets = 0U;
    // Assets `current` has and `next` does not. A patch cannot take content away, so they stay;
    // packaging reports them.
    std::uint64_t removedAssets = 0U;
    std::uint64_t changedFiles = 0U;
    bool settingsChanged = false;
    AssetPackToolReport pack;
};

// Writes a patch pack with every asset and auxiliary file of `next` that `current` lacks or
// holds differently, together with their artifacts and the project settings of `next`. Refuses
// when nothing changed, when the profiles differ, or when the patch level does not go up.
[[nodiscard]] bool BuildAssetPackPatch(
    const AssetPackPatchRequest& request,
    AssetPackPatchReport& report,
    std::string& error);

} // namespace kb::assets::bake
