#include "project/EditorProjectAssetIndex.hpp"

#include "project/EditorProjectPaths.hpp"

#include <algorithm>

namespace kb::editor {

std::vector<std::filesystem::path> EditorProjectAssetIndex::PrefabAssets() {
    const std::filesystem::path root = EditorProjectPaths::PrefabsRoot();
    std::filesystem::create_directories(root);

    std::vector<std::filesystem::path> assets;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".kbprefab") {
            assets.push_back(entry.path());
        }
    }
    std::sort(assets.begin(), assets.end());
    return assets;
}

} // namespace kb::editor
