#pragma once

#include <filesystem>
#include <string>

namespace kb::editor {

class EditorProjectPaths {
public:
    EditorProjectPaths() = delete;

    static void SetProjectFile(std::filesystem::path projectFile);
    [[nodiscard]] static std::filesystem::path ProjectRoot();
    [[nodiscard]] static std::filesystem::path ProjectFile();
    [[nodiscard]] static std::filesystem::path AssetsRoot();
    [[nodiscard]] static std::filesystem::path ScenesRoot();
    [[nodiscard]] static std::filesystem::path PrefabsRoot();
    [[nodiscard]] static std::filesystem::path DefaultScenePath();
    [[nodiscard]] static std::filesystem::path UniquePrefabPath(std::string name);
    [[nodiscard]] static std::filesystem::path UniquePrefabPathInFolder(const std::filesystem::path& folder, std::string name);
    [[nodiscard]] static std::filesystem::path UniqueScenePath(std::string name);
};

} // namespace kb::editor
