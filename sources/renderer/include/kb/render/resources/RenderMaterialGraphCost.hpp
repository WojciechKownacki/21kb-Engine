#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::render {

// What a material graph costs per pixel, estimated from the nodes that reach the Material Output (a node nothing
// uses costs nothing). The numbers are relative, for deciding whether a material suits a phone, not a profile.
struct RenderMaterialGraphCost {
    std::uint32_t nodes = 0U;
    std::uint32_t instructions = 0U;
    std::uint32_t textureSamples = 0U;
    std::uint32_t sceneReads = 0U;  // reads of the scene colour or depth behind the surface
    // Why the material is too heavy for a phone (older mobile GPUs, tablets), one reason each; empty when it is not.
    std::vector<std::string> mobileWarnings;
    // Things worth knowing that do not make the material too heavy: overdraw of blending, vertex offsets that the culling does not know about.
    std::vector<std::string> notes;
};

inline constexpr std::uint32_t kRenderMaterialMobileInstructionBudget = 120U;
inline constexpr std::uint32_t kRenderMaterialMobileTextureSampleBudget = 6U;

[[nodiscard]] RenderMaterialGraphCost EstimateRenderMaterialGraphCost(const RenderMaterialGraphDocument& graph);

} // namespace kb::render
