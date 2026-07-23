#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"
#include "scene/material_preview/EditorMaterialPreviewSettings.hpp"
#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace kb::editor {

class EditorMaterialPreviewScene {
public:
    ~EditorMaterialPreviewScene();

    // previewInputRevision is a cheap monotonic "did anything that feeds the preview change?" key supplied by
    // the caller (material working-copy DocumentRevision combined with the asset registry Generation, and the
    // selected node when node-preview is on). When it is unchanged from the previous call for the same
    // material, SceneFor returns the cached scene WITHOUT recomputing MaterialPreviewContentHash - which does a
    // full deep-copy + serialization of the whole material and, at the 180 Hz the editor paces at while a
    // material is open, otherwise saturated a core for nothing. Pass 0 (the default) to opt out of the gate and
    // recompute every call, which is what the unit tests that drive SceneFor directly do.
    [[nodiscard]] const kb::scene::Scene& SceneFor(
        const kb::scene::Scene& sourceScene,
        kb::assets::AssetId materialAssetId,
        const kb::render::RenderMaterialAssetData* workingCopy = nullptr,
        std::uint64_t previewInputRevision = 0U);
    // The thumbnail pipeline needs to talk to the scene's async screen-capture channel, which is a
    // mutating call; SceneFor stays const so nothing else can accidentally edit the shared preview scene.
    [[nodiscard]] kb::scene::Scene* MutableScene() noexcept;
    [[nodiscard]] const EditorMaterialPreviewPrimitivePolicy& PrimitivePolicy() const noexcept;
    bool SetPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy policy) noexcept;
    [[nodiscard]] const EditorMaterialPreviewSceneSettings& SceneSettings() const noexcept;
    bool SetSceneSettings(EditorMaterialPreviewSceneSettings settings) noexcept;
    // Camera-only update (orbit + dolly) that does not rebuild the scene or re-cook the shader. Returns true
    // when the view actually changed.
    bool SetCameraOrbit(float yawDegrees, float pitchDegrees, float cameraDistance) noexcept;
    [[nodiscard]] const EditorMaterialPreviewTelemetry& Telemetry() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;

private:
    void Rebuild(
        const kb::scene::Scene& sourceScene,
        kb::assets::AssetId materialAssetId,
        const kb::render::RenderMaterialAssetData* workingCopy,
        std::uint64_t contentHash);

    std::unique_ptr<kb::scene::Scene> scene_;
    // The scene's single mesh entity, remembered so a content-only Rebuild (the SAME material, just
    // edited) can rebind its material in place instead of recreating the whole Scene - see Rebuild()'s
    // comment for why that matters (Scene::Id()-keyed runtime resource caches, texture decode cost).
    kb::scene::SceneEntity previewMeshEntity_{};
    EditorMaterialPreviewTelemetry telemetry_{};
    kb::assets::AssetId materialAssetId_{};
    std::filesystem::path workingCopyPath_;
    EditorMaterialPreviewPrimitivePolicy primitivePolicy_ = EditorMaterialPreviewPrimitivePolicy::Sphere();
    EditorMaterialPreviewSceneSettings sceneSettings_{};
    std::uint64_t materialContentHash_ = 0U;
    std::uint64_t revision_ = 1U;
    // Gate state for SceneFor: the last previewInputRevision we actually computed a content hash for, and
    // whether that cached value is usable (a 0/"untracked" revision never is). See SceneFor.
    std::uint64_t lastPreviewInputRevision_ = 0U;
    bool previewInputCacheValid_ = false;
};

} // namespace kb::editor
