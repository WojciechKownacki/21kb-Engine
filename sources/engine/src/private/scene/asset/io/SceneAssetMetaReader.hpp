#pragma once

#include "engine/scene/SceneAssetMeta.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

struct SceneAssetMetaReadResult {
    bool succeeded = false;
    SceneAssetMeta meta{};
    std::string error;
};

class SceneAssetMetaReader {
public:
    SceneAssetMetaReader() = delete;

    [[nodiscard]] static SceneAssetMetaReadResult Read(const std::filesystem::path& path);
};

} // namespace kb::scene
