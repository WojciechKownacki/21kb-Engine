#pragma once

#include "engine/assets/bake/AssetPack.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The PACK SET INDEX: which .kbpack files a game mounts, in which order, and as what.
//
// A game's content need not fit one pack. It is a base pack, any number of chunk packs (content
// split off by world cell or data layer) and any number of patch packs that replace assets of
// the packs before them. The index is a small text file beside the packs, `Game.kbpackset`:
//
//     21kb-pack-set 1
//     base Game.kbpack
//     chunk cell_0_0 Game.cell_0_0.kbpack
//     patch 1 patch-0001 Game.patch-0001.kbpack
//
// One line per pack, in mount order: exactly one base, first; then the chunks; then the patches
// in strictly ascending patch level. Paths are relative to the index, '/'-separated, and name a
// .kbpack inside the index's directory tree. Every pack also states its role, label and patch
// level in its own (sealed) header; the mount refuses an index and a pack that disagree.
//
// In a packaged Windows release the index is a critical file: the signed release manifest lists
// it, the player hashes it at startup, and every pack it names must be bound to the release
// through its seal digest. That is what makes the mount order, the patch levels and the set of
// patches part of what the release key signs -- and, with anti-rollback, part of what cannot be
// rolled back.
namespace kb::assets::bake {

inline constexpr std::string_view kAssetPackSetFileName = "Game.kbpackset";
inline constexpr std::string_view kAssetPackSetFileExtension = ".kbpackset";
inline constexpr std::uintmax_t kMaxAssetPackSetFileBytes = 4U * 1024U * 1024U;
// More packs than any world partition needs, and few enough that a hostile index cannot make a
// reader open files without bound.
inline constexpr std::size_t kMaxAssetPackSetPacks = 65536U;

struct AssetPackSetEntry {
    AssetPackRole role = AssetPackRole::Base;
    // Empty for the base (an index does not name the base's label); the chunk's or patch's
    // label otherwise (a bake-cache name).
    std::string label;
    // >= 1 for a patch, 0 otherwise.
    std::uint32_t patchLevel = 0U;
    // Relative to the index file's directory, '/'-separated.
    std::string path;

    [[nodiscard]] bool operator==(const AssetPackSetEntry&) const noexcept = default;
};

struct AssetPackSetIndex {
    std::vector<AssetPackSetEntry> packs;

    [[nodiscard]] bool operator==(const AssetPackSetIndex&) const noexcept = default;
};

enum class AssetPackSetStatus : std::uint8_t {
    Success,
    // The index file is missing or could not be read.
    Unreadable,
    // Not an index, a line that does not parse, a path that is not a relative .kbpack path.
    Malformed,
    // Longer than kMaxAssetPackSetFileBytes or names more than kMaxAssetPackSetPacks packs.
    TooLarge,
    // No base, a base that is not first, a chunk after a patch, patch levels that do not
    // strictly ascend.
    OrderInvalid,
    // Two packs with the same path or the same label.
    Duplicate,
};

[[nodiscard]] std::string_view ToString(AssetPackSetStatus status) noexcept;

// True for a relative, '/'-separated path of a .kbpack without empty, "." or ".." components,
// backslashes or control characters.
[[nodiscard]] bool IsValidAssetPackSetPath(std::string_view path) noexcept;

// Checks the order and uniqueness rules above.
[[nodiscard]] AssetPackSetStatus ValidateAssetPackSetIndex(const AssetPackSetIndex& index);

// The canonical text. Fails (returns empty) for an index ValidateAssetPackSetIndex refuses.
[[nodiscard]] std::string EncodeAssetPackSetIndex(const AssetPackSetIndex& index);

// Strict parse of the text, then ValidateAssetPackSetIndex. `out` changes only on Success.
[[nodiscard]] AssetPackSetStatus ParseAssetPackSetIndex(std::string_view text, AssetPackSetIndex& out);

// Reads and parses the file.
[[nodiscard]] AssetPackSetStatus ReadAssetPackSetIndex(const std::filesystem::path& path, AssetPackSetIndex& out);

// Where a pack named by an index entry lies, given the index file's path.
[[nodiscard]] std::filesystem::path ResolveAssetPackSetPath(
    const std::filesystem::path& indexPath,
    std::string_view entryPath);

} // namespace kb::assets::bake
