#pragma once

#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

namespace kb::render {

class RuntimeMeshResourceEnsurer final {
public:
    static void Ensure(
        const RuntimeRenderResourceEnsureContext& context,
        RuntimeMeshResourceMap& meshes,
        RuntimeMaterialResourceMap& embeddedMaterials);
};

} // namespace kb::render
