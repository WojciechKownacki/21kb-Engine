#pragma once

#include "engine/assets/bake/AssetPack.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/world/WorldPartitionGrid.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
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

// The cells of a built partitioned world that a chunk takes: every cell scene of the region's
// cells (both corners included) in the listed data layers, and the HLOD proxy mesh of each of
// those cells when the base layer is among them. The world is named by its descriptor's virtual
// path; its built cell index (WorldPaths::CellIndexVirtualPath) says which cell scene and proxy
// belong to which cell. The default region is the whole world, and only a whole-world region
// also takes the persistent (always-loaded) units of its layers.
struct AssetPackWorldRegion {
    std::string world;
    kb::world::WorldCellCoord min{ std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min() };
    kb::world::WorldCellCoord max{ std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max() };
    // Empty for every layer; "" names the base layer.
    std::vector<std::string> dataLayers;

    [[nodiscard]] bool operator==(const AssetPackWorldRegion&) const noexcept = default;
};

// Parses the text form shared by `kb_cli pack split --chunk-cells` and packaging:
//   <world virtual path>[@<minX>:<minZ>..<maxX>:<maxZ>][#<layer>[,<layer>...]]
// e.g. /Game/Worlds/Forest.21kbworld@-2:-2..1:1#(base),night. "(base)" names the base layer, which
// has no name of its own.
[[nodiscard]] bool ParseAssetPackWorldRegion(std::string_view text, AssetPackWorldRegion& out, std::string& error);

// One chunk of a split: every asset whose virtual path starts with one of the prefixes (a data
// layer's folder, say), and every cell scene and proxy of the world regions, moves with its
// artifacts into the chunk pack. Assets those depend on stay where their own rules put them.
struct AssetPackChunkRule {
    std::string label;
    std::vector<std::string> virtualPathPrefixes;
    std::vector<AssetPackWorldRegion> worldRegions;
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
    // How `current` is read: the content key of the release it belongs to decrypts its packs
    // (and unwraps the keys its pack set index carries).
    AssetPackTrust currentTrust{};
};

struct AssetPackPatchReport {
    std::uint64_t changedAssets = 0U;
    std::uint64_t addedAssets = 0U;
    // Assets and auxiliary files `current` has and `next` does not: the patch carries a tombstone
    // for each, so they are gone once it mounts. An asset `next` holds at the same virtual path
    // under another id is replaced by path instead and does not count here.
    std::uint64_t removedAssets = 0U;
    std::uint64_t removedFiles = 0U;
    std::uint64_t changedFiles = 0U;
    bool settingsChanged = false;
    AssetPackToolReport pack;
};

// Writes a patch pack with every asset and auxiliary file of `next` that `current` lacks or
// holds differently, together with their artifacts and the project settings of `next`, and a
// tombstone for every asset and file `next` no longer has. Refuses when nothing changed, when the
// profiles differ, or when the patch level does not go up.
[[nodiscard]] bool BuildAssetPackPatch(
    const AssetPackPatchRequest& request,
    AssetPackPatchReport& report,
    std::string& error);

struct AssetPackSetKeyReport {
    std::uint64_t packs = 0U;
    std::uint64_t encryptedPacks = 0U;
    // Packs that now carry a key line: encrypted under a key other than the release's.
    std::uint64_t wrappedKeys = 0U;
};

// Gives the pack set index at `indexPath` exactly the key lines its packs need under a release
// whose anchor carries `releaseKey` (nullptr for a release without one): a pack sealed without
// encryption, or encrypted under `releaseKey`, gets none; a pack encrypted under any other key
// gets that key -- found among `knownKeys` by the id its seal records -- wrapped under
// `releaseKey`. Fails, leaving the index as it was, when a pack is unsealed or unreadable, when an
// encrypted pack's key is not among `knownKeys`, or when an encrypted pack would need a key line
// and there is no `releaseKey` to wrap it under.
[[nodiscard]] bool RewrapAssetPackSetKeys(
    const std::filesystem::path& indexPath,
    const kb::security::AeadKey* releaseKey,
    std::span<const kb::security::AeadKey> knownKeys,
    AssetPackSetKeyReport& report,
    std::string& error);

} // namespace kb::assets::bake
