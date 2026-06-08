#pragma once

#include "engine/scene/SceneAssetMeta.hpp"

#include <filesystem>

namespace kb::scene {

class SceneAssetIntegrityService {
public:
    SceneAssetIntegrityService() = delete;

    [[nodiscard]] static SceneAssetIntegrity ComputeFile(const std::filesystem::path& path) noexcept;
};

} // namespace kb::scene
