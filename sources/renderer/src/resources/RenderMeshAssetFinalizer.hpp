#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::render {

class RenderMeshAssetFinalizer final {
public:
    static void EnsureTangentVertexStorage(RenderMeshAssetData& asset);
    [[nodiscard]] static bool Finalize(
        RenderMeshAssetData& asset,
        const RenderMeshFinalizeOptions& options = {});
};

} // namespace kb::render
