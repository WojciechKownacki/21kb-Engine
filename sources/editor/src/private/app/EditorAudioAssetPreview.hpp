#pragma once

#include "engine/assets/AssetId.hpp"
#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAudioAssetPreview {
public:
    EditorAudioAssetPreview() = delete;

    [[nodiscard]] static bool Play(kb::scene::Scene& scene, kb::assets::AssetId assetId);
    [[nodiscard]] static bool PlayOnBus(kb::scene::Scene& scene, kb::assets::AssetId assetId, std::string_view bus);
    static void Stop(kb::scene::Scene& scene) noexcept;
    // Returns true only when the visible playback state changed.
    [[nodiscard]] static bool Tick(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static bool HasActivePreview() noexcept;
    [[nodiscard]] static bool IsPlaying(kb::assets::AssetId assetId) noexcept;
    [[nodiscard]] static bool IsPlaying(const kb::scene::Scene& scene, kb::assets::AssetId assetId) noexcept;
};

} // namespace kb::editor
