#pragma once

#include <filesystem>
#include <string>

namespace kb::editor {

class EditorExternalCodeLauncher final {
public:
    EditorExternalCodeLauncher() = delete;

    [[nodiscard]] static bool OpenProjectFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& filePath,
        std::string& error);
};

} // namespace kb::editor
