#include "app/EditorAssetBrowserDoubleClickHandler.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "app/EditorParticleDocumentLifecycle.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "assets/EditorAssetOpenPolicy.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::filesystem::path ResolveAssetPath(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetManager& manager) {
    if (const std::optional<std::filesystem::path> mounted = manager.Mounts().Resolve(metadata.virtualPath)) {
        return *mounted;
    }
    return metadata.physicalPath;
}

} // namespace

EditorAssetBrowserDoubleClickResult EditorAssetBrowserDoubleClickHandler::HandleDoubleClick(
    HWND owner,
    const RECT& content,
    int x,
    int y,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::ContentFolder:
    case EditorAssetBrowserHitKind::Folder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        return folder.has_value() && state.SelectFolder(*folder, manager)
            ? EditorAssetBrowserDoubleClickResult::BrowserNavigation
            : EditorAssetBrowserDoubleClickResult::None;
    }
    case EditorAssetBrowserHitKind::Asset: {
        const std::optional<kb::assets::AssetMetadata> metadata = EditorAssetBrowserHitPayloadResolver::AssetMetadataAt(hit, state, manager);
        return metadata.has_value() ? OpenAsset(owner, *metadata, sceneContext)
                                    : EditorAssetBrowserDoubleClickResult::None;
    }
    default:
        return EditorAssetBrowserDoubleClickResult::None;
    }
}

// The single open route. Double-click and the Project Files Open command must not
// drift apart, so both resolve the target asset and then land here.
EditorAssetBrowserDoubleClickResult EditorAssetBrowserDoubleClickHandler::OpenAsset(
    HWND owner,
    const kb::assets::AssetMetadata& metadataValue,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const kb::assets::AssetMetadata* const metadata = &metadataValue;
    if (metadata->type == "LuaScript") {
        return sceneContext.OpenLuaScript(metadata->id)
            ? EditorAssetBrowserDoubleClickResult::ScriptEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
        // The Project Files glyph and its activation must classify the two
        // skeletal document kinds identically.
        if (ProjectFilesAssetIconResolver::IsSkeletalMesh(*metadata)) {
            return sceneContext.RequestOpenSkeletalMeshEditorAsset(metadata->id)
                ? EditorAssetBrowserDoubleClickResult::SkeletalMeshEditorOpened
                : EditorAssetBrowserDoubleClickResult::None;
        }
        if (ProjectFilesAssetIconResolver::IsSkeleton(*metadata)) {
            return sceneContext.RequestOpenSkeletalMeshEditorSkeletonAsset(metadata->id)
                ? EditorAssetBrowserDoubleClickResult::SkeletalMeshEditorOpened
                : EditorAssetBrowserDoubleClickResult::None;
        }
        if (metadata->type == kb::scene::kAnimationClipAssetType) {
        return sceneContext.OpenAnimationClipEditorAsset(metadata->id)
            ? EditorAssetBrowserDoubleClickResult::AnimationClipEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
    if (metadata->type == kb::scene::kAnimatorControllerAssetType) {
            return sceneContext.OpenAnimatorEditorAsset(metadata->id)
            ? EditorAssetBrowserDoubleClickResult::AnimatorEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
        if (metadata->type == kb::scene::kParticleEffectAssetType) {
            if (!EditorParticleDocumentLifecycle::Resolve(
                    owner, sceneContext,
                    kb::particle_editor::ParticleDocumentTransition::Open,
                    L"opening another particle effect")) {
                return EditorAssetBrowserDoubleClickResult::None;
            }
            return sceneContext.OpenParticleEditorAsset(metadata->id)
                ? EditorAssetBrowserDoubleClickResult::ParticleEditorOpened
                : EditorAssetBrowserDoubleClickResult::None;
        }
        if (metadata->type == kb::scene::kTimelineAssetType) {
        return sceneContext.OpenAnimationAsset(metadata->id)
            ? EditorAssetBrowserDoubleClickResult::ScriptEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
    if (metadata->type == "RenderMaterial" || metadata->type == "RenderMaterialInstance" || metadata->type == kb::render::kRenderMaterialGraphAssetType) {
        if (!sceneContext.OpenMaterialEditorAsset(metadata->id)) {
            return EditorAssetBrowserDoubleClickResult::None;
        }
        return HandleMaterialAssetDoubleClick(*metadata, state, manager);
    }
    if (!EditorAssetOpenPolicy::IsSceneDocument(*metadata)) {
        return EditorAssetBrowserDoubleClickResult::None;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, L"opening another scene");
    if (!resolution.has_value()) {
        return EditorAssetBrowserDoubleClickResult::None;
    }
    return sceneContext.OpenScene(ResolveAssetPath(*metadata, manager), *resolution)
        ? EditorAssetBrowserDoubleClickResult::SceneOpened
        : EditorAssetBrowserDoubleClickResult::None;
}

} // namespace kb::editor

#endif
