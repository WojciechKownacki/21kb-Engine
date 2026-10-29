#include "app/EditorAssetBrowserDoubleClickHandler.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "app/EditorExternalCodeLauncher.hpp"
#include "app/EditorParticleDocumentLifecycle.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "assets/EditorAssetOpenPolicy.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/scene/BlendSpace2D.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "scene/EditorSceneContext.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <optional>
#include <string>

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

bool EditorAssetBrowserDoubleClickHandler::PreviewLocalization(EditorSceneContext& context,
    kb::assets::AssetId catalogId, std::string_view language) {
    if (context.HasPlayModeSceneSession()) return false;
    const auto catalog = context.Scene().Assets().Manager().Load<kb::localization::LocalizationCatalog>(catalogId);
    if (!catalog.IsLoaded() || !catalog->languages.contains(language)) return false;
    if (!context.Scene().Localization().SetCatalog(catalogId.value) ||
        !context.Scene().Localization().SetLanguage(language)) return false;
    auto& preview = context.ViewportPreview(1U);
    if (!preview.Is2D()) preview.Toggle2D();
    context.MarkSceneRenderDirty();
    context.Console().Info("Localization", "Preview language: " + std::string{language});
    return true;
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
    if (metadata->type == kb::localization::kLocalizationCatalogAssetType) {
        if (sceneContext.HasPlayModeSceneSession()) return EditorAssetBrowserDoubleClickResult::None;
        const auto catalog = manager.Load<kb::localization::LocalizationCatalog>(metadata->id);
        if (!catalog.IsLoaded()) {
            sceneContext.Console().Error("Localization", manager.LastError());
            return EditorAssetBrowserDoubleClickResult::None;
        }
        const HMENU menu = CreatePopupMenu();
        if (!menu) return EditorAssetBrowserDoubleClickResult::None;
        std::vector<std::string> languages;
        for (const auto& [language, messages] : catalog->languages) {
            static_cast<void>(messages);
            languages.push_back(language);
            const std::wstring label{language.begin(), language.end()};
            const bool selected = sceneContext.Scene().Localization().Catalog() == metadata->id.value &&
                sceneContext.Scene().Localization().Language() == language;
            AppendMenuW(menu, MF_STRING | (selected ? MF_CHECKED : MF_UNCHECKED),
                static_cast<UINT_PTR>(languages.size()), label.c_str());
        }
        POINT cursor{};
        GetCursorPos(&cursor);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            cursor.x, cursor.y, 0, owner, nullptr);
        DestroyMenu(menu);
        if (command == 0U || command > languages.size()) return EditorAssetBrowserDoubleClickResult::None;
        return PreviewLocalization(sceneContext, metadata->id, languages[command - 1U])
            ? EditorAssetBrowserDoubleClickResult::LocalizationPreviewChanged : EditorAssetBrowserDoubleClickResult::None;
    }
    if (metadata->type == "LuaScript") {
        std::string error;
        if (!EditorExternalCodeLauncher::OpenProjectFile(
                EditorProjectPaths::ProjectRoot(), ResolveAssetPath(*metadata, manager), error)) {
            sceneContext.Console().Error("Scripts", error);
            return EditorAssetBrowserDoubleClickResult::None;
        }
        return sceneContext.OpenLuaScript(metadata->id)
            ? EditorAssetBrowserDoubleClickResult::ExternalScriptOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
    if (metadata->type == kb::script::ScriptAssetTypes::NativeBehaviour) {
        const std::filesystem::path descriptorPath = ResolveAssetPath(*metadata, manager);
        const auto descriptor = manager.Load<kb::script::NativeBehaviourDescriptor>(metadata->id);
        if (!descriptor.IsLoaded()) {
            sceneContext.Console().Error("Scripts", manager.LastError());
            return EditorAssetBrowserDoubleClickResult::None;
        }
        const std::filesystem::path sourcePath = descriptor->sourcePath.empty()
            ? descriptorPath
            : descriptor->sourcePath.is_absolute() ? descriptor->sourcePath : descriptorPath.parent_path() / descriptor->sourcePath;
        std::string error;
        if (!EditorExternalCodeLauncher::OpenProjectFile(EditorProjectPaths::ProjectRoot(), sourcePath.lexically_normal(), error)) {
            sceneContext.Console().Error("Scripts", error);
            return EditorAssetBrowserDoubleClickResult::None;
        }
        return EditorAssetBrowserDoubleClickResult::ExternalScriptOpened;
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
                    kb::particle_editor::ParticleDocumentTransition::Open)) {
                return EditorAssetBrowserDoubleClickResult::None;
            }
            return sceneContext.OpenParticleEditorAsset(metadata->id)
                ? EditorAssetBrowserDoubleClickResult::ParticleEditorOpened
                : EditorAssetBrowserDoubleClickResult::None;
        }
        if (metadata->type == kb::scene::kTimelineAssetType) {
        return sceneContext.OpenAnimationAsset(metadata->id)
            ? EditorAssetBrowserDoubleClickResult::TimelineEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
        if (metadata->type == kb::scene::kBlendSpace2DAssetType) {
            return sceneContext.OpenBlendSpace2DEditorAsset(metadata->id)
                ? EditorAssetBrowserDoubleClickResult::BlendSpace2DEditorOpened : EditorAssetBrowserDoubleClickResult::None;
        }
    if (metadata->type == "ScenePrefab") {
        return sceneContext.OpenPrefabEditMode(ResolveAssetPath(*metadata, manager))
            ? EditorAssetBrowserDoubleClickResult::PrefabEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
    if (metadata->type == "RenderMaterial" || metadata->type == "RenderMaterialInstance" || metadata->type == kb::render::kRenderMaterialGraphAssetType) {
        if (!sceneContext.OpenMaterialEditorAsset(metadata->id)) {
            return EditorAssetBrowserDoubleClickResult::None;
        }
        return HandleMaterialAssetDoubleClick(*metadata, state, manager);
    }
    if (!EditorAssetOpenPolicy::IsSceneDocument(*metadata) && !EditorAssetOpenPolicy::IsWorldDocument(*metadata)) {
        return EditorAssetBrowserDoubleClickResult::None;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, EditorSceneTransition::OpenScene);
    if (!resolution.has_value()) {
        return EditorAssetBrowserDoubleClickResult::None;
    }
    return sceneContext.OpenScene(ResolveAssetPath(*metadata, manager), *resolution)
        ? EditorAssetBrowserDoubleClickResult::SceneOpened
        : EditorAssetBrowserDoubleClickResult::None;
}

} // namespace kb::editor

#endif
