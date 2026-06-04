#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::render {

class RenderMeshGltfImporter final {
public:
    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc);
};

} // namespace kb::render
