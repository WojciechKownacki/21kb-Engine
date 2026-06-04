#pragma once

#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

namespace kb::scene {
class Scene;
}

namespace kb::render {

class RuntimeRenderResourceLifecycle final {
public:
    static void ReleaseScene(
        kb::scene::Scene& scene,
        SceneRenderer* sceneRenderer,
        RuntimeMeshResourceMap& meshes,
        RuntimeMaterialResourceMap& materials,
        RuntimeMaterialResourceMap& embeddedMaterials,
        RuntimeTextureResourceMap& textures) noexcept;

    static void DestroyAll(
        SceneRenderer* sceneRenderer,
        RuntimeMeshResourceMap& meshes,
        RuntimeMaterialResourceMap& materials,
        RuntimeMaterialResourceMap& embeddedMaterials,
        RuntimeTextureResourceMap& textures) noexcept;
};

} // namespace kb::render
