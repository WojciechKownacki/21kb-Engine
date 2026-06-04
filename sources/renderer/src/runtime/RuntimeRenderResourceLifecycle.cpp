#include "runtime/RuntimeRenderResourceLifecycle.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

namespace kb::render {

void RuntimeRenderResourceLifecycle::ReleaseScene(
    kb::scene::Scene& scene,
    SceneRenderer* sceneRenderer,
    RuntimeMeshResourceMap& meshes,
    RuntimeMaterialResourceMap& materials,
    RuntimeMaterialResourceMap& embeddedMaterials,
    RuntimeTextureResourceMap& textures) noexcept {
    if (sceneRenderer == nullptr) {
        return;
    }

    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const std::uint64_t sceneId = scene.Id();
    for (auto it = meshes.begin(); it != meshes.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindMeshHandle(it->second.handle);
        sceneRenderer->Resources().DestroyMesh(it->second.handle);
        static_cast<void>(manager.Unload(kb::assets::AssetId{ it->first.assetId }));
        it = meshes.erase(it);
    }
    for (auto it = materials.begin(); it != materials.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer->Resources().DestroyMaterial(it->second.handle);
        static_cast<void>(manager.Unload(kb::assets::AssetId{ it->first.assetId }));
        it = materials.erase(it);
    }
    for (auto it = embeddedMaterials.begin(); it != embeddedMaterials.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer->Resources().DestroyMaterial(it->second.handle);
        it = embeddedMaterials.erase(it);
    }
    for (auto it = textures.begin(); it != textures.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindTextureHandle(it->second.handle);
        sceneRenderer->Resources().DestroyTexture(it->second.handle);
        static_cast<void>(manager.Unload(kb::assets::AssetId{ it->first.assetId }));
        it = textures.erase(it);
    }
}

void RuntimeRenderResourceLifecycle::DestroyAll(
    SceneRenderer* sceneRenderer,
    RuntimeMeshResourceMap& meshes,
    RuntimeMaterialResourceMap& materials,
    RuntimeMaterialResourceMap& embeddedMaterials,
    RuntimeTextureResourceMap& textures) noexcept {
    if (sceneRenderer == nullptr) {
        meshes.clear();
        materials.clear();
        embeddedMaterials.clear();
        textures.clear();
        return;
    }

    for (const auto& [meshKey, resource] : meshes) {
        static_cast<void>(meshKey);
        sceneRenderer->ResourceMap().UnbindMeshHandle(resource.handle);
        sceneRenderer->Resources().DestroyMesh(resource.handle);
    }
    meshes.clear();

    for (const auto& [materialKey, resource] : materials) {
        static_cast<void>(materialKey);
        sceneRenderer->ResourceMap().UnbindMaterialHandle(resource.handle);
        sceneRenderer->Resources().DestroyMaterial(resource.handle);
    }
    materials.clear();

    for (const auto& [materialKey, resource] : embeddedMaterials) {
        static_cast<void>(materialKey);
        sceneRenderer->ResourceMap().UnbindMaterialHandle(resource.handle);
        sceneRenderer->Resources().DestroyMaterial(resource.handle);
    }
    embeddedMaterials.clear();

    for (const auto& [textureKey, resource] : textures) {
        static_cast<void>(textureKey);
        sceneRenderer->ResourceMap().UnbindTextureHandle(resource.handle);
        sceneRenderer->Resources().DestroyTexture(resource.handle);
    }
    textures.clear();
}

} // namespace kb::render
