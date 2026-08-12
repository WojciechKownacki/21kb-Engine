#pragma once

#include "engine/assets/AssetId.hpp"

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAudioAssetPreview {
public:
    EditorAudioAssetPreview() = delete;

    [[nodiscard]] static bool Play(kb::scene::Scene& scene, kb::assets::AssetId assetId);
    // Returns true only when the visible playback state changed.
    [[nodiscard]] static bool Tick(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static bool HasActivePreview() noexcept;
    [[nodiscard]] static bool IsPlaying(kb::assets::AssetId assetId) noexcept;
};

} // namespace kb::editor
