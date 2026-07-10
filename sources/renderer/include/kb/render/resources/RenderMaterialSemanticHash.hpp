#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>

namespace kb::render {

[[nodiscard]] std::uint64_t RenderMaterialRuntimeSemanticHash(const RenderMaterialAssetData& material);
[[nodiscard]] std::uint64_t RenderMaterialShaderSemanticHash(const RenderMaterialAssetData& material);

} // namespace kb::render
