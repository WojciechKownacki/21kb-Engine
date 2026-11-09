#pragma once
#include "engine/assets/IAssetLoader.hpp"
namespace kb::scene {
// Immutable, owner-thread-published display-encoded RGBA8 frame.
// Runtime only; author the source video.
struct DynamicImageAsset {
    std::uint32_t width=0,height=0;
    std::uint64_t revision=0;
    std::vector<std::uint8_t> rgba;
};
class DynamicImageAssetLoader final : public kb::assets::IAssetLoader {
public:
    std::string_view Type() const noexcept override {return "DynamicImage";}
    std::type_index PayloadType() const noexcept override {return typeid(DynamicImageAsset);}
    std::vector<std::string> Extensions() const override {return {};}
    kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest&) override {return {.error="dynamic images must be published by their runtime producer"};}
};
}
