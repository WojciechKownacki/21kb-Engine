#pragma once

#include "kb/render/runtime/RuntimeFrameResourceReferences.hpp"
#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

#include <cstdint>
#include <span>

namespace kb::scene {
class Scene;
}

namespace kb::render {

class RuntimeRenderResourcePruner final {
public:
    static void PruneUnused(
        std::span<const kb::scene::Scene*> submittedScenes,
        const RuntimeFrameResourceReferences& frameReferences,
        SceneRenderer& sceneRenderer,
        std::uint64_t currentFrame,
        std::uint64_t retentionFrames,
        RuntimeMeshResourceMap& meshes,
        RuntimeMaterialResourceMap& materials,
        RuntimeMaterialResourceMap& embeddedMaterials,
        RuntimeTextureResourceMap& textures);
};

} // namespace kb::render
