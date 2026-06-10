#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::editor {

// Plain on-disk text IO for script sources. Kept free of any scene/asset or
// Win32 dependency so it can be shared by the editor window and the asset
// gateway without crossing module layers.
class ScriptSourceFile {
public:
    ScriptSourceFile() = delete;

    [[nodiscard]] static std::string Read(const std::filesystem::path& path);
    [[nodiscard]] static bool Write(const std::filesystem::path& path, std::string_view text);
};

} // namespace kb::editor
