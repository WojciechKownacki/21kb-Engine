#include "scene/EditorSceneContext.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/LensEchoComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "rendering/EditorMeshPreviewRasterizer.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "scene/audio/EditorSceneAudioSettingsService.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/UIAssetIO.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/library/EngineLibraryModule.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include "project/EditorProjectPaths.hpp"
#include "packaging/EditorProjectPackageService.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptModule.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialSemanticHash.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include "scene/EditorScriptAssetGateway.hpp"
#include "scene/input/EditorInputActionAuthoring.hpp"
#include "scene/input/EditorInputAssetGateway.hpp"
#include "scene/input/EditorInputMappingContextAuthoring.hpp"
#include "scene/audio/EditorAudioMixerAuthoring.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include "scene/EditorAssetErrorMessage.hpp"
#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorPluginCatalog.hpp"
#include "scene/EditorSceneAssetBrowserCommands.hpp"
#include "scene/EditorSceneCommandController.hpp"
#include "scene/EditorSceneDocumentAssetLoaders.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/EditorTerrainService.hpp"
#include "scene/EditorSceneObjectEditCommands.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialGraphDebugLog.hpp"
#include "scene/material/EditorMaterialGraphSemanticAnalysis.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/EditorMaterialReferenceFinder.hpp"
#include "scene/material/EditorMaterialTextureSlotValidation.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"
#include "scene/material_preview/EditorMaterialNodePreviewBuilder.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "scene/EditorAnimationPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/transform_edit/EditorSceneTransformCommitBuilder.hpp"
#include "scene/transform_edit/EditorSceneTransformEditApplier.hpp"
#include "scene/transform_edit/EditorSceneTransformEditController.hpp"
#include "scene/transform_edit/EditorSceneTransformSnapshotBuilder.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"
#include "diagnostics/EditorLagTrace.hpp"

#include <bit>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::editor {

bool EditorSceneContext::RequestOpenSkeletalMeshEditorAsset(kb::assets::AssetId id) {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().Find(id);
    if (meshMetadata == nullptr || meshMetadata->type != kb::scene::kSkeletalMeshAssetType) {
        console_.Error("Skeletal Mesh Editor", "Selected asset is not a Skeletal Mesh.");
        return false;
    }
    if (skeletalMeshEditorDocument_.Dirty() && skeletalMeshEditorDocument_.AssetId() != id) {
        return PrepareSkeletalMeshEditorClose("opening another Skeletal Mesh");
    }
    if (!HasPendingSkeletalMeshEditorOpen() && skeletalMeshEditorAssetId_ == id && animationPreviewScene_ != nullptr &&
        animationPreviewScene_->CurrentScene() != nullptr) {
        return SwitchSkeletalMeshEditorDocument(false);
    }
    pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
    if (pendingSkeletalMeshEditorAssetId_ == id) {
        return true;
    }

    pendingSkeletalMeshEditorAssetId_ = id;
    pendingSkeletalMeshEditorSkeletonId_ = {};
    pendingSkeletalMeshEditorOpenEventId_ = diagnostics::EditorLagTrace::NextEventId();
    if (!manager.LoadAsync<kb::scene::SkeletalMeshAsset>(id)) {
        pendingSkeletalMeshEditorAssetId_ = {};
        pendingSkeletalMeshEditorOpenEventId_ = 0U;
        console_.Error("Skeletal Mesh Editor", AssetErrorOr(manager, "Skeletal Mesh loading could not be started."));
        return false;
    }
    console_.Info("Skeletal Mesh Editor", "Loading document: " + meshMetadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::RequestOpenSkeletalMeshEditorSkeletonAsset(kb::assets::AssetId skeletonId) {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* skeletonMetadata = manager.Registry().Find(skeletonId);
    if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType) {
        console_.Error("Skeleton", "Selected asset is not a Skeleton.");
        return false;
    }
    if ((skeletalMeshEditorDocument_.Dirty() || skeletonEditorDocument_.Dirty()) &&
        animationPreview_.SkeletonAsset() != skeletonId) {
        return PrepareSkeletalMeshEditorClose("opening another Skeleton");
    }
    if (!HasPendingSkeletalMeshEditorOpen() && animationPreview_.SkeletonAsset() == skeletonId && animationPreviewScene_ != nullptr &&
        animationPreviewScene_->CurrentScene() != nullptr) {
        return SwitchSkeletalMeshEditorDocument(true);
    }

    const auto requestWithPreviewMesh = [this, skeletonId](kb::assets::AssetId meshId) {
        if (skeletalMeshEditorAssetId_ == meshId && animationPreviewScene_ != nullptr) {
            return FinalizeLoadedSkeletalMeshEditorAsset(
                meshId, skeletonId, diagnostics::EditorLagTrace::NextEventId(), skeletonId);
        }
        if (!RequestOpenSkeletalMeshEditorAsset(meshId)) return false;
        pendingSkeletalMeshEditorPrimarySkeletonId_ = skeletonId;
        return true;
    };

    // A Skeleton owns the shared rig, while geometry remains an optional compatible preview.
    // LoadBinding reads only the two-line mesh header, so choosing the preview never decodes
    // vertices on the UI thread. The active document remains the Skeleton asset.
    kb::assets::AssetId fallbackMeshId{};
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (metadata.type != kb::scene::kSkeletalMeshAssetType) {
            continue;
        }
        std::filesystem::path path = metadata.physicalPath;
        if (const std::optional<std::filesystem::path> mounted = manager.Mounts().Resolve(metadata.virtualPath)) {
            path = *mounted;
        }
        const std::optional<kb::scene::SkeletalMeshAssetBinding> binding =
            kb::scene::SkeletalMeshAssetIO::LoadBinding(path);
        if (binding.has_value() && binding->skeletonAssetId == skeletonId.value) {
            const bool matchingName = metadata.name == skeletonMetadata->name ||
                metadata.virtualPath.stem() == skeletonMetadata->virtualPath.stem();
            if (matchingName) {
                return requestWithPreviewMesh(metadata.id);
            }
            if (!fallbackMeshId.IsValid()) fallbackMeshId = metadata.id;
        }
    }

    if (fallbackMeshId.IsValid()) {
        return requestWithPreviewMesh(fallbackMeshId);
    }

    pendingSkeletalMeshEditorAssetId_ = {};
    pendingSkeletalMeshEditorSkeletonId_ = skeletonId;
    pendingSkeletalMeshEditorPrimarySkeletonId_ = skeletonId;
    pendingSkeletalMeshEditorOpenEventId_ = diagnostics::EditorLagTrace::NextEventId();
    if (!manager.LoadAsync<kb::scene::SkeletonAsset>(skeletonId)) {
        pendingSkeletalMeshEditorSkeletonId_ = {};
        pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
        pendingSkeletalMeshEditorOpenEventId_ = 0U;
        console_.Error("Skeleton Editor", AssetErrorOr(manager, "Skeleton loading could not be started."));
        return false;
    }
    console_.Info("Skeleton Editor",
        "Loading document without preview geometry: " + skeletonMetadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::PumpPendingSkeletalMeshEditorOpen() {
    const bool skeletonOnly = !pendingSkeletalMeshEditorAssetId_.IsValid() &&
        pendingSkeletalMeshEditorPrimarySkeletonId_.IsValid() &&
        pendingSkeletalMeshEditorSkeletonId_.IsValid();
    if (!pendingSkeletalMeshEditorAssetId_.IsValid() && !skeletonOnly) {
        return false;
    }

    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetId meshId = pendingSkeletalMeshEditorAssetId_;
    const std::uint64_t eventId = pendingSkeletalMeshEditorOpenEventId_;
    if (!skeletonOnly && !pendingSkeletalMeshEditorSkeletonId_.IsValid()) {
        const kb::assets::AsyncAssetLoadStatus meshStatus = manager.AsyncLoadStatus(meshId);
        if (meshStatus == kb::assets::AsyncAssetLoadStatus::Pending) {
            return false;
        }
        if (meshStatus != kb::assets::AsyncAssetLoadStatus::Completed) {
            const std::string error = manager.AsyncLoadError(meshId);
            pendingSkeletalMeshEditorAssetId_ = {};
            pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
            pendingSkeletalMeshEditorOpenEventId_ = 0U;
            console_.Error("Skeletal Mesh Editor", error.empty()
                ? "Skeletal Mesh runtime data could not be loaded."
                : "Skeletal Mesh runtime data could not be loaded: " + error);
            return true;
        }

        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
            manager.AcquireLoaded<kb::scene::SkeletalMeshAsset>(meshId);
        if (!mesh.IsLoaded() || mesh->skeletonAssetId == 0U) {
            pendingSkeletalMeshEditorAssetId_ = {};
            pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
            pendingSkeletalMeshEditorOpenEventId_ = 0U;
            console_.Error("Skeletal Mesh Editor", "Skeletal Mesh runtime data or its Skeleton binding could not be loaded.");
            return true;
        }

        const kb::assets::AssetId skeletonId{ mesh->skeletonAssetId };
        const kb::assets::AssetMetadata* skeletonMetadata = manager.Registry().Find(skeletonId);
        if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType) {
            pendingSkeletalMeshEditorAssetId_ = {};
            pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
            pendingSkeletalMeshEditorOpenEventId_ = 0U;
            console_.Error("Skeletal Mesh Editor", "Skeletal Mesh references a missing Skeleton asset.");
            return true;
        }
        pendingSkeletalMeshEditorSkeletonId_ = skeletonId;
        if (!manager.LoadAsync<kb::scene::SkeletonAsset>(skeletonId)) {
            pendingSkeletalMeshEditorAssetId_ = {};
            pendingSkeletalMeshEditorSkeletonId_ = {};
            pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
            pendingSkeletalMeshEditorOpenEventId_ = 0U;
            console_.Error("Skeletal Mesh Editor", AssetErrorOr(manager, "Skeleton loading could not be started."));
            return true;
        }
    }

    const kb::assets::AssetId skeletonId = pendingSkeletalMeshEditorSkeletonId_;
    const kb::assets::AsyncAssetLoadStatus skeletonStatus = manager.AsyncLoadStatus(skeletonId);
    if (skeletonStatus == kb::assets::AsyncAssetLoadStatus::Pending) {
        return false;
    }
    if (skeletonStatus != kb::assets::AsyncAssetLoadStatus::Completed) {
        const std::string error = manager.AsyncLoadError(skeletonId);
        pendingSkeletalMeshEditorAssetId_ = {};
        pendingSkeletalMeshEditorSkeletonId_ = {};
        pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
        pendingSkeletalMeshEditorOpenEventId_ = 0U;
        console_.Error(skeletonOnly ? "Skeleton Editor" : "Skeletal Mesh Editor", error.empty()
            ? "Skeleton runtime data could not be loaded."
            : "Skeleton runtime data could not be loaded: " + error);
        return true;
    }

    const kb::assets::AssetId primarySkeletonId = pendingSkeletalMeshEditorPrimarySkeletonId_;
    pendingSkeletalMeshEditorAssetId_ = {};
    pendingSkeletalMeshEditorSkeletonId_ = {};
    pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
    pendingSkeletalMeshEditorOpenEventId_ = 0U;
    static_cast<void>(FinalizeLoadedSkeletalMeshEditorAsset(
        meshId, skeletonId, eventId, primarySkeletonId));
    return true;
}

bool EditorSceneContext::HasPendingSkeletalMeshEditorOpen() const noexcept {
    return pendingSkeletalMeshEditorAssetId_.IsValid() ||
        (pendingSkeletalMeshEditorPrimarySkeletonId_.IsValid() &&
            pendingSkeletalMeshEditorSkeletonId_.IsValid());
}

bool EditorSceneContext::FinalizeLoadedSkeletalMeshEditorAsset(
    kb::assets::AssetId meshId,
    kb::assets::AssetId skeletonId,
    std::uint64_t diagnosticEventId,
    kb::assets::AssetId primarySkeletonId) {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().Find(meshId);
    const kb::assets::AssetMetadata* skeletonMetadata = manager.Registry().Find(skeletonId);
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
        meshId.IsValid()
            ? manager.AcquireLoaded<kb::scene::SkeletalMeshAsset>(meshId)
            : kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset>{};
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
        manager.AcquireLoaded<kb::scene::SkeletonAsset>(skeletonId);
    if (meshId.IsValid() &&
        (meshMetadata == nullptr || meshMetadata->type != kb::scene::kSkeletalMeshAssetType || !mesh.IsLoaded())) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh runtime data could not be acquired after loading.");
        return false;
    }
    if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType ||
        !skeleton.IsLoaded() || (mesh.IsLoaded() &&
            (mesh->skeletonAssetId != skeletonId.value ||
                mesh->skeletonCompatibilitySignature != kb::scene::SkeletonCompatibilitySignature(*skeleton)))) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh and Skeleton are incompatible.");
        return false;
    }
    if (primarySkeletonId.IsValid() && primarySkeletonId != skeletonId) {
        console_.Error("Skeleton Editor", "Preview mesh is bound to a different Skeleton asset.");
        return false;
    }
    if (skeletonEditorDocument_.Dirty() && skeletonEditorDocument_.AssetId() != skeletonId) {
        console_.Warning("Skeleton Editor", "Unsaved Skeleton edits block opening an asset with another rig.");
        return false;
    }
    if (skeletalMeshEditorAssetId_ == meshId && animationPreviewScene_ != nullptr &&
        skeletalMeshEditorPrimarySkeletonId_ == primarySkeletonId) {
        const kb::assets::AssetMetadata& primaryMetadata = primarySkeletonId.IsValid()
            ? *skeletonMetadata
            : *meshMetadata;
        console_.Info(primarySkeletonId.IsValid() ? "Skeleton Editor" : "Skeletal Mesh Editor",
            "Focused existing document: " + primaryMetadata.virtualPath.generic_string());
        return true;
    }

    std::ostringstream assetDetail;
    assetDetail << "meshId=" << meshId.value << " skeletonId=" << skeletonId.value;
    const auto documentStart = std::chrono::steady_clock::now();
    if (animationPreviewScene_ != nullptr) {
        static_cast<void>(animationPreviewScene_->SetForcedLod(std::nullopt, 0U));
    }
    animationPreview_.SetAssets(skeletonId, meshId, {}, {});
    animationPreview_.SetPoseMode(AnimationPreviewPoseMode::Reference);
    static_cast<void>(animationPreview_.Overlays().SetBonesVisible(true));
    animatorEditorAssetId_ = {};
    animatorEditorController_.reset();
    animatorEditorGraphDocument_ = {};
    animationClipEditorAssetId_ = {};
    animationClipEditorTimeline_ = {};
    skeletalMeshEditorAssetId_ = meshId;
    skeletalMeshEditorPrimarySkeletonId_ = primarySkeletonId;
    if (skeletonEditorDocument_.AssetId() != skeletonId) {
        skeletonEditorDocument_.Open(skeletonId, *skeleton);
    }
    const kb::scene::SkeletonAsset* documentSkeleton = skeletonEditorDocument_.WorkingCopy();
    const kb::scene::SkeletonAsset& displayedSkeleton = documentSkeleton == nullptr ? *skeleton : *documentSkeleton;
    skeletalMeshEditorTree_.SetSkeleton(displayedSkeleton);
    if (primarySkeletonId.IsValid()) {
        if (skeletalMeshEditorDocument_.AssetId() != meshId) {
            skeletalMeshEditorDocument_.Close();
        }
        skeletalMeshEditorDetails_.SetSkeletonDocument(displayedSkeleton, *skeletonMetadata, meshMetadata);
    } else {
        if (skeletalMeshEditorDocument_.AssetId() != meshId) {
            skeletalMeshEditorDocument_.Open(meshId, *mesh);
        }
        const kb::scene::SkeletalMeshAsset* documentMesh = skeletalMeshEditorDocument_.WorkingCopy();
        skeletalMeshEditorDetails_.SetDocument(
            documentMesh == nullptr ? *mesh : *documentMesh, displayedSkeleton, *meshMetadata);
    }
    const double documentMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - documentStart).count();
    diagnostics::EditorLagTrace::Slow("skeletal-open-document", diagnosticEventId, documentMs, assetDetail.str(), 4.0);
    const auto previewStart = std::chrono::steady_clock::now();
    static_cast<void>(AnimationPreviewScene());
    const double previewMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - previewStart).count();
    diagnostics::EditorLagTrace::Slow("skeletal-open-preview", diagnosticEventId, previewMs, assetDetail.str(), 4.0);
    const kb::assets::AssetMetadata& primaryMetadata = primarySkeletonId.IsValid()
        ? *skeletonMetadata
        : *meshMetadata;
    console_.Info(primarySkeletonId.IsValid() ? "Skeleton Editor" : "Skeletal Mesh Editor",
        "Opened document: " + primaryMetadata.virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::OpenSkeletalMeshEditorAsset(kb::assets::AssetId id) {
    pendingSkeletalMeshEditorAssetId_ = {};
    pendingSkeletalMeshEditorSkeletonId_ = {};
    pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
    pendingSkeletalMeshEditorOpenEventId_ = 0U;
    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto totalStart = std::chrono::steady_clock::now();
    std::ostringstream assetDetail;
    assetDetail << "assetId=" << id.value;
    const kb::assets::AssetMetadata* meshMetadata = scene_->Assets().Manager().Registry().Find(id);
    if (meshMetadata == nullptr || meshMetadata->type != kb::scene::kSkeletalMeshAssetType) {
        console_.Error("Skeletal Mesh Editor", "Selected asset is not a Skeletal Mesh.");
        return false;
    }
    const auto meshLoadStart = std::chrono::steady_clock::now();
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
        scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(id);
    const double meshLoadMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - meshLoadStart).count();
    diagnostics::EditorLagTrace::Slow("skeletal-open-mesh-load", eventId, meshLoadMs, assetDetail.str(), 4.0);
    if (!mesh.IsLoaded() || mesh->skeletonAssetId == 0U) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh runtime data or its Skeleton binding could not be loaded.");
        return false;
    }
    const kb::assets::AssetId skeletonId{ mesh->skeletonAssetId };
    const kb::assets::AssetMetadata* skeletonMetadata = scene_->Assets().Manager().Registry().Find(skeletonId);
    if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh references a missing Skeleton asset.");
        return false;
    }
    const auto skeletonLoadStart = std::chrono::steady_clock::now();
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
        scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(skeletonId);
    const double skeletonLoadMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - skeletonLoadStart).count();
    diagnostics::EditorLagTrace::Slow("skeletal-open-skeleton-load", eventId, skeletonLoadMs, assetDetail.str(), 4.0);
    if (!skeleton.IsLoaded()) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh and Skeleton are incompatible.");
        return false;
    }
    const bool opened = FinalizeLoadedSkeletalMeshEditorAsset(id, skeletonId, eventId);
    const double totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - totalStart).count();
    diagnostics::EditorLagTrace::Slow("skeletal-open-total", eventId, totalMs, assetDetail.str(), 4.0);
    return opened;
}

kb::assets::AssetId EditorSceneContext::SkeletalMeshEditorAssetId() const noexcept {
    return skeletalMeshEditorAssetId_;
}

kb::assets::AssetId EditorSceneContext::SkeletalMeshEditorSkeletonAssetId() const noexcept {
    return pendingSkeletalMeshEditorSkeletonId_.IsValid()
        ? pendingSkeletalMeshEditorSkeletonId_
        : animationPreview_.SkeletonAsset();
}

kb::assets::AssetId EditorSceneContext::RequestedSkeletalMeshEditorAssetId() const noexcept {
    if (pendingSkeletalMeshEditorPrimarySkeletonId_.IsValid()) {
        return pendingSkeletalMeshEditorPrimarySkeletonId_;
    }
    if (skeletalMeshEditorPrimarySkeletonId_.IsValid() &&
        !pendingSkeletalMeshEditorAssetId_.IsValid()) {
        return skeletalMeshEditorPrimarySkeletonId_;
    }
    return pendingSkeletalMeshEditorAssetId_.IsValid()
        ? pendingSkeletalMeshEditorAssetId_
        : skeletalMeshEditorAssetId_;
}

bool EditorSceneContext::IsSkeletalMeshEditorSkeletonDocument() const noexcept {
    return HasPendingSkeletalMeshEditorOpen()
        ? pendingSkeletalMeshEditorPrimarySkeletonId_.IsValid()
        : skeletalMeshEditorPrimarySkeletonId_.IsValid();
}

bool EditorSceneContext::SwitchSkeletalMeshEditorDocument(bool skeletonDocument) {
    if (!HasSkeletalMeshEditorAsset() || HasPendingSkeletalMeshEditorOpen()) return false;
    const kb::assets::AssetId skeletonId = animationPreview_.SkeletonAsset();
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* skeletonMetadata = manager.Registry().Find(skeletonId);
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
        manager.AcquireLoaded<kb::scene::SkeletonAsset>(skeletonId);
    if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType ||
        !skeleton.IsLoaded()) {
        console_.Error("Skeleton Editor", "The linked Skeleton runtime data is unavailable.");
        return false;
    }

    if (skeletonDocument) {
        const kb::assets::AssetMetadata* meshMetadata = skeletalMeshEditorAssetId_.IsValid()
            ? manager.Registry().Find(skeletalMeshEditorAssetId_)
            : nullptr;
        skeletalMeshEditorPrimarySkeletonId_ = skeletonId;
        if (skeletonEditorDocument_.AssetId() != skeletonId) {
            skeletonEditorDocument_.Open(skeletonId, *skeleton);
        }
        const kb::scene::SkeletonAsset* documentSkeleton = skeletonEditorDocument_.WorkingCopy();
        skeletalMeshEditorDetails_.SetSkeletonDocument(
            documentSkeleton == nullptr ? *skeleton : *documentSkeleton,
            *skeletonMetadata,
            meshMetadata);
        console_.Info("Skeleton Editor", "Switched to linked Skeleton: " + skeletonMetadata->virtualPath.generic_string());
        return true;
    }

    const kb::assets::AssetId meshId = skeletalMeshEditorAssetId_;
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().Find(meshId);
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
        meshId.IsValid()
            ? manager.AcquireLoaded<kb::scene::SkeletalMeshAsset>(meshId)
            : kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset>{};
    if (meshMetadata == nullptr || meshMetadata->type != kb::scene::kSkeletalMeshAssetType ||
        !mesh.IsLoaded()) {
        console_.Warning("Skeletal Mesh Editor", "This Skeleton has no compatible preview mesh to open.");
        return false;
    }
    if (!skeletalMeshEditorDocument_.IsOpen()) {
        skeletalMeshEditorDocument_.Open(meshId, *mesh);
    }
    const kb::scene::SkeletalMeshAsset* documentMesh = skeletalMeshEditorDocument_.WorkingCopy();
    const kb::scene::SkeletonAsset* documentSkeleton = skeletonEditorDocument_.WorkingCopy();
    skeletalMeshEditorPrimarySkeletonId_ = {};
    skeletalMeshEditorDetails_.SetDocument(
        documentMesh == nullptr ? *mesh : *documentMesh,
        documentSkeleton == nullptr ? *skeleton : *documentSkeleton,
        *meshMetadata);
    console_.Info("Skeletal Mesh Editor", "Switched to linked Skeletal Mesh: " + meshMetadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::HasSkeletalMeshEditorAsset() const noexcept {
    return (skeletalMeshEditorAssetId_.IsValid() || skeletalMeshEditorPrimarySkeletonId_.IsValid()) &&
        animationPreview_.SkeletonAsset().IsValid() && animationPreviewScene_ != nullptr &&
        animationPreviewScene_->CurrentScene() != nullptr;
}

const kb::scene::Scene* EditorSceneContext::SkeletalMeshEditorPreviewScene() const noexcept {
    return animationPreviewScene_ == nullptr ? nullptr : animationPreviewScene_->CurrentScene();
}

std::uint64_t EditorSceneContext::SkeletalMeshEditorPreviewRevision() const noexcept {
    if (animationPreviewScene_ == nullptr) return 0U;
    std::uint64_t revision = animationPreviewScene_->Revision();
    revision ^= animationPreview_.Revision() + 0x9e3779b97f4a7c15ULL + (revision << 6U) + (revision >> 2U);
    revision ^= animationPreview_.Overlays().Revision() + 0x9e3779b97f4a7c15ULL + (revision << 6U) + (revision >> 2U);
    return revision;
}

bool EditorSceneContext::SetSkeletalMeshEditorTreeFilter(std::string filter) {
    return skeletalMeshEditorTree_.SetFilter(std::move(filter));
}

const std::string& EditorSceneContext::SkeletalMeshEditorTreeFilter() const noexcept {
    return skeletalMeshEditorTree_.Filter();
}

bool EditorSceneContext::IsSkeletalMeshEditorTreeSearchFocused() const noexcept {
    return skeletalMeshEditorTree_.IsSearchFocused();
}

void EditorSceneContext::FocusSkeletalMeshEditorTreeSearch(bool focused) noexcept {
    skeletalMeshEditorTree_.FocusSearch(focused);
}

void EditorSceneContext::AppendSkeletalMeshEditorTreeSearchText(wchar_t character) {
    skeletalMeshEditorTree_.AppendSearchText(character);
}

void EditorSceneContext::InsertSkeletalMeshEditorTreeSearchText(std::string_view text) {
    skeletalMeshEditorTree_.InsertSearchText(text);
}

void EditorSceneContext::BackspaceSkeletalMeshEditorTreeSearch() {
    skeletalMeshEditorTree_.BackspaceSearch();
}

void EditorSceneContext::SelectAllSkeletalMeshEditorTreeSearch() noexcept {
    skeletalMeshEditorTree_.SelectAllSearch();
}

void EditorSceneContext::ClearSkeletalMeshEditorTreeSearch() {
    skeletalMeshEditorTree_.ClearSearch();
}

std::vector<SkeletalMeshEditorTreeRow> EditorSceneContext::SkeletalMeshEditorTreeRows() const {
    return skeletalMeshEditorTree_.Rows();
}

bool EditorSceneContext::ToggleSkeletalMeshEditorTreeBoneExpanded(
    kb::scene::SkeletonBoneId boneId) {
    return skeletalMeshEditorTree_.ToggleExpanded(boneId);
}

int EditorSceneContext::SkeletalMeshEditorTreeScrollOffset() const noexcept {
    return skeletalMeshEditorTree_.ScrollOffset();
}

bool EditorSceneContext::IsSkeletalMeshEditorTreeScrollbarDragging() const noexcept {
    return skeletalMeshEditorTree_.IsScrollbarDragging();
}

bool EditorSceneContext::SetSkeletalMeshEditorTreeScrollOffset(int offset, int maxOffset) noexcept {
    return skeletalMeshEditorTree_.SetScrollOffset(offset, maxOffset);
}

void EditorSceneContext::BeginSkeletalMeshEditorTreeScrollbarDrag(int y) noexcept {
    skeletalMeshEditorTree_.BeginScrollbarDrag(y);
}

void EditorSceneContext::DragSkeletalMeshEditorTreeScrollbar(
    int y, int trackTravel, int maxOffset) noexcept {
    skeletalMeshEditorTree_.DragScrollbar(y, trackTravel, maxOffset);
}

void EditorSceneContext::EndSkeletalMeshEditorTreeScrollbarDrag() noexcept {
    skeletalMeshEditorTree_.EndScrollbarDrag();
}

int EditorSceneContext::SkeletalMeshEditorToolboxWidth() const noexcept {
    return skeletalMeshEditorPanelResize_.ToolboxWidth();
}

int EditorSceneContext::SkeletalMeshEditorSkeletonTreeWidth() const noexcept {
    return skeletalMeshEditorPanelResize_.SkeletonTreeWidth();
}

int EditorSceneContext::SkeletalMeshEditorSkeletonTreeHeight() const noexcept {
    return skeletalMeshEditorPanelResize_.SkeletonTreeHeight();
}

bool EditorSceneContext::IsSkeletalMeshEditorToolboxWidthDragging() const noexcept {
    return skeletalMeshEditorPanelResize_.IsDragging(
        SkeletalMeshEditorPanelDrag::ToolboxWidth);
}

bool EditorSceneContext::IsSkeletalMeshEditorSkeletonTreeWidthDragging() const noexcept {
    return skeletalMeshEditorPanelResize_.IsDragging(
        SkeletalMeshEditorPanelDrag::SkeletonTreeWidth);
}

bool EditorSceneContext::IsSkeletalMeshEditorTreeDetailsHeightDragging() const noexcept {
    return skeletalMeshEditorPanelResize_.IsDragging(
        SkeletalMeshEditorPanelDrag::TreeDetailsHeight);
}

void EditorSceneContext::SetSkeletalMeshEditorToolboxWidth(int width) noexcept {
    skeletalMeshEditorPanelResize_.SetToolboxWidth(width);
}

void EditorSceneContext::SetSkeletalMeshEditorSkeletonTreeWidth(int width) noexcept {
    skeletalMeshEditorPanelResize_.SetSkeletonTreeWidth(width);
}

void EditorSceneContext::SetSkeletalMeshEditorSkeletonTreeHeight(int height) noexcept {
    skeletalMeshEditorPanelResize_.SetSkeletonTreeHeight(height);
}

void EditorSceneContext::BeginSkeletalMeshEditorToolboxWidthDrag() noexcept {
    skeletalMeshEditorPanelResize_.BeginDrag(
        SkeletalMeshEditorPanelDrag::ToolboxWidth);
}

void EditorSceneContext::BeginSkeletalMeshEditorSkeletonTreeWidthDrag() noexcept {
    skeletalMeshEditorPanelResize_.BeginDrag(
        SkeletalMeshEditorPanelDrag::SkeletonTreeWidth);
}

void EditorSceneContext::BeginSkeletalMeshEditorTreeDetailsHeightDrag() noexcept {
    skeletalMeshEditorPanelResize_.BeginDrag(
        SkeletalMeshEditorPanelDrag::TreeDetailsHeight);
}

void EditorSceneContext::EndSkeletalMeshEditorPanelResizeDrag() noexcept {
    skeletalMeshEditorPanelResize_.EndDrag();
}

bool EditorSceneContext::SelectSkeletalMeshEditorBone(kb::scene::SkeletonBoneId boneId) {
    const bool changed = skeletalMeshEditorTree_.SelectBone(boneId);
    if (animationClipEditorAssetId_.IsValid()) {
        static_cast<void>(animationClipEditorTimeline_.SelectBoneTrack(boneId));
    }
    return changed;
}

bool EditorSceneContext::SelectSkeletalMeshEditorSocket(std::string socketName) {
    return skeletalMeshEditorTree_.SelectSocket(std::move(socketName));
}

bool EditorSceneContext::ClearSkeletalMeshEditorTreeSelection() {
    return skeletalMeshEditorTree_.ClearSelection();
}

kb::scene::SkeletonBoneId EditorSceneContext::SelectedSkeletalMeshEditorBone() const noexcept {
    return skeletalMeshEditorTree_.SelectedBone();
}

const std::string& EditorSceneContext::SelectedSkeletalMeshEditorSocket() const noexcept {
    return skeletalMeshEditorTree_.SelectedSocket();
}

SkeletalMeshEditorDetailsModel EditorSceneContext::SkeletalMeshEditorDetails() const {
    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto start = std::chrono::steady_clock::now();
    const kb::scene::SkeletonBoneId selectedBone = skeletalMeshEditorTree_.SelectedBone();
    const std::string& selectedSocket = skeletalMeshEditorTree_.SelectedSocket();
    SkeletalMeshEditorDetailsModel details = skeletalMeshEditorDetails_.Build(selectedBone, selectedSocket);
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    for (SkeletalMeshEditorDetailsSection& section : details.sections) {
        for (SkeletalMeshEditorDetailsField& field : section.fields) {
            if (field.action == SkeletalMeshEditorDetailsAction::SectionMaterial && field.assetId != 0U) {
                const kb::assets::AssetMetadata* material =
                    scene_->Assets().Manager().Registry().Find(kb::assets::AssetId{ field.assetId });
                if (material != nullptr) {
                    const std::string filename = material->virtualPath.filename().string();
                    field.value = filename.empty() ? material->name : filename;
                } else {
                    field.value = "Missing (" + std::to_string(field.assetId) + ")";
                }
            } else if (field.action == SkeletalMeshEditorDetailsAction::PreviewLod &&
                       working != nullptr && animationPreviewScene_ != nullptr) {
                const std::uint32_t resolved = animationPreviewScene_->ResolvePreviewLod(*working);
                const std::optional<std::uint32_t> forced = animationPreviewScene_->ForcedLod();
                field.value = forced.has_value()
                    ? "LOD " + std::to_string(resolved)
                    : "Auto (LOD " + std::to_string(resolved) + ")";
            }
        }
    }
    const double durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    const char* selection = selectedBone != 0U ? "bone" : (!selectedSocket.empty() ? "socket" : "asset");
    diagnostics::EditorLagTrace::Slow(
        "skeletal-details-build",
        eventId,
        durationMs,
        std::string{"selection="} + selection + " assetId=" + std::to_string(skeletalMeshEditorAssetId_.value),
        4.0);
    return details;
}

bool EditorSceneContext::ToggleSkeletalMeshEditorDetailsSection(std::string_view title) {
    return skeletalMeshEditorDetails_.ToggleSection(title);
}

int EditorSceneContext::SkeletalMeshEditorDetailsScrollOffset() const noexcept {
    return skeletalMeshEditorDetails_.ScrollOffset();
}

bool EditorSceneContext::IsSkeletalMeshEditorDetailsScrollbarDragging() const noexcept {
    return skeletalMeshEditorDetails_.IsScrollbarDragging();
}

bool EditorSceneContext::SetSkeletalMeshEditorDetailsScrollOffset(int offset, int maxOffset) noexcept {
    return skeletalMeshEditorDetails_.SetScrollOffset(offset, maxOffset);
}

void EditorSceneContext::BeginSkeletalMeshEditorDetailsScrollbarDrag(int y) noexcept {
    skeletalMeshEditorDetails_.BeginScrollbarDrag(y);
}

void EditorSceneContext::DragSkeletalMeshEditorDetailsScrollbar(
    int y, int trackTravel, int maxOffset) noexcept {
    skeletalMeshEditorDetails_.DragScrollbar(y, trackTravel, maxOffset);
}

void EditorSceneContext::EndSkeletalMeshEditorDetailsScrollbarDrag() noexcept {
    skeletalMeshEditorDetails_.EndScrollbarDrag();
}

std::uint32_t EditorSceneContext::SkeletalMeshEditorLodCount() const noexcept {
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    return working == nullptr ? 0U : static_cast<std::uint32_t>(working->lods.size());
}

std::optional<std::uint32_t> EditorSceneContext::SkeletalMeshEditorForcedPreviewLod() const noexcept {
    return animationPreviewScene_ == nullptr ? std::nullopt : animationPreviewScene_->ForcedLod();
}

bool EditorSceneContext::SetSkeletalMeshEditorPreviewLod(std::optional<std::uint32_t> lodIndex) {
    if (IsSkeletalMeshEditorSkeletonDocument() || animationPreviewScene_ == nullptr) return false;
    const std::uint32_t lodCount = SkeletalMeshEditorLodCount();
    if (lodCount == 0U) return false;
    return animationPreviewScene_->SetForcedLod(lodIndex, lodCount);
}

bool EditorSceneContext::CommitSkeletalMeshEditorCandidate(kb::scene::SkeletalMeshAsset candidate) {
    if (IsSkeletalMeshEditorSkeletonDocument() || !skeletalMeshEditorDocument_.IsOpen() ||
        !kb::scene::ValidateSkeletalMeshAsset(candidate).valid ||
        !skeletalMeshEditorDocument_.Apply(std::move(candidate))) return false;
    const kb::scene::SkeletalMeshAsset* updated = skeletalMeshEditorDocument_.WorkingCopy();
    if (updated == nullptr || !scene_->Assets().Manager().PublishRuntimeAsset(
            skeletalMeshEditorAssetId_, std::make_shared<kb::scene::SkeletalMeshAsset>(*updated))) {
        static_cast<void>(skeletalMeshEditorDocument_.Undo());
        const kb::scene::SkeletalMeshAsset* restored = skeletalMeshEditorDocument_.WorkingCopy();
        if (restored != nullptr) {
            static_cast<void>(scene_->Assets().Manager().PublishRuntimeAsset(
                skeletalMeshEditorAssetId_, std::make_shared<kb::scene::SkeletalMeshAsset>(*restored)));
        }
        return false;
    }
    RefreshSkeletalEditorDetails();
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::SetSkeletalMeshEditorLodScreenCoverage(
    std::uint32_t lodIndex, float coverage) {
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (working == nullptr || lodIndex >= working->lods.size() || !std::isfinite(coverage) ||
        coverage < 0.0F || coverage > 1.0F || working->lods[lodIndex].minScreenCoverage == coverage) return false;
    if ((lodIndex > 0U && coverage > working->lods[lodIndex - 1U].minScreenCoverage) ||
        (lodIndex + 1U < working->lods.size() && coverage < working->lods[lodIndex + 1U].minScreenCoverage)) {
        console_.Warning("Skeletal Mesh Editor", "LOD screen coverage must remain non-increasing from LOD 0.");
        return false;
    }
    kb::scene::SkeletalMeshAsset candidate = *working;
    candidate.lods[lodIndex].minScreenCoverage = coverage;
    return CommitSkeletalMeshEditorCandidate(std::move(candidate));
}

bool EditorSceneContext::SetSkeletalMeshEditorFixedBounds(
    std::optional<kb::scene::Vec3> center, std::optional<kb::scene::Vec3> extents) {
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (working == nullptr || (!center.has_value() && !extents.has_value())) return false;
    kb::scene::SkeletalMeshAsset candidate = *working;
    if (center.has_value()) candidate.fixedBounds.center = *center;
    if (extents.has_value()) candidate.fixedBounds.extents = *extents;
    return CommitSkeletalMeshEditorCandidate(std::move(candidate));
}

const std::vector<kb::scene::SkeletalMeshMorphTarget>& EditorSceneContext::SkeletalMeshEditorMorphTargets() const noexcept {
    return skeletalMeshEditorDetails_.MorphTargets();
}

bool EditorSceneContext::HasDirtySkeletalMeshEditorAssetEdit() const noexcept {
    return skeletalMeshEditorDocument_.Dirty() || skeletonEditorDocument_.Dirty();
}

bool EditorSceneContext::HasDirtyActiveSkeletalMeshEditorDocument() const noexcept {
    return IsSkeletalMeshEditorSkeletonDocument()
        ? skeletonEditorDocument_.Dirty()
        : skeletalMeshEditorDocument_.Dirty();
}

bool EditorSceneContext::CanUndoSkeletalMeshEditorAssetEdit() const noexcept {
    return IsSkeletalMeshEditorSkeletonDocument()
        ? skeletonEditorDocument_.CanUndo()
        : skeletalMeshEditorDocument_.CanUndo();
}

bool EditorSceneContext::CanRedoSkeletalMeshEditorAssetEdit() const noexcept {
    return IsSkeletalMeshEditorSkeletonDocument()
        ? skeletonEditorDocument_.CanRedo()
        : skeletalMeshEditorDocument_.CanRedo();
}

bool EditorSceneContext::CanReloadSkeletalMeshEditorAsset() const noexcept {
    return IsSkeletalMeshEditorSkeletonDocument()
        ? skeletonEditorDocument_.IsOpen()
        : skeletalMeshEditorDocument_.IsOpen();
}

kb::scene::SkeletalMeshBoundsMode EditorSceneContext::SkeletalMeshEditorBoundsMode() const noexcept {
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    return working == nullptr
        ? kb::scene::SkeletalMeshBoundsMode::ImportedConservative
        : working->boundsMode;
}

bool EditorSceneContext::IsSkeletalMeshEditorReferencePose() const noexcept {
    return animationPreview_.PoseMode() == AnimationPreviewPoseMode::Reference &&
        !animationPreview_.Transport().IsPlaying() &&
        animationPreview_.Transport().NormalizedTime() == 0.0F;
}

bool EditorSceneContext::SetSkeletalMeshEditorBoundsMode(kb::scene::SkeletalMeshBoundsMode mode) {
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (working == nullptr || working->boundsMode == mode) return false;
    kb::scene::SkeletalMeshAsset candidate = *working;
    candidate.boundsMode = mode;
    return CommitSkeletalMeshEditorCandidate(std::move(candidate));
}

bool EditorSceneContext::ToggleSkeletalMeshEditorBoundsMode() {
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (working == nullptr) return false;
    return SetSkeletalMeshEditorBoundsMode(
        working->boundsMode == kb::scene::SkeletalMeshBoundsMode::Fixed
            ? kb::scene::SkeletalMeshBoundsMode::ImportedConservative
            : kb::scene::SkeletalMeshBoundsMode::Fixed);
}

bool EditorSceneContext::FocusSkeletalMeshEditorPreview() noexcept {
    if (!HasSkeletalMeshEditorAsset() || animationPreviewScene_ == nullptr) return false;
    animationPreviewScene_->Focus(0.15F);
    return true;
}

bool EditorSceneContext::ShowSkeletalMeshEditorReferencePose() {
    if (!HasSkeletalMeshEditorAsset()) return false;
    const bool changed = !IsSkeletalMeshEditorReferencePose();
    animationPreview_.SetPoseMode(AnimationPreviewPoseMode::Reference);
    static_cast<void>(animationPreview_.Transport().SetPlaying(false));
    static_cast<void>(animationPreview_.Transport().Scrub(0.0F));
    if (changed) {
        animationPreviewScene_->Clear();
        static_cast<void>(AnimationPreviewScene());
    }
    // The command is successful even when the preview was already at the reference pose. This is
    // an idempotent state command, not a toggle, and callers still need to repaint its active state.
    return true;
}

bool EditorSceneContext::UndoSkeletalMeshEditorAssetEdit() {
    if (IsSkeletalMeshEditorSkeletonDocument()) {
        return skeletonEditorDocument_.Undo() && PublishSkeletonEditorWorkingCopy();
    }
    if (!skeletalMeshEditorDocument_.Undo()) return false;
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (working == nullptr || !scene_->Assets().Manager().PublishRuntimeAsset(
            skeletalMeshEditorAssetId_, std::make_shared<kb::scene::SkeletalMeshAsset>(*working))) return false;
    RefreshSkeletalEditorDetails();
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::RedoSkeletalMeshEditorAssetEdit() {
    if (IsSkeletalMeshEditorSkeletonDocument()) {
        return skeletonEditorDocument_.Redo() && PublishSkeletonEditorWorkingCopy();
    }
    if (!skeletalMeshEditorDocument_.Redo()) return false;
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (working == nullptr || !scene_->Assets().Manager().PublishRuntimeAsset(
            skeletalMeshEditorAssetId_, std::make_shared<kb::scene::SkeletalMeshAsset>(*working))) return false;
    RefreshSkeletalEditorDetails();
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::SaveSkeletalMeshEditorAsset() {
    if (skeletonEditorDocument_.Dirty()) {
        const kb::assets::AssetMetadata* skeletonMetadata =
            scene_->Assets().Manager().Registry().Find(skeletonEditorDocument_.AssetId());
        const kb::scene::SkeletonAsset* skeleton = skeletonEditorDocument_.WorkingCopy();
        if (skeletonMetadata == nullptr || skeleton == nullptr ||
            !kb::scene::SkeletonAssetIO::Save(skeletonMetadata->physicalPath, *skeleton)) {
            console_.Error("Skeleton Editor", "Skeleton working copy could not be saved atomically.");
            return false;
        }
        static_cast<void>(skeletonEditorDocument_.MarkSaved());
        console_.Info("Skeleton Editor", "Saved Skeleton working copy.");
    }
    if (!skeletalMeshEditorDocument_.Dirty()) {
        static_cast<void>(scene_->Assets().Manager().DiscoverMountedAssets());
        return true;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(skeletalMeshEditorAssetId_);
    const kb::scene::SkeletalMeshAsset* working = skeletalMeshEditorDocument_.WorkingCopy();
    if (metadata == nullptr || working == nullptr || !kb::scene::SkeletalMeshAssetIO::Save(metadata->physicalPath, *working)) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh working copy could not be saved atomically.");
        return false;
    }
    static_cast<void>(scene_->Assets().Manager().DiscoverMountedAssets());
    static_cast<void>(skeletalMeshEditorDocument_.MarkSaved());
    console_.Info("Skeletal Mesh Editor", "Saved Skeletal Mesh working copy.");
    return true;
}

bool EditorSceneContext::RevertSkeletalMeshEditorAsset() {
    if (skeletonEditorDocument_.Dirty()) {
        if (!skeletonEditorDocument_.RevertToSaved() || !PublishSkeletonEditorWorkingCopy()) return false;
    }
    if (!skeletalMeshEditorDocument_.Dirty()) return true;
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(skeletalMeshEditorAssetId_);
    std::string error;
    const std::optional<kb::scene::SkeletalMeshAsset> candidate = metadata == nullptr
        ? std::nullopt : kb::scene::SkeletalMeshAssetIO::Load(metadata->physicalPath, &error);
    if (!candidate.has_value() || !skeletalMeshEditorDocument_.ReplaceFromReimport(*candidate)) {
        console_.Error("Skeletal Mesh Editor", error.empty() ? "Skeletal Mesh could not be reverted." : error);
        return false;
    }
    static_cast<void>(scene_->Assets().Manager().PublishRuntimeAsset(
        skeletalMeshEditorAssetId_, std::make_shared<kb::scene::SkeletalMeshAsset>(*candidate)));
    RefreshSkeletalEditorDetails();
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    console_.Info("Skeletal Mesh Editor", "Reverted Skeletal Mesh working copy.");
    return true;
}

bool EditorSceneContext::ReloadSkeletalMeshEditorAsset() {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    if (IsSkeletalMeshEditorSkeletonDocument()) {
        if (!skeletonEditorDocument_.IsOpen()) return false;
        const kb::assets::AssetId skeletonId = skeletonEditorDocument_.AssetId();
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(skeletonId);
        std::string error;
        const std::optional<kb::scene::SkeletonAsset> candidate = metadata == nullptr
            ? std::nullopt : kb::scene::SkeletonAssetIO::Load(metadata->physicalPath, &error);
        if (!candidate.has_value()) {
            console_.Error("Skeleton Editor", error.empty() ? "Skeleton could not be reloaded." : error);
            return false;
        }

        const kb::scene::SkeletalMeshAsset* linkedMesh = skeletalMeshEditorDocument_.WorkingCopy();
        if (linkedMesh != nullptr &&
            (linkedMesh->skeletonAssetId != skeletonId.value ||
                linkedMesh->skeletonCompatibilitySignature != kb::scene::SkeletonCompatibilitySignature(*candidate))) {
            console_.Error(
                "Skeleton Editor",
                "Reloaded Skeleton is incompatible with the linked Skeletal Mesh. Reload or reimport the matching mesh first.");
            return false;
        }
        if (!skeletonEditorDocument_.ReplaceFromReload(*candidate) ||
            !manager.PublishRuntimeAsset(skeletonId, std::make_shared<kb::scene::SkeletonAsset>(*candidate))) {
            console_.Error("Skeleton Editor", "Skeleton runtime data could not be published after reload.");
            return false;
        }
        RefreshSkeletalEditorDetails();
        animationPreviewScene_->Clear();
        static_cast<void>(AnimationPreviewScene());
        console_.Info("Skeleton Editor", "Reloaded Skeleton from its authored asset.");
        return true;
    }

    if (!skeletalMeshEditorDocument_.IsOpen()) return false;
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(skeletalMeshEditorAssetId_);
    std::string error;
    const std::optional<kb::scene::SkeletalMeshAsset> candidate = metadata == nullptr
        ? std::nullopt : kb::scene::SkeletalMeshAssetIO::Load(metadata->physicalPath, &error);
    const kb::scene::SkeletonAsset* skeleton = skeletonEditorDocument_.WorkingCopy();
    if (!candidate.has_value()) {
        console_.Error("Skeletal Mesh Editor", error.empty() ? "Skeletal Mesh could not be reloaded." : error);
        return false;
    }
    if (skeleton == nullptr || candidate->skeletonAssetId != skeletonEditorDocument_.AssetId().value ||
        candidate->skeletonCompatibilitySignature != kb::scene::SkeletonCompatibilitySignature(*skeleton)) {
        console_.Error("Skeletal Mesh Editor", "Reloaded Skeletal Mesh is incompatible with the open Skeleton.");
        return false;
    }
    if (!skeletalMeshEditorDocument_.ReplaceFromReimport(*candidate) ||
        !manager.PublishRuntimeAsset(
            skeletalMeshEditorAssetId_, std::make_shared<kb::scene::SkeletalMeshAsset>(*candidate))) {
        console_.Error("Skeletal Mesh Editor", "Skeletal Mesh runtime data could not be published after reload.");
        return false;
    }
    RefreshSkeletalEditorDetails();
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    console_.Info("Skeletal Mesh Editor", "Reloaded Skeletal Mesh from its authored asset.");
    return true;
}

bool EditorSceneContext::PrepareSkeletalMeshEditorClose(std::string_view reason) {
    if (!HasDirtySkeletalMeshEditorAssetEdit()) return true;
    console_.Warning("Skeletal Asset Editor", "Unsaved skeletal asset edits block " + std::string{ reason } + ". Save or Revert first.");
    return false;
}

void EditorSceneContext::CloseSkeletalMeshEditorAsset() noexcept {
    pendingSkeletalMeshEditorAssetId_ = {};
    pendingSkeletalMeshEditorSkeletonId_ = {};
    pendingSkeletalMeshEditorPrimarySkeletonId_ = {};
    pendingSkeletalMeshEditorOpenEventId_ = 0U;
    skeletalMeshEditorDocument_.Close();
    skeletonEditorDocument_.Close();
    skeletalMeshEditorAssetId_ = {};
    skeletalMeshEditorPrimarySkeletonId_ = {};
}

} // namespace kb::editor
