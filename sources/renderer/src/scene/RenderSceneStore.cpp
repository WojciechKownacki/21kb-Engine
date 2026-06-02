#include "kb/render/scene/RenderSceneStore.hpp"

namespace kb::render {

RenderScene& RenderSceneStore::ForScene(std::uint64_t sceneId, const RenderSceneReserveDesc& reserveDesc) {
    auto [it, inserted] = scenes_.try_emplace(sceneId);
    if (inserted) {
        it->second = std::make_unique<RenderScene>();
        it->second->Reserve(reserveDesc);
    }
    return *it->second;
}

void RenderSceneStore::ReserveSceneCount(std::uint32_t sceneCount) {
    if (sceneCount > 0U) {
        scenes_.reserve(sceneCount);
    }
}

void RenderSceneStore::ApplyRenderSceneReserve(const RenderSceneReserveDesc& reserveDesc) {
    for (auto& [sceneId, renderScene] : scenes_) {
        static_cast<void>(sceneId);
        if (renderScene != nullptr) {
            renderScene->Reserve(reserveDesc);
        }
    }
}

void RenderSceneStore::Release(std::uint64_t sceneId) noexcept {
    scenes_.erase(sceneId);
}

void RenderSceneStore::ReleaseAll() noexcept {
    scenes_.clear();
}

RenderSceneStoreStats RenderSceneStore::Stats() const noexcept {
    RenderSceneStats aggregate{};
    for (const auto& [sceneId, renderScene] : scenes_) {
        static_cast<void>(sceneId);
        if (renderScene == nullptr) {
            continue;
        }
        const RenderSceneStats sceneStats = renderScene->Stats();
        aggregate.meshProxyCount += sceneStats.meshProxyCount;
        aggregate.cameraProxyCount += sceneStats.cameraProxyCount;
        aggregate.lightProxyCount += sceneStats.lightProxyCount;
        aggregate.meshProxyCapacity += sceneStats.meshProxyCapacity;
        aggregate.cameraProxyCapacity += sceneStats.cameraProxyCapacity;
        aggregate.lightProxyCapacity += sceneStats.lightProxyCapacity;
        aggregate.drawGroupLookupCapacity += sceneStats.drawGroupLookupCapacity;
    }

    return RenderSceneStoreStats{
        .sceneCount = static_cast<std::uint32_t>(scenes_.size()),
        .sceneCapacity = static_cast<std::uint32_t>(scenes_.bucket_count()),
        .renderSceneStats = aggregate,
    };
}

} // namespace kb::render
