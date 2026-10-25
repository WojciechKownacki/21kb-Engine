#pragma once

#include "engine/assets/IAssetLoader.hpp"

namespace kb::audio {

class AudioClipAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    // Reads and decodes one clip from its own file or pack entry; nothing is shared between loads.
    [[nodiscard]] bool AllowsConcurrentLoad() const noexcept override {
        return true;
    }
};

} // namespace kb::audio
