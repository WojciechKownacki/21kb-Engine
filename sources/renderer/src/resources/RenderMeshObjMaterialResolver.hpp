#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstdint>
#include <string_view>

namespace kb::render {

class RenderMeshObjMaterialResolver final {
public:
    [[nodiscard]] static std::uint32_t EnsureMaterialSlot(
        RenderMeshAssetData& asset,
        std::string_view materialName,
        const RenderMeshObjImportDesc& desc);
};

} // namespace kb::render
