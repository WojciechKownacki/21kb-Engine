#pragma once

#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>

namespace kb::render {

struct DirectionalShadowLightSelection {
    const LightRenderProxyDesc* light = nullptr;
    std::uint64_t entityId = 0;
};

class DirectionalShadowLightSelector {
public:
    DirectionalShadowLightSelector() = delete;

    [[nodiscard]] static DirectionalShadowLightSelection Select(const RenderScene& renderScene) noexcept;
};

} // namespace kb::render
