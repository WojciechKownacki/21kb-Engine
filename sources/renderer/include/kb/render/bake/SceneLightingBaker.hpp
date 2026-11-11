#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace kb::scene { class Scene; }

namespace kb::render {

struct SceneLightingBakeSettings {
    std::uint16_t resolution = 128;
    std::uint8_t bounces = 1;
};

struct SceneLightingBakeResult {
    std::string error;
    bool cancelled = false;
    std::uint32_t receivers = 0, lights = 0, generatedMeshes = 0, skippedLights = 0;
    std::uint64_t texels = 0;
    [[nodiscard]] bool Succeeded() const noexcept { return error.empty() && !cancelled; }
};

// Called between receivers with the finished share (0..1) and a short stage name; returning false cancels.
using SceneLightingBakeProgress = std::function<bool(float fraction, std::string_view stage)>;

// Bakes every active Static mesh renderer of the scene against its Static point/directional lights:
// one lightmap texture + material per receiver under /Game/Baked/<sceneName>, and a generated
// lightmap UV mesh (second UV set) for receivers without a usable one. The scene's mesh and material
// references are switched to the baked assets; the caller saves the scene. Nothing is written when the
// bake fails or is cancelled. Re-baking replaces the previous bake of the same scene.
[[nodiscard]] SceneLightingBakeResult BakeSceneLighting(kb::scene::Scene& scene,
    std::string_view sceneName, const SceneLightingBakeSettings& settings = {}, const SceneLightingBakeProgress& progress = {});

} // namespace kb::render
