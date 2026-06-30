#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

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
