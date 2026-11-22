#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace kb::assets {

class AssetManager;

// The scene open in the editor and the project's startup scene (physical paths): no asset change
// deletes, renames or moves either, nor a folder holding one.
struct AssetProtectedScenes {
    std::filesystem::path openScene;
    std::filesystem::path startupScene;
};

// What may be deleted, renamed or moved in a project, the same for the editor's Project Files and for
// kb_cli. Each check returns false with the manager's error set to the reason, in words a person can act on.
class AssetChangeRules final {
public:
    AssetChangeRules() = delete;

    // No asset outside `sameOperationAssets` uses `target`. `operation` is "deleted", "renamed" or "moved";
    // a scene or prefab that links a prefab by its identity does not stop the prefab moving.
    [[nodiscard]] static bool CanChange(AssetManager& manager, AssetId target, std::string_view operation,
        const std::unordered_set<std::uint64_t>& sameOperationAssets = {});
    // The same for every asset inside `virtualFolder`; deleting a folder may break links among its own assets.
    [[nodiscard]] static bool CanChangeFolder(AssetManager& manager, const std::filesystem::path& virtualFolder, std::string_view operation);
    // `physical` (an asset file or a folder) is not, and does not hold, a protected scene. `operation` is
    // "delete", "rename" or "move".
    [[nodiscard]] static bool LeavesProtectedScenes(AssetManager& manager, const AssetProtectedScenes& protectedScenes,
        const std::filesystem::path& physical, std::string_view operation);
    // The assets inside `virtualFolder`, at any depth.
    [[nodiscard]] static std::vector<AssetId> AssetsUnder(const AssetManager& manager, const std::filesystem::path& virtualFolder);
};

} // namespace kb::assets
