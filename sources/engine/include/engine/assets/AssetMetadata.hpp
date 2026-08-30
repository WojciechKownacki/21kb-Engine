#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::assets {

enum class AssetLoadState {
    Unloaded,
    Loaded,
    Failed,
};

struct AssetMetadata {
    AssetId id{};
    std::string type;
    // A closed, trusted vocabulary read from AssetImportCategory (see ToString/RuntimeAssetType in
    // AssetKind.hpp) plus the reserved "EditorLiveOverride" sentinel — callers match this by exact
    // equality to classify or gate behavior (e.g. mesh/texture drag targets, icon resolution). Never
    // set this from loader- or author-supplied free text; use browseTag for that instead.
    std::string importCategory;
    // Free-text, loader-supplied tag folded into Asset Browser search only (see
    // EditorAssetBrowserAssetRows::SearchText). Unlike importCategory this is not a closed
    // vocabulary and callers must not match it by equality for classification.
    std::string browseTag;
    std::string name;
    std::filesystem::path virtualPath;
    std::filesystem::path physicalPath;
    // Original extension retained by a runtime package whose assets intentionally have no
    // physical path. Loose discovery fills it too, so loaders use one dispatch field in both
    // modes instead of inventing pseudo-paths for packaged data.
    std::string sourceExtension;
    std::uint64_t contentHash = 0;
    std::vector<AssetId> dependencies;
    bool runtimeLoadable = true;
};

[[nodiscard]] std::string NormalizeAssetPath(const std::filesystem::path& path);

} // namespace kb::assets
