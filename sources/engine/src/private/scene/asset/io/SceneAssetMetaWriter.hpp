#pragma once

#include "engine/scene/SceneAssetMeta.hpp"

#include <filesystem>

namespace kb::scene {

class SceneAssetMetaWriter {
public:
    SceneAssetMetaWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, const SceneAssetMeta& meta);
};

} // namespace kb::scene
