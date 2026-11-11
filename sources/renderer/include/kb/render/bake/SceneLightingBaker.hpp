#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace kb::scene { class Scene; }

namespace kb::render {

struct RenderMaterialAssetData;

struct SceneLightingBakeSettings {
    std::uint16_t resolution = 128;
    std::uint8_t bounces = 1;
    // Edge length (m) of the light probe grid over the static geometry; 0 bakes no probes. The grid is
    // coarsened when it would exceed maxProbes.
    float probeSpacing = 2.0F;
    std::uint32_t maxProbes = 4096;
};

struct SceneLightingBakeResult {
    std::string error;
    bool cancelled = false;
    std::uint32_t receivers = 0, lights = 0, generatedMeshes = 0, probes = 0;
    std::uint64_t texels = 0;
    [[nodiscard]] bool Succeeded() const noexcept { return error.empty() && !cancelled; }
};

// Called between receivers with the finished share (0..1) and a short stage name; returning false cancels.
using SceneLightingBakeProgress = std::function<bool(float fraction, std::string_view stage)>;

// Whether a material can receive a baked lightmap: opaque matte builtin PBR without emission, normal, occlusion
// or layered lobes. Its graph may only be the default one (a lone output node, as plain colour materials carry).
[[nodiscard]] bool IsBakeReceiverMaterial(const RenderMaterialAssetData& material) noexcept;

// Bakes every active Static mesh renderer of the scene against its Static point/spot/directional lights:
// one lightmap texture + material per receiver under /Game/Baked/<sceneName>, and a generated
// lightmap UV mesh (second UV set) for receivers without a usable one. A grid of L2 spherical-harmonics
// light probes over the static geometry is written beside them (LightProbes.kblp). The scene mesh and
// material references are switched to the baked assets; the caller saves the scene. Nothing is written when
// the bake fails or is cancelled. Re-baking replaces the previous bake of the same scene and starts from the
// original meshes of the receivers (BakeSources.kbbake remembers them), so edits of those meshes are picked up.
[[nodiscard]] SceneLightingBakeResult BakeSceneLighting(kb::scene::Scene& scene,
    std::string_view sceneName, const SceneLightingBakeSettings& settings = {}, const SceneLightingBakeProgress& progress = {});

} // namespace kb::render
