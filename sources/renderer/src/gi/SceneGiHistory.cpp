#include "kb/render/gi/SceneGiHistory.hpp"

namespace kb::render {

SceneGiHistory::~SceneGiHistory() {
    Shutdown();
}

bool SceneGiHistory::Ensure(RenderExtent extent, bgfx::TextureFormat::Enum format) {
    if (bgfx::isValid(texture_) && extent_.width == extent.width && extent_.height == extent.height && format_ == format) {
        return true;
    }
    Shutdown();
    if (extent.width == 0U || extent.height == 0U || format == bgfx::TextureFormat::Count) {
        return false;
    }
    texture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(extent.width),
        static_cast<std::uint16_t>(extent.height),
        false,
        1U,
        format,
        BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(texture_)) {
        return false;
    }
    bgfx::setName(texture_, "KB GI History");
    extent_ = extent;
    format_ = format;
    return true;
}

void SceneGiHistory::Shutdown() noexcept {
    if (bgfx::isValid(texture_)) {
        bgfx::destroy(texture_);
    }
    texture_ = BGFX_INVALID_HANDLE;
    extent_ = {};
    format_ = bgfx::TextureFormat::Count;
    captured_ = false;
    frameIndex_ = 0U;
}

SceneGiBinding SceneGiHistory::Binding() const noexcept {
    return SceneGiBinding{
        .history = texture_,
        .previousViewProjection = previousViewProjection_,
        .frameIndex = frameIndex_,
        .active = captured_ && bgfx::isValid(texture_),
    };
}

void SceneGiHistory::Capture(bgfx::ViewId viewId, bgfx::TextureHandle sourceColor, const std::array<float, 16>& viewProjection) noexcept {
    if (!bgfx::isValid(texture_) || !bgfx::isValid(sourceColor)) {
        return;
    }
    bgfx::blit(viewId, texture_, 0U, 0U, sourceColor);
    previousViewProjection_ = viewProjection;
    captured_ = true;
    ++frameIndex_;
}

} // namespace kb::render
