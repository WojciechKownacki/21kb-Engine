#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::script {
enum class ScriptLanguage;
}

namespace kb::editor {

class EditorAssetBrowserState;

// Creates script assets and their source files, then discovers and selects
// the behaviour asset in the browser.
class EditorScriptAssetGateway {
public:
    EditorScriptAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept;

    // Writes a new script named NewScript (NewScript1... when taken) under the resolved folder, then
    // discovers and selects it. Returns the asset's physical path (the .lua file, or the C++ script's
    // .native descriptor). Nothing, with `error` filled, when the script cannot be made.
    [[nodiscard]] std::optional<std::filesystem::path> CreateLuaScript(const std::filesystem::path& virtualFolder,
        std::string& error);
    [[nodiscard]] std::optional<std::filesystem::path> CreateNativeScript(const std::filesystem::path& virtualFolder, std::string& error);

    [[nodiscard]] static std::string ReadSource(const std::filesystem::path& path);
    [[nodiscard]] static bool WriteSource(const std::filesystem::path& path, std::string_view text);

private:
    [[nodiscard]] std::optional<std::filesystem::path> Create(kb::script::ScriptLanguage language,
        const std::filesystem::path& virtualFolder, std::string& error);
    [[nodiscard]] std::optional<std::filesystem::path> ResolveFolder(const std::filesystem::path& virtualFolder) const;
    void DiscoverAndSelect(const std::filesystem::path& path);

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
};

} // namespace kb::editor
