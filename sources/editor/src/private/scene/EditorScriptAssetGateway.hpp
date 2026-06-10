#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAssetBrowserState;

// Persistence for Lua script assets: creates a new .lua file with a starter
// template (discovered + selected in the browser), and reads/writes script
// source on disk. Single responsibility: Lua script asset file IO. Mirrors
// EditorInputAssetGateway for the input assets.
class EditorScriptAssetGateway {
public:
    EditorScriptAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept;

    // Writes a new "NewScript.lua" (uniquified) under the resolved folder, then
    // discovers and selects it. Returns the created file's physical path.
    [[nodiscard]] std::optional<std::filesystem::path> CreateLuaScript(const std::filesystem::path& virtualFolder);

    [[nodiscard]] static std::string ReadSource(const std::filesystem::path& path);
    [[nodiscard]] static bool WriteSource(const std::filesystem::path& path, std::string_view text);

private:
    [[nodiscard]] std::optional<std::filesystem::path> ResolveFolder(const std::filesystem::path& virtualFolder) const;
    void DiscoverAndSelect(const std::filesystem::path& path);

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
};

} // namespace kb::editor
