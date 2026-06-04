#pragma once

#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

namespace kb::render {

class RuntimeMaterialResourceEnsurer final {
public:
    static void Ensure(
        const RuntimeRenderResourceEnsureContext& context,
        RuntimeMaterialResourceMap& materials,
        RuntimeMaterialResourceMap& embeddedMaterials);
};

} // namespace kb::render
