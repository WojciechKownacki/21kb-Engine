#pragma once

#include "engine/scene/SceneDocumentService.hpp"

#include <filesystem>

namespace kb::scene {

class SceneAssetReader {
public:
    SceneAssetReader() = delete;

    [[nodiscard]] static SceneDocumentLoadResult Read(const std::filesystem::path& path);
};

} // namespace kb::scene
