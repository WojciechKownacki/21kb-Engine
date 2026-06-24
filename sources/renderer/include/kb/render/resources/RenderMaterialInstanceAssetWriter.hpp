#pragma once

#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include <filesystem>
#include <iosfwd>

namespace kb::render {

class RenderMaterialInstanceAssetWriter final {
public:
    RenderMaterialInstanceAssetWriter() = delete;

    [[nodiscard]] static bool Save(const std::filesystem::path& path, const RenderMaterialInstanceAssetData& asset);
    static void Write(std::ostream& output, const RenderMaterialInstanceAssetData& asset);
};

} // namespace kb::render
