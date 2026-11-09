#include "scene/EditorSceneContext.hpp"
#include "scene/GameplayGraphEditorState.hpp"
#include "engine/scene/SceneAssets.hpp"

namespace kb::editor {
bool EditorSceneContext::OpenGameplayGraphAsset(kb::assets::AssetId id) {
    if (!gameplayGraphEditor_) gameplayGraphEditor_ = std::make_unique<GameplayGraphEditorState>();
    if (!gameplayGraphEditor_->Load(*scene_, id)) { console_.Error("Gameplay Graph", gameplayGraphEditor_->error); return false; }
    return true;
}
bool EditorSceneContext::CreateGameplayGraphAsset(const std::filesystem::path& virtualFolder) {
    auto& manager = scene_->Assets().Manager();
    const auto folder = kb::assets::NormalizeAssetPath(virtualFolder);
    if (folder != "/Game" && !folder.starts_with("/Game/")) return false;
    if (gameplayGraphEditor_ && gameplayGraphEditor_->document.Dirty()) {
        console_.Error("Gameplay Graph", "Save the current graph before creating another graph"); return false;
    }
    const auto probe = manager.Mounts().Resolve(virtualFolder / "probe"); if (!probe) return false;
    auto file = probe->parent_path() / "NewGameplayGraph.kbgraph";
    for (std::size_t suffix = 1; std::filesystem::exists(file); ++suffix)
        file = probe->parent_path() / ("NewGameplayGraph" + std::to_string(suffix) + ".kbgraph");
    std::error_code directoryError; std::filesystem::create_directories(file.parent_path(), directoryError);
    kb::visual::VisualGraphAuthoringDocument document; document.Create(file.stem().string()); std::string error;
    if (directoryError || !document.Save(file, error)) { console_.Error("Gameplay Graph", error.empty() ? directoryError.message() : error); return false; }
    static_cast<void>(scene_->Assets().Discover()); const auto* metadata = manager.Registry().FindByPath(virtualFolder / file.filename());
    if (metadata && OpenGameplayGraphAsset(metadata->id) && assetBrowser_.SelectAsset(metadata->id, manager)) return true;
    std::error_code ignored; std::filesystem::remove(file, ignored); static_cast<void>(scene_->Assets().Discover()); return false;
}
bool EditorSceneContext::ConsumeGameplayGraphOpenRequest() noexcept {
    if (!gameplayGraphEditor_ || !gameplayGraphEditor_->openRequested) return false;
    gameplayGraphEditor_->openRequested = false; return true;
}
void EditorSceneContext::ClearGameplayGraphPreview() noexcept { if (gameplayGraphEditor_) gameplayGraphEditor_->StopPreview(); }
} // namespace kb::editor
