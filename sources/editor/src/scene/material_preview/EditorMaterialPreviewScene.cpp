#include "scene/material_preview/EditorMaterialPreviewScene.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialSemanticHash.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"

#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace kb::editor {
namespace {

[[nodiscard]] std::uint64_t ProcessIdentity() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

[[nodiscard]] std::uint64_t AssetDependencyHash(const kb::scene::Scene& scene, std::uint64_t assetId) noexcept {
    if (assetId == 0U) {
        return 0U;
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ assetId });
    const std::uint64_t contentHash = metadata == nullptr ? 0U : metadata->contentHash;
    return assetId ^ (contentHash + 0x9e3779b97f4a7c15ULL + (assetId << 6U) + (assetId >> 2U));
}

[[nodiscard]] std::uint64_t HashCombine(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
}

[[nodiscard]] std::uint64_t HashBytes(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] std::uint64_t MaterialTextureDependencyHash(
    const kb::scene::Scene& scene,
    const kb::render::RenderMaterialAssetData& material) noexcept {
    std::vector<std::uint64_t> textureAssetIds;
    const auto appendTexture = [&textureAssetIds](std::uint64_t assetId) {
        if (assetId != 0U) {
            textureAssetIds.push_back(assetId);
        }
    };
    appendTexture(material.desc.albedoTextureAssetId);
    appendTexture(material.desc.normalTextureAssetId);
    appendTexture(material.desc.metallicRoughnessTextureAssetId);
    appendTexture(material.desc.occlusionTextureAssetId);
    appendTexture(material.desc.emissiveTextureAssetId);
    appendTexture(material.desc.clearcoatTextureAssetId);
    appendTexture(material.desc.clearcoatRoughnessTextureAssetId);
    appendTexture(material.desc.sheenColorTextureAssetId);
    appendTexture(material.desc.transmissionTextureAssetId);
    appendTexture(material.desc.thicknessTextureAssetId);
    appendTexture(material.desc.anisotropyTextureAssetId);
    appendTexture(material.desc.decalTextureAssetId);
    appendTexture(material.desc.layerMaskTextureAssetId);
    for (const kb::render::RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.type == kb::render::RenderMaterialParameterType::Texture) {
            appendTexture(value.assetId);
        }
    }
    std::ranges::sort(textureAssetIds);
    textureAssetIds.erase(std::unique(textureAssetIds.begin(), textureAssetIds.end()), textureAssetIds.end());
    std::uint64_t hash = 0xBADC0FFEE0DDF00DULL;
    for (const std::uint64_t assetId : textureAssetIds) {
        hash = HashCombine(hash, AssetDependencyHash(scene, assetId));
    }
    return hash;
}

[[nodiscard]] std::uint64_t WorkingCopyContentHash(const kb::render::RenderMaterialAssetData& material) {
    return kb::render::RenderMaterialRuntimeSemanticHash(material);
}

[[nodiscard]] std::filesystem::path WorkingCopyPreviewPath(
    const kb::scene::Scene& scene,
    kb::assets::AssetId materialAssetId,
    std::uint64_t contentHash,
    kb::render::RenderMaterialGraphQualityLevel quality,
    const void* editorInstance) {
    std::error_code canonicalError;
    const std::filesystem::path projectRoot = std::filesystem::weakly_canonical(
        EditorProjectPaths::ProjectRoot(),
        canonicalError);
    std::uint64_t projectHash = HashBytes(
        (canonicalError ? EditorProjectPaths::ProjectRoot() : projectRoot).generic_string());
    if (const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(materialAssetId)) {
        const std::filesystem::path materialRoot = metadata->physicalPath.empty()
            ? metadata->virtualPath.parent_path()
            : metadata->physicalPath.parent_path();
        projectHash = HashCombine(projectHash, HashBytes(materialRoot.generic_string()));
    }
    return std::filesystem::temp_directory_path() /
        "21kb_material_preview" /
        ("project_" + std::to_string(projectHash)) /
        ("process_" + std::to_string(ProcessIdentity())) /
        ("editor_" + std::to_string(reinterpret_cast<std::uintptr_t>(editorInstance))) /
        ("material_" + std::to_string(materialAssetId.value)) /
        ("variant_" + std::to_string(contentHash) + "_q" + std::to_string(static_cast<unsigned>(quality)) + ".kbmat");
}

[[nodiscard]] std::uint64_t TransitiveAssetDependencyHash(
    const kb::scene::Scene& scene,
    std::vector<kb::assets::AssetId> pending) noexcept {
    const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
    std::vector<std::uint64_t> visited;
    while (!pending.empty()) {
        const kb::assets::AssetId id = pending.back();
        pending.pop_back();
        if (!id.IsValid() || std::ranges::find(visited, id.value) != visited.end()) {
            continue;
        }
        visited.push_back(id.value);
        const kb::assets::AssetMetadata* metadata = registry.Find(id);
        if (metadata != nullptr) {
            std::vector<kb::assets::AssetId> dependencies = metadata->dependencies;
            std::ranges::sort(dependencies, {}, &kb::assets::AssetId::value);
            pending.insert(pending.end(), dependencies.rbegin(), dependencies.rend());
        }
    }
    std::ranges::sort(visited);
    std::uint64_t hash = 0xD3A17E5C0FFEE123ULL;
    for (const std::uint64_t assetId : visited) {
        const kb::assets::AssetMetadata* metadata = registry.Find(kb::assets::AssetId{ assetId });
        hash = HashCombine(hash, assetId);
        hash = HashCombine(hash, metadata == nullptr ? 0U : metadata->contentHash);
    }
    return hash;
}

[[nodiscard]] std::uint64_t MaterialDocumentContentHash(const kb::scene::Scene& scene, kb::assets::AssetId materialAssetId) {
    return TransitiveAssetDependencyHash(scene, { materialAssetId });
}

[[nodiscard]] std::uint64_t MaterialPreviewContentHash(
    const kb::scene::Scene& scene,
    kb::assets::AssetId materialAssetId,
    const kb::render::RenderMaterialAssetData* workingCopy) {
    if (workingCopy != nullptr) {
        std::vector<kb::assets::AssetId> dependencies;
        const kb::assets::AssetManager& manager = scene.Assets().Manager();
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(materialAssetId)) {
            dependencies = kb::render::RenderMaterialAssetLoader::DiscoverMaterialDependencies(
                *workingCopy,
                *metadata,
                manager.Registry());
        }
        return HashCombine(HashCombine(
            HashCombine(WorkingCopyContentHash(*workingCopy), MaterialTextureDependencyHash(scene, *workingCopy)),
            TransitiveAssetDependencyHash(scene, std::move(dependencies))),
            0xA11CE21FULL);
    }
    return MaterialDocumentContentHash(scene, materialAssetId);
}

void RegisterPreviewLoaders(kb::assets::AssetManager& manager) {
    static_cast<void>(manager.RegisterLoader(std::make_unique<EditorMaterialPreviewMeshLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialFunctionAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialParameterCollectionAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
}

void CopyAssetRegistry(const kb::assets::AssetManager& source, kb::assets::AssetManager& target) {
    for (const kb::assets::AssetMetadata& metadata : source.Registry().All()) {
        static_cast<void>(target.RegisterAsset(metadata));
    }
}

void RegisterPreviewMesh(kb::assets::AssetManager& manager, const EditorMaterialPreviewPrimitivePolicy& policy) {
    if (policy.kind == EditorMaterialPreviewPrimitiveKind::CustomMesh && policy.meshAssetId.IsValid()) {
        return;
    }
    static_cast<void>(manager.RegisterAsset(kb::assets::AssetMetadata{
        .id = policy.meshAssetId.IsValid() ? policy.meshAssetId : EditorMaterialPreviewPrimitivePolicy::Fallback().meshAssetId,
        .type = "RenderMesh",
        .name = "Material Preview " + std::string{ EditorMaterialPreviewPrimitiveName(policy.kind) },
        .virtualPath = std::filesystem::path{ "/Editor/Preview" } / ("Material" + std::string{ EditorMaterialPreviewPrimitiveName(policy.kind) }),
        .physicalPath = "__editor_material_preview_" + std::string{ EditorMaterialPreviewPrimitiveName(policy.kind) } + "__",
        .runtimeLoadable = true,
    }));
}

void RegisterWorkingCopyMaterial(
    const kb::assets::AssetManager& sourceManager,
    kb::assets::AssetManager& targetManager,
    kb::assets::AssetId materialAssetId,
    const kb::render::RenderMaterialAssetData& workingCopy,
    const std::filesystem::path& path,
    std::uint64_t contentHash) {
    kb::render::RenderMaterialAssetData runtimeWorkingCopy = workingCopy;
    runtimeWorkingCopy.graphSourceAssetId = 0U;
    runtimeWorkingCopy.graphSourceAssetPath.clear();
    runtimeWorkingCopy.graph.storageModel = "inline-kbmat";
    if (!kb::render::RenderMaterialAssetWriter::Save(path, runtimeWorkingCopy)) {
        return;
    }

    kb::assets::AssetMetadata metadata{};
    if (const kb::assets::AssetMetadata* sourceMetadata = sourceManager.Registry().Find(materialAssetId)) {
        metadata = *sourceMetadata;
    }
    metadata.id = materialAssetId;
    metadata.type = "RenderMaterial";
    metadata.physicalPath = path;
    metadata.contentHash = contentHash;
    metadata.runtimeLoadable = true;
    if (metadata.name.empty()) {
        metadata.name = "Material Preview Working Copy";
    }
    if (metadata.virtualPath.empty()) {
        metadata.virtualPath = std::filesystem::path{ "/Editor/Preview" } / ("WorkingMaterial" + std::to_string(materialAssetId.value) + ".kbmat");
    }
    static_cast<void>(targetManager.RegisterAsset(metadata));
}

[[nodiscard]] kb::scene::SceneEntity AddPreviewMesh(kb::scene::Scene& scene, kb::assets::AssetId materialAssetId, bool materialLoaded, const EditorMaterialPreviewPrimitivePolicy& policy) {
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{.name = "Material Preview Mesh"});
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = policy.meshAssetId.value,
        .materialAssetId = materialLoaded ? materialAssetId.value : 0U,
    });
    return mesh;
}

void AddPreviewCamera(kb::scene::Scene& scene, const EditorMaterialPreviewSceneSettings& settings) {
    kb::scene::SceneObjectDesc cameraDesc{.name = "Material Preview Camera"};
    const std::array<float, 3U> eye = settings.CameraEye();
    cameraDesc.transform.localPosition = kb::scene::Vec3{eye[0], eye[1], eye[2]};
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(cameraDesc);
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{
        .verticalFovDegrees = settings.verticalFovDegrees,
        .nearClip = 0.05F,
        .farClip = 50.0F,
        .primary = true,
    });
}

void AddPreviewLighting(kb::scene::Scene& scene) {
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
}

} // namespace

EditorMaterialPreviewScene::~EditorMaterialPreviewScene() {
    Clear();
}

const kb::scene::Scene& EditorMaterialPreviewScene::SceneFor(
    const kb::scene::Scene& sourceScene,
    kb::assets::AssetId materialAssetId,
    const kb::render::RenderMaterialAssetData* workingCopy,
    std::uint64_t previewInputRevision) {
    // Fast path: nothing that feeds the preview has changed since the last call for this same material, so the
    // scene is already up to date. This skips MaterialPreviewContentHash entirely (a full material deep-copy +
    // text serialization + dependency traversal) on the overwhelmingly common steady-state frame. A 0 revision
    // means the caller opted out of the gate (unit tests), so it never takes this path.
    if (previewInputRevision != 0U && previewInputCacheValid_ && scene_ != nullptr &&
        materialAssetId_.value == materialAssetId.value && lastPreviewInputRevision_ == previewInputRevision) {
        return *scene_;
    }

    const std::uint64_t contentHash = MaterialPreviewContentHash(sourceScene, materialAssetId, workingCopy);
    lastPreviewInputRevision_ = previewInputRevision;
    previewInputCacheValid_ = previewInputRevision != 0U;
    if (scene_ == nullptr || materialAssetId_.value != materialAssetId.value || materialContentHash_ != contentHash) {
        Rebuild(sourceScene, materialAssetId, workingCopy, contentHash);
    }
    return *scene_;
}

kb::scene::Scene* EditorMaterialPreviewScene::MutableScene() noexcept {
    return scene_.get();
}

const EditorMaterialPreviewPrimitivePolicy& EditorMaterialPreviewScene::PrimitivePolicy() const noexcept {
    return primitivePolicy_;
}

bool EditorMaterialPreviewScene::SetPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy policy) noexcept {
    if (!policy.meshAssetId.IsValid()) {
        policy = EditorMaterialPreviewPrimitivePolicy::Fallback();
    }
    if (primitivePolicy_.kind == policy.kind &&
        primitivePolicy_.meshAssetId.value == policy.meshAssetId.value &&
        primitivePolicy_.customMeshAssetId.value == policy.customMeshAssetId.value) {
        return false;
    }
    primitivePolicy_ = policy;
    Clear();
    return true;
}

const EditorMaterialPreviewSceneSettings& EditorMaterialPreviewScene::SceneSettings() const noexcept {
    return sceneSettings_;
}

bool EditorMaterialPreviewScene::SetSceneSettings(EditorMaterialPreviewSceneSettings settings) noexcept {
    if (std::tie(sceneSettings_.lightingPreset,
            sceneSettings_.qualityLevel,
            sceneSettings_.cameraDistance,
            sceneSettings_.verticalFovDegrees,
            sceneSettings_.keyLightIntensity,
            sceneSettings_.ambientIntensity,
            sceneSettings_.environmentDiffuseIntensity,
            sceneSettings_.environmentSpecularIntensity,
            sceneSettings_.exposureStops,
            sceneSettings_.postProcessEnabled,
            sceneSettings_.normalDebugView)
        == std::tie(settings.lightingPreset,
            settings.qualityLevel,
            settings.cameraDistance,
            settings.verticalFovDegrees,
            settings.keyLightIntensity,
            settings.ambientIntensity,
            settings.environmentDiffuseIntensity,
            settings.environmentSpecularIntensity,
            settings.exposureStops,
            settings.postProcessEnabled,
            settings.normalDebugView)) {
        return false;
    }
    sceneSettings_ = settings;
    Clear();
    return true;
}

bool EditorMaterialPreviewScene::SetCameraOrbit(float yawDegrees, float pitchDegrees, float cameraDistance) noexcept {
    const float clampedPitch = std::clamp(pitchDegrees, -kEditorMaterialPreviewMaxPitchDegrees, kEditorMaterialPreviewMaxPitchDegrees);
    const float clampedDistance = std::clamp(cameraDistance, kEditorMaterialPreviewMinCameraDistance, kEditorMaterialPreviewMaxCameraDistance);
    if (sceneSettings_.orbitYawDegrees == yawDegrees && sceneSettings_.orbitPitchDegrees == clampedPitch &&
        sceneSettings_.cameraDistance == clampedDistance) {
        return false;
    }
    sceneSettings_.orbitYawDegrees = yawDegrees;
    sceneSettings_.orbitPitchDegrees = clampedPitch;
    sceneSettings_.cameraDistance = clampedDistance;
    // Deliberately does NOT bump revision_: the camera is read fresh as a per-frame present override, so the
    // orbit shows up on the next frame for free. Bumping the revision would make the viewport treat the scene
    // as changed and re-sync every mesh/material to the GPU on every orbit frame - the "cosmic lag". A pure
    // camera move must be as cheap as dragging a node.
    return true;
}

const EditorMaterialPreviewTelemetry& EditorMaterialPreviewScene::Telemetry() const noexcept {
    return telemetry_;
}

std::uint64_t EditorMaterialPreviewScene::Revision() const noexcept {
    return revision_;
}

void EditorMaterialPreviewScene::Clear() noexcept {
    scene_.reset();
    previewMeshEntity_ = {};
    if (!workingCopyPath_.empty()) {
        std::error_code error;
        std::filesystem::remove(workingCopyPath_, error);
        workingCopyPath_.clear();
    }
    telemetry_ = {};
    materialAssetId_ = {};
    materialContentHash_ = 0U;
    ++revision_;
}

void EditorMaterialPreviewScene::Rebuild(
    const kb::scene::Scene& sourceScene,
    kb::assets::AssetId materialAssetId,
    const kb::render::RenderMaterialAssetData* workingCopy,
    std::uint64_t contentHash) {
    // Editing the graph changes the working copy's content hash on almost every edit (connect/create/
    // disconnect), so this runs on nearly every edit. The old code always fell through to the full path
    // below, which replaces scene_ with a BRAND NEW Scene - a new Scene::Id() - which resets every
    // per-scene runtime resource cache keyed by that id (RuntimeTextureResourceMap in particular), forcing
    // every texture the material references to be re-decoded from disk from scratch on every single edit.
    // Measured cost: a 2048x2048 texture decode alone took ~1.1s in Debug - the actual "freeze after every
    // connection". When it's still the SAME material and the scene already exists, only the material's
    // content changed - the mesh/camera/lighting entities and the asset registry are unaffected - so
    // refresh just the material in place and keep the Scene (and its caches) alive.
    if (scene_ != nullptr && materialAssetId_.value == materialAssetId.value) {
        kb::assets::AssetManager& targetManager = scene_->Assets().Manager();
        if (!workingCopyPath_.empty()) {
            std::error_code error;
            std::filesystem::remove(workingCopyPath_, error);
            workingCopyPath_.clear();
        }
        if (workingCopy != nullptr) {
            workingCopyPath_ = WorkingCopyPreviewPath(
                sourceScene,
                materialAssetId,
                contentHash,
                sceneSettings_.qualityLevel,
                this);
            std::error_code directoryError;
            std::filesystem::create_directories(workingCopyPath_.parent_path(), directoryError);
            RegisterWorkingCopyMaterial(sourceScene.Assets().Manager(), targetManager, materialAssetId, *workingCopy, workingCopyPath_, contentHash);
        } else if (const kb::assets::AssetMetadata* sourceMetadata = sourceScene.Assets().Manager().Registry().Find(materialAssetId)) {
            // No working copy (e.g. right after Save clears it): re-sync this one asset's registry entry
            // from the source scene - a reused targetManager otherwise keeps serving the STALE metadata it
            // was copied with at the last full rebuild, so a Save's new contentHash/physicalPath would never
            // be picked up. Force-unload so the resolver re-reads the just-saved content from disk.
            static_cast<void>(targetManager.RegisterAsset(*sourceMetadata));
            static_cast<void>(targetManager.Unload(materialAssetId));
        }

        kb::render::RenderMaterialGraphBuildContext graphContext{};
        graphContext.assetId = materialAssetId.value;
        graphContext.qualityLevel = sceneSettings_.qualityLevel;
        graphContext.variantUsage = kb::render::RenderMaterialGraphVariantUsage::Preview;
        const kb::render::ResolvedRuntimeMaterialAsset resolved =
            kb::render::RuntimeMaterialResolver{ graphContext }.ResolveAsset(targetManager, materialAssetId);
        kb::render::RenderMaterialAssetData telemetryMaterial{};
        if (resolved.resolved) {
            telemetryMaterial.desc = resolved.material.desc;
            if (workingCopy != nullptr) {
                telemetryMaterial.graph = workingCopy->graph;
                telemetryMaterial.materialTypeVersion = workingCopy->materialTypeVersion;
            }
        }

        if (previewMeshEntity_.IsValid()) {
            if (const kb::scene::MeshRendererComponent* existing = scene_->Components().MeshRenderers().TryGet(previewMeshEntity_)) {
                kb::scene::MeshRendererComponent updated = *existing;
                updated.materialAssetId = resolved.resolved ? materialAssetId.value : 0U;
                scene_->Components().MeshRenderers().Set(previewMeshEntity_, updated);
            }
        }

        telemetry_ = EditorMaterialPreviewTelemetryBuilder::Build(
            targetManager,
            materialAssetId,
            resolved.resolved ? &telemetryMaterial : nullptr,
            true,
            graphContext);
        materialContentHash_ = contentHash;
        ++revision_;
        return;
    }

    scene_ = std::make_unique<kb::scene::Scene>(kb::scene::SceneMode::Runtime);
    kb::assets::AssetManager& targetManager = scene_->Assets().Manager();
    RegisterPreviewLoaders(targetManager);
    CopyAssetRegistry(sourceScene.Assets().Manager(), targetManager);
    if (!workingCopyPath_.empty()) {
        std::error_code error;
        std::filesystem::remove(workingCopyPath_, error);
        workingCopyPath_.clear();
    }
    if (workingCopy != nullptr) {
        workingCopyPath_ = WorkingCopyPreviewPath(
            sourceScene,
            materialAssetId,
            contentHash,
            sceneSettings_.qualityLevel,
            this);
        std::error_code directoryError;
        std::filesystem::create_directories(workingCopyPath_.parent_path(), directoryError);
        RegisterWorkingCopyMaterial(sourceScene.Assets().Manager(), targetManager, materialAssetId, *workingCopy, workingCopyPath_, contentHash);
    }
    EditorMaterialPreviewPrimitivePolicy effectivePolicy = primitivePolicy_;
    if (effectivePolicy.kind == EditorMaterialPreviewPrimitiveKind::CustomMesh &&
        targetManager.Registry().Find(effectivePolicy.meshAssetId) == nullptr) {
        effectivePolicy = EditorMaterialPreviewPrimitivePolicy::Fallback();
    }
    RegisterPreviewMesh(targetManager, effectivePolicy);

    kb::render::RenderMaterialGraphBuildContext graphContext{};
    graphContext.assetId = materialAssetId.value;
    graphContext.qualityLevel = sceneSettings_.qualityLevel;
    graphContext.variantUsage = kb::render::RenderMaterialGraphVariantUsage::Preview;
    const kb::render::ResolvedRuntimeMaterialAsset resolved =
        kb::render::RuntimeMaterialResolver{ graphContext }.ResolveAsset(targetManager, materialAssetId);
    kb::render::RenderMaterialAssetData telemetryMaterial{};
    if (resolved.resolved) {
        telemetryMaterial.desc = resolved.material.desc;
        if (workingCopy != nullptr) {
            telemetryMaterial.graph = workingCopy->graph;
            telemetryMaterial.materialTypeVersion = workingCopy->materialTypeVersion;
        }
    }

    previewMeshEntity_ = AddPreviewMesh(*scene_, materialAssetId, resolved.resolved, effectivePolicy);
    AddPreviewCamera(*scene_, sceneSettings_);
    AddPreviewLighting(*scene_);
    scene_->Runtime().SynchronizeTransforms();

    telemetry_ = EditorMaterialPreviewTelemetryBuilder::Build(
        targetManager,
        materialAssetId,
        resolved.resolved ? &telemetryMaterial : nullptr,
        true,
        graphContext);
    materialAssetId_ = materialAssetId;
    materialContentHash_ = contentHash;
    ++revision_;
}

} // namespace kb::editor
