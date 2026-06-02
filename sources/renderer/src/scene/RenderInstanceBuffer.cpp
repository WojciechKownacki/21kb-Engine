#include "kb/render/scene/RenderInstanceBuffer.hpp"

#include <algorithm>

namespace kb::render {

RenderInstanceData RenderInstanceBuffer::Pack(const SceneRenderMeshInstance& instance, const RenderMaterialResource* material, bool encodeShadowReceiver) noexcept {
    std::array<float, 4> color = instance.color;
    if (material != nullptr) {
        color[0] *= material->baseColor[0];
        color[1] *= material->baseColor[1];
        color[2] *= material->baseColor[2];
        color[3] *= material->baseColor[3];
    }
    if (encodeShadowReceiver && !instance.receivesShadow) {
        color[3] = -std::max(color[3], 0.000001F);
    }

    return RenderInstanceData{
        .model = instance.model,
        .color = color,
    };
}

void RenderInstanceBuffer::Copy(std::span<RenderInstanceData> destination, std::span<const SceneRenderMeshInstance> source, const RenderMaterialResource* material, bool encodeShadowReceiver) noexcept {
    const std::size_t count = std::min(destination.size(), source.size());
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = Pack(source[index], material, encodeShadowReceiver);
    }
}

} // namespace kb::render
