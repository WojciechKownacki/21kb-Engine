#pragma once

#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

namespace kb::render {

class RuntimeTextureResourceEnsurer final {
public:
    static void Ensure(
        const RuntimeRenderResourceEnsureContext& context,
        const RuntimeMaterialResourceMap& materials,
        const RuntimeMaterialResourceMap& embeddedMaterials,
        RuntimeTextureResourceMap& textures);
};

} // namespace kb::render
