#include "scene/EditorScriptAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

namespace kb::editor {
namespace {

constexpr std::string_view kLuaExtension = ".lua";

// Starter template so a freshly created script already ticks in play mode and
// proves scripting works by printing to the editor Console once on the first tick.
constexpr std::string_view kLuaTemplate =
    "-- Lua behaviour script. Runs every frame in play mode.\n"
    "-- 'self' is the entity this script is attached to; 'dt' is the frame delta.\n"
    "-- Log(\"text\") prints to the editor Console.\n"
    "\n"
    "local started = false\n"
    "\n"
    "function Tick(self, dt)\n"
    "    if not started then\n"
    "        started = true\n"
    "        Log(\"Hello from Lua! Scripting works.\")\n"
    "    end\n"
    "end\n";

[[nodiscard]] std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName) {
    std::filesystem::path candidate = folder / (std::string{ baseName } + std::string{ kLuaExtension });
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = folder / (std::string{ baseName } + std::to_string(suffix) + std::string{ kLuaExtension });
        ++suffix;
    }
    return candidate;
}

} // namespace

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

std::optional<std::filesystem::path> EditorScriptAssetGateway::CreateLuaScript(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        return std::nullopt;
    }
    const std::filesystem::path path = UniqueFilePath(*folder, "NewScript");
    if (!WriteSource(path, kLuaTemplate)) {
        return std::nullopt;
    }
    DiscoverAndSelect(path);
    return path;
}

std::string EditorScriptAssetGateway::ReadSource(const std::filesystem::path& path) {
    return ScriptSourceFile::Read(path);
}

bool EditorScriptAssetGateway::WriteSource(const std::filesystem::path& path, std::string_view text) {
    return ScriptSourceFile::Write(path, text);
}

} // namespace kb::editor
