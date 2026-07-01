#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

// Converts a graph sampler state into bgfx sampler flags (filter + wrap). Linear/Repeat resolves to 0
// (bgfx defaults). Returned value is OR-able into bgfx::setTexture's flags argument.
[[nodiscard]] std::uint32_t RenderMaterialGraphSamplerBgfxFlags(const RenderMaterialGraphSamplerState& state) noexcept;

struct RenderMaterialGraphProgramBindingResult {
    RenderMaterialGraphProgramBinding binding;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
};

[[nodiscard]] RenderMaterialGraphProgramBindingResult BuildRenderMaterialGraphProgramBinding(
    std::uint64_t materialTypeId,
    std::uint32_t materialTypeVersion,
    const RenderMaterialGraphShaderSource& shader,
    std::span<const RenderMaterialGraphParameterValue> parameterValues);

} // namespace kb::render
