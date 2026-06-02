#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace kb::scene {
class Scene;
}

namespace kb::render {

struct RuntimeRenderAssetDiscoveryStats {
    std::uint32_t registeredSceneCount = 0;
    std::uint32_t discoverySceneCount = 0;
    std::uint32_t discoverySceneCapacity = 0;
};

class RuntimeRenderAssetDiscovery {
public:
    void Ensure(kb::scene::Scene& scene, std::uint64_t currentFrame);
    void ReserveSceneCount(std::uint32_t sceneCount);
    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void Clear() noexcept;

    void SetDiscoveryIntervalFrames(std::uint64_t frameInterval) noexcept;
    [[nodiscard]] std::uint64_t DiscoveryIntervalFrames() const noexcept;
    [[nodiscard]] RuntimeRenderAssetDiscoveryStats Stats() const noexcept;

private:
    void Refresh(kb::scene::Scene& scene, std::uint64_t currentFrame);

    std::unordered_set<std::uint64_t> registeredScenes_;
    std::unordered_map<std::uint64_t, std::uint64_t> lastDiscoveryFrames_;
    std::uint64_t discoveryIntervalFrames_ = 30ULL;
};

} // namespace kb::render
