#pragma once

#include "engine/scene/SceneDocument.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace kb::scene {

class SceneAssetWriter {
public:
    SceneAssetWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, const SceneDocument& scene);
    // The scene file bytes Write would store, without the .meta sidecar; empty when the
    // document cannot be written.
    [[nodiscard]] static std::vector<std::uint8_t> Encode(const SceneDocument& scene);
};

} // namespace kb::scene
