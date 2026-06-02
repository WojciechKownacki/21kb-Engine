#pragma once

#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace kb::render {

struct RenderInstanceData {
    std::array<float, 16> model{};
    std::array<float, 4> color{ 1.0F, 1.0F, 1.0F, 1.0F };
};

class RenderInstanceBuffer {
public:
    [[nodiscard]] static constexpr std::uint16_t Stride() noexcept {
        return static_cast<std::uint16_t>(sizeof(RenderInstanceData));
    }

    [[nodiscard]] static RenderInstanceData Pack(const SceneRenderMeshInstance& instance, const RenderMaterialResource* material = nullptr, bool encodeShadowReceiver = true) noexcept;
    static void Copy(std::span<RenderInstanceData> destination, std::span<const SceneRenderMeshInstance> source, const RenderMaterialResource* material = nullptr, bool encodeShadowReceiver = true) noexcept;
};

} // namespace kb::render
