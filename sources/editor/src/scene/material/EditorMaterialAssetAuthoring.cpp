#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <optional>

namespace kb::editor {

EditorMaterialAssetAuthoring::EditorMaterialAssetAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept
    : gateway_(scene, browser)
    , console_(console) {}

bool EditorMaterialAssetAuthoring::Create(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Materials", "Could not resolve a physical folder for the new material.");
        return false;
    }

    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(*folder, "NewMaterial");
    kb::render::RenderMaterialAssetData material{};
    if (!gateway_.WriteNewMaterial(path, material)) {
        console_.Error("Materials", "Material asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material asset created: " + path.generic_string());
    return true;
}

} // namespace kb::editor
