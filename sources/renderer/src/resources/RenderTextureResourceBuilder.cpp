#include "resources/RenderTextureResourceBuilder.hpp"

namespace kb::render {

bool RenderTextureResourceBuilder::IsValidDesc(const RenderTextureDesc& desc) noexcept {
    if (desc.width == 0U || desc.height == 0U || desc.depth == 0U || desc.layers == 0U || desc.mipCount == 0U ||
        desc.format == bgfx::TextureFormat::Count) {
        return false;
    }
    switch (desc.dimension) {
    case RenderTextureDimension::Texture2D:
        return desc.depth == 1U && desc.layers == 1U;
    case RenderTextureDimension::TextureCube:
        return desc.width == desc.height && desc.depth == 1U && desc.layers == 1U;
    case RenderTextureDimension::Texture3D:
        // bgfx and its backends classify volume resources as 3D only when depth is greater than one.
        return desc.depth > 1U && desc.layers == 1U;
    case RenderTextureDimension::Texture2DArray:
        return desc.depth == 1U && desc.layers > 1U;
    }
    return false;
}

RenderTextureResource RenderTextureResourceBuilder::Build(const RenderTextureDesc& desc, bgfx::TextureHandle texture) noexcept {
    return RenderTextureResource{
        .texture = texture,
        .width = desc.width,
        .height = desc.height,
        .depth = desc.depth,
        .layers = desc.layers,
        .mipCount = desc.mipCount,
        .dimension = desc.dimension,
        .format = desc.format,
        .colorSpace = desc.colorSpace,
    };
}

} // namespace kb::render
