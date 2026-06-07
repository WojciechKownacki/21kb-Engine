#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>

namespace kb::render {

class RenderMeshFbxImporter {
public:
    RenderMeshFbxImporter() = delete;

    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(std::span<const std::byte> data);
};

} // namespace kb::render
