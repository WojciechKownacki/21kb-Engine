#include "scene/EditorScriptAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/ScriptProjectFiles.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

#include "project/EditorProjectPaths.hpp"

#include <string>

namespace kb::editor {

EditorScriptAssetGateway::EditorScriptAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept
    : scene_(scene)
    , browser_(browser) {}

std::optional<std::filesystem::path> EditorScriptAssetGateway::ResolveFolder(const std::filesystem::path& virtualFolder) const {
    if (virtualFolder.empty()) {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> probe = scene_.Assets().Manager().Mounts().Resolve(virtualFolder / "probe");
    return probe.has_value() ? std::optional<std::filesystem::path>{ probe->parent_path() } : std::nullopt;
}

void EditorScriptAssetGateway::DiscoverAndSelect(const std::filesystem::path& path) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(scene_.Assets().Discover());
    if (const std::optional<std::filesystem::path> created = manager.Mounts().ToVirtual(path)) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*created); metadata != nullptr) {
            static_cast<void>(browser_.SelectAsset(metadata->id, manager));
        }
    }
}

std::optional<std::filesystem::path> EditorScriptAssetGateway::CreateLuaScript(const std::filesystem::path& virtualFolder,
    std::string& error) {
    return Create(kb::script::ScriptLanguage::Lua, virtualFolder, error);
}

std::optional<std::filesystem::path> EditorScriptAssetGateway::CreateNativeScript(
    const std::filesystem::path& virtualFolder, std::string& error) {
    return Create(kb::script::ScriptLanguage::Cpp, virtualFolder, error);
}

// The Create menu names the script itself (NewScript, NewScript1...), with the rules and templates
// kb_cli script-new uses.
std::optional<std::filesystem::path> EditorScriptAssetGateway::Create(kb::script::ScriptLanguage language,
    const std::filesystem::path& virtualFolder, std::string& error) {
    const std::optional<std::filesystem::path> folder = ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        error = virtualFolder.generic_string() + " is not a folder of this project.";
        return std::nullopt;
    }
    const std::filesystem::path projectRoot = EditorProjectPaths::ProjectRoot();
    const std::string name = kb::script::ScriptProjectFiles::FreeName(language, projectRoot, *folder);
    kb::script::CreatedScript created = kb::script::ScriptProjectFiles::Create(language, projectRoot, *folder, name);
    if (!created.error.empty()) {
        error = std::move(created.error);
        return std::nullopt;
    }
    DiscoverAndSelect(created.asset);
    return created.asset;
}

std::string EditorScriptAssetGateway::ReadSource(const std::filesystem::path& path) {
    return ScriptSourceFile::Read(path);
}

bool EditorScriptAssetGateway::WriteSource(const std::filesystem::path& path, std::string_view text) {
    return ScriptSourceFile::Write(path, text);
}

} // namespace kb::editor
