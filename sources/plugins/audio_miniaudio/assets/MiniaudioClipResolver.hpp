#pragma once

#include <cstdint>
#include <filesystem>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioClipResolver final {
public:
    [[nodiscard]] std::filesystem::path Resolve(kb::scene::Scene& scene, std::uint64_t clipAssetId) const;
};

} // namespace kb::audio_miniaudio
