#pragma once

#include "engine/scene/SceneDocumentService.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace kb::scene {

class SceneAssetReader {
public:
    SceneAssetReader() = delete;

    [[nodiscard]] static SceneDocumentLoadResult Read(const std::filesystem::path& path);
    [[nodiscard]] static SceneDocumentLoadResult Read(std::vector<std::uint8_t> bytes);
};

} // namespace kb::scene
