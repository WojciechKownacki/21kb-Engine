#pragma once

#include <filesystem>
#include <string>

namespace kb::editor {

class EditorProjectPaths {
public:
    EditorProjectPaths() = delete;

    [[nodiscard]] static std::filesystem::path ProjectRoot();
    [[nodiscard]] static std::filesystem::path AssetsRoot();
    [[nodiscard]] static std::filesystem::path PrefabsRoot();
    [[nodiscard]] static std::filesystem::path UniquePrefabPath(std::string name);
    [[nodiscard]] static std::filesystem::path UniquePrefabPathInFolder(const std::filesystem::path& folder, std::string name);
};

} // namespace kb::editor
