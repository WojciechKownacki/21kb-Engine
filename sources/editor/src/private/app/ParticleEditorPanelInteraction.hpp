#pragma once

#include "engine/assets/AssetId.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"

#include <cstdint>

namespace kb::editor {

class EditorSceneContext;

class ParticleEditorPanelInteraction final {
public:
    ParticleEditorPanelInteraction() = delete;
    [[nodiscard]] static bool Execute(
        EditorSceneContext& sceneContext,
        const ParticleEditorPanelHit& hit,
        kb::assets::AssetId selectedMaterial = {});
    static void UpdateDrag(EditorSceneContext& sceneContext,
                           const ParticleEditorPanelLayout& layout,
                           int y) noexcept;
    [[nodiscard]] static bool CommitDrag(EditorSceneContext& sceneContext);
    [[nodiscard]] static bool HandleCharacter(EditorSceneContext& sceneContext, wchar_t character);
    [[nodiscard]] static bool HandleKeyDown(EditorSceneContext& sceneContext, std::uintptr_t key);
};

} // namespace kb::editor
