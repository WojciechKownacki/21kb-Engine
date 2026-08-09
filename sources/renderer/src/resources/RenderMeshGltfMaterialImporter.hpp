#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cgltf/cgltf.h>

#include <cstdint>
#include <string_view>

namespace kb::render {

class RenderMeshGltfMaterialImporter final {
public:
    [[nodiscard]] static RenderMeshEmbeddedMaterial BuildEmbeddedMaterial(
        std::string_view materialName,
        const cgltf_material* material);

    [[nodiscard]] static std::uint32_t EnsureMaterialSlot(
        RenderMeshAssetData& asset,
        std::string_view materialName,
        const cgltf_material* material,
        const RenderMeshGltfImportDesc& desc);
};

} // namespace kb::render
