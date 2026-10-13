#pragma once

#include "commands/IEditorCommand.hpp"
#include "engine/assets/AssetId.hpp"
#include "kb/render/post/ScenePostProcessSettings.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAssetBrowserState;

// Post-process profile assets (.kbppfx) in the editor: Create > Post-Process Profile, reading one
// for the Inspector and writing an edit so the asset manager (and with it the renderer) loads the
// new values at once.
class EditorPostProcessProfileAssets final {
public:
    EditorPostProcessProfileAssets() = delete;

    // A new profile with the renderer's defaults in `virtualFolder`, selected in Project Files;
    // its virtual path, or empty when it could not be written.
    [[nodiscard]] static std::filesystem::path Create(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& browser,
        const std::filesystem::path& virtualFolder);
    [[nodiscard]] static bool IsProfile(const kb::scene::Scene& scene, kb::assets::AssetId id);
    [[nodiscard]] static std::optional<kb::render::ScenePostProcessSettings> Read(
        const kb::scene::Scene& scene,
        kb::assets::AssetId id);
    // Saves `settings` into the profile file and reloads the asset; false (file unchanged) when
    // the settings could not be written or do not load back as written.
    [[nodiscard]] static bool Write(
        kb::scene::Scene& scene,
        kb::assets::AssetId id,
        const kb::render::ScenePostProcessSettings& settings);
};

// One undoable edit of a profile asset (Edit > Undo writes the previous values back).
class EditorPostProcessProfileEditCommand final : public IEditorCommand {
public:
    EditorPostProcessProfileEditCommand(
        kb::scene::Scene& scene,
        kb::assets::AssetId profile,
        std::string label,
        kb::render::ScenePostProcessSettings before,
        kb::render::ScenePostProcessSettings after);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool AffectsSceneDocument() const noexcept override;
    [[nodiscard]] bool AffectsHierarchySelection() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    kb::scene::Scene& scene_;
    kb::assets::AssetId profile_{};
    std::string label_;
    kb::render::ScenePostProcessSettings before_;
    kb::render::ScenePostProcessSettings after_;
};

} // namespace kb::editor
