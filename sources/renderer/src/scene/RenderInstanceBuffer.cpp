#include "kb/render/scene/RenderInstanceBuffer.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {

namespace {

// Deterministic per-instance random in [0,1) derived from the stable entity id (splitmix64 finalizer).
// Distinct entities in the same batch get distinct values; the same entity is stable across frames.
[[nodiscard]] float PerInstanceRandomFromEntity(std::uint64_t entityId) noexcept {
    std::uint64_t x = entityId + 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    const std::uint32_t mantissa = static_cast<std::uint32_t>(x >> 40) & 0xFFFFFFU; // 24 bits
    return static_cast<float>(mantissa) / static_cast<float>(0x1000000U);
}

} // namespace

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

    // The model is affine, so the .w lanes are material-graph data lanes. The vertex shader extracts
    // them and rebuilds the matrix with (0,0,0,1) in the last row before transforming vertices.
    std::array<float, 16> model = instance.model;
    model[3] = PerInstanceRandomFromEntity(instance.entityId);
    model[7] = instance.worldBounds.radius;
    model[11] = instance.fadeAmount;
    model[15] = instance.customData0;

    return RenderInstanceData{
        .model = model,
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
