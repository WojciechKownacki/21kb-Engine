#pragma once

#include "engine/scene/SceneDocument.hpp"

#include <filesystem>

namespace kb::scene {

class SceneAssetWriter {
public:
    SceneAssetWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, const SceneDocument& scene);
};

} // namespace kb::scene
