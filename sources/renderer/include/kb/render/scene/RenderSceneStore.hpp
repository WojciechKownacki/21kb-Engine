#pragma once

#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace kb::render {

struct RenderSceneStoreStats {
    std::uint32_t sceneCount = 0;
    std::uint32_t sceneCapacity = 0;
    RenderSceneStats renderSceneStats{};
};

class RenderSceneStore {
public:
    [[nodiscard]] RenderScene& ForScene(std::uint64_t sceneId, const RenderSceneReserveDesc& reserveDesc);

    void ReserveSceneCount(std::uint32_t sceneCount);
    void ApplyRenderSceneReserve(const RenderSceneReserveDesc& reserveDesc);
    void Release(std::uint64_t sceneId) noexcept;
    void ReleaseAll() noexcept;

    [[nodiscard]] RenderSceneStoreStats Stats() const noexcept;

private:
    std::unordered_map<std::uint64_t, std::unique_ptr<RenderScene>> scenes_;
};

} // namespace kb::render
