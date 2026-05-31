#pragma once

#include <filesystem>
#include <vector>

namespace kb::editor {

class EditorProjectAssetIndex {
public:
    EditorProjectAssetIndex() = delete;

    [[nodiscard]] static std::vector<std::filesystem::path> PrefabAssets();
};

} // namespace kb::editor
