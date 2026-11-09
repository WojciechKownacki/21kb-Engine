#include "scene/EditorSceneContext.hpp"
#include "scene/ui/EditorUIThemeAssets.hpp"

#include <memory>

namespace kb::editor {

bool EditorSceneContext::CreateUIThemeAsset(const std::filesystem::path& folder) {
    const auto created = EditorUIThemeAssets::Create(*scene_, assetBrowser_, folder);
    if (created.empty()) return false;
    console_.Info("UI Theme", "Created: " + created.generic_string());
    return true;
}
std::optional<kb::scene::UITheme> EditorSceneContext::ReadUITheme(kb::assets::AssetId id) const {
    return EditorUIThemeAssets::Read(*scene_, id);
}
bool EditorSceneContext::SetUITheme(kb::assets::AssetId id, const kb::scene::UITheme& theme) {
    const auto before = EditorUIThemeAssets::Read(*scene_, id);
    if (!before || !commandStack_.Execute(std::make_unique<EditorUIThemeEditCommand>(
            *scene_, id, "Edit UI Theme", *before, theme))) return false;
    MarkSceneRenderDirty();
    return true;
}

} // namespace kb::editor
