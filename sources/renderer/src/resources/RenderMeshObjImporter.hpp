#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <iosfwd>

namespace kb::render {

class RenderMeshObjImporter final {
public:
    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(const std::filesystem::path& path, const RenderMeshObjImportDesc& desc);
    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(std::istream& input, const RenderMeshObjImportDesc& desc);
};

} // namespace kb::render
