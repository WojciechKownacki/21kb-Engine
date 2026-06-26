#include "runtime/RuntimeRenderResourcePruner.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

namespace kb::render {
namespace {

void UnloadFromSubmittedScene(std::span<const kb::scene::Scene*> submittedScenes, RuntimeAssetKey key) {
    for (const kb::scene::Scene* scene : submittedScenes) {
        if (scene != nullptr && scene->Id() == key.sceneId) {
            static_cast<void>(const_cast<kb::scene::Scene*>(scene)->Assets().Manager().Unload(kb::assets::AssetId{ key.assetId }));
            break;
        }
    }
}

void UnloadTextureFromSubmittedScene(std::span<const kb::scene::Scene*> submittedScenes, RuntimeTextureAssetKey key) {
    for (const kb::scene::Scene* scene : submittedScenes) {
        if (scene != nullptr && scene->Id() == key.sceneId) {
            static_cast<void>(const_cast<kb::scene::Scene*>(scene)->Assets().Manager().Unload(kb::assets::AssetId{ key.assetId }));
            break;
        }
    }
}

[[nodiscard]] bool ShouldRetain(
    const RuntimeFrameResourceReferences& frameReferences,
    RuntimeAssetKey key,
    std::uint64_t lastReferencedFrame,
    std::uint64_t currentFrame,
    std::uint64_t retentionFrames,
    bool material) noexcept {
    return (material ? frameReferences.ContainsMaterial(key) : frameReferences.ContainsMesh(key)) ||
           currentFrame <= lastReferencedFrame + retentionFrames;
}

} // namespace

void RuntimeRenderResourcePruner::PruneUnused(
    std::span<const kb::scene::Scene*> submittedScenes,
    const RuntimeFrameResourceReferences& frameReferences,
    SceneRenderer& sceneRenderer,
    std::uint64_t currentFrame,
    std::uint64_t retentionFrames,
    RuntimeMeshResourceMap& meshes,
    RuntimeMaterialResourceMap& materials,
    RuntimeMaterialResourceMap& embeddedMaterials,
    RuntimeTextureResourceMap& textures) {
    for (auto it = meshes.begin(); it != meshes.end();) {
        if (ShouldRetain(frameReferences, it->first, it->second.lastReferencedFrame, currentFrame, retentionFrames, false)) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindMeshHandle(it->second.handle);
        sceneRenderer.Resources().DestroyMesh(it->second.handle);
        UnloadFromSubmittedScene(submittedScenes, it->first);
        it = meshes.erase(it);
    }

    for (auto it = materials.begin(); it != materials.end();) {
        if (ShouldRetain(frameReferences, it->first, it->second.lastReferencedFrame, currentFrame, retentionFrames, true)) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer.Resources().DestroyMaterial(it->second.handle);
        UnloadFromSubmittedScene(submittedScenes, it->first);
        it = materials.erase(it);
    }

    for (auto it = embeddedMaterials.begin(); it != embeddedMaterials.end();) {
        if (ShouldRetain(frameReferences, it->first, it->second.lastReferencedFrame, currentFrame, retentionFrames, true)) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer.Resources().DestroyMaterial(it->second.handle);
        it = embeddedMaterials.erase(it);
    }

    for (auto it = textures.begin(); it != textures.end();) {
        if (frameReferences.ContainsTexture(it->first) ||
            currentFrame <= it->second.lastReferencedFrame + retentionFrames) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindTextureHandle(it->second.handle);
        sceneRenderer.Resources().DestroyTexture(it->second.handle);
        UnloadTextureFromSubmittedScene(submittedScenes, it->first);
        it = textures.erase(it);
    }
}

} // namespace kb::render
