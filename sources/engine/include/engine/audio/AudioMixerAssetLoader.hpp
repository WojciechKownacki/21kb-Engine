#pragma once

#include "engine/assets/IAssetLoader.hpp"

namespace kb::audio {

// LIB-147: registers the authored `.kbmixer` AudioMixer asset (type "AudioMixer", payload
// AudioMixerAsset) - see AudioMixerAssetIO.hpp for the text format and the validation
// contract (a structurally broken mixer fails to load, it never reaches the backend).
class AudioMixerAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};

} // namespace kb::audio
