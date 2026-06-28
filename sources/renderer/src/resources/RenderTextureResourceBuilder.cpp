#include "resources/RenderTextureResourceBuilder.hpp"

namespace kb::render {

bool RenderTextureResourceBuilder::IsValidDesc(const RenderTextureDesc& desc) noexcept {
    return desc.width > 0U && desc.height > 0U && desc.format != bgfx::TextureFormat::Count;
}

RenderTextureResource RenderTextureResourceBuilder::Build(const RenderTextureDesc& desc, bgfx::TextureHandle texture) noexcept {
    return RenderTextureResource{
        .texture = texture,
        .width = desc.width,
        .height = desc.height,
        .format = desc.format,
        .colorSpace = desc.colorSpace,
    };
}

} // namespace kb::render
