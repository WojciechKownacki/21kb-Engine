#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <filesystem>
#include <iosfwd>

namespace kb::render {

class RenderMaterialAssetWriter final {
public:
    RenderMaterialAssetWriter() = delete;

    [[nodiscard]] static bool Save(const std::filesystem::path& path, const RenderMaterialAssetData& asset);
    static void Write(std::ostream& output, const RenderMaterialAssetData& asset);
};

} // namespace kb::render
