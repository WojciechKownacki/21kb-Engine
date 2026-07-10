#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <typeindex>
#include <vector>

namespace kb::render {

enum class RenderTextureAssetColorSpace : std::uint8_t {
    Unknown,
    Linear,
    Srgb,
};

enum class RenderTextureAssetSemantic : std::uint8_t {
    Unknown,
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
};

struct RenderTextureAssetData {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 1;
    std::uint16_t layers = 1;
    std::uint8_t mipCount = 1;
    RenderTextureDimension dimension = RenderTextureDimension::Texture2D;
    // Authoring/import metadata. Unknown is intentional for legacy assets and decoded image
    // formats whose import settings did not declare a policy; callers must not infer either
    // value from the asset name or path.
    RenderTextureAssetColorSpace colorSpace = RenderTextureAssetColorSpace::Unknown;
    RenderTextureAssetSemantic semantic = RenderTextureAssetSemantic::Unknown;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] RenderTextureDesc MakeDesc(const bgfx::Memory* memory, RenderTextureColorSpace runtimeColorSpace = RenderTextureColorSpace::Linear) const noexcept;
};

class RenderTextureAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    [[nodiscard]] static std::optional<RenderTextureAssetData> LoadTexture(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderTextureAssetData> LoadTexture(std::istream& input);
};

} // namespace kb::render
