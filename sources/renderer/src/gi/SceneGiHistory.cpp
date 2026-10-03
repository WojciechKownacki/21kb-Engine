#include "kb/render/gi/SceneGiHistory.hpp"

namespace kb::render {

SceneGiHistory::~SceneGiHistory() {
    Shutdown();
}

bool SceneGiHistory::Ensure(RenderExtent extent, bgfx::TextureFormat::Enum format) {
    if (bgfx::isValid(lit_) && extent_.width == extent.width && extent_.height == extent.height && format_ == format) {
        return true;
    }
    Shutdown();
    if (extent.width == 0U || extent.height == 0U || format == bgfx::TextureFormat::Count) {
        return false;
    }
    const auto width = static_cast<std::uint16_t>(extent.width);
    const auto height = static_cast<std::uint16_t>(extent.height);
    lit_ = bgfx::createTexture2D(width, height, false, 1U, format, BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bool created = bgfx::isValid(lit_);
    for (std::size_t index = 0U; created && index < accum_.size(); ++index) {
        accum_[index] = bgfx::createTexture2D(
            width, height, false, 1U, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        created = bgfx::isValid(accum_[index]);
        if (created) {
            frameBuffers_[index] = bgfx::createFrameBuffer(1U, &accum_[index], false);
            created = bgfx::isValid(frameBuffers_[index]);
        }
    }
    if (!created) {
        Shutdown();
        return false;
    }
    bgfx::setName(lit_, "KB GI Lit Colour");
    bgfx::setName(accum_[0], "KB GI Accum 0");
    bgfx::setName(accum_[1], "KB GI Accum 1");
    extent_ = extent;
    format_ = format;
    return true;
}

void SceneGiHistory::Shutdown() noexcept {
    for (std::size_t index = 0U; index < accum_.size(); ++index) {
        if (bgfx::isValid(frameBuffers_[index])) {
            bgfx::destroy(frameBuffers_[index]);
        }
        if (bgfx::isValid(accum_[index])) {
            bgfx::destroy(accum_[index]);
        }
        frameBuffers_[index] = BGFX_INVALID_HANDLE;
        accum_[index] = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lit_)) {
        bgfx::destroy(lit_);
    }
    lit_ = BGFX_INVALID_HANDLE;
    extent_ = {};
    format_ = bgfx::TextureFormat::Count;
    frameIndex_ = 0U;
    readIndex_ = 0U;
    captured_ = false;
    hasAccum_ = false;
}

SceneGiBinding SceneGiHistory::Binding() const noexcept {
    return SceneGiBinding{
        .accum = accum_[readIndex_],
        .accumViewProjection = accumViewProjection_,
        .frameIndex = frameIndex_,
        .active = hasAccum_ && bgfx::isValid(accum_[readIndex_]),
    };
}

void SceneGiHistory::Capture(bgfx::ViewId viewId, bgfx::TextureHandle sourceColor) noexcept {
    if (!bgfx::isValid(lit_) || !bgfx::isValid(sourceColor)) {
        return;
    }
    bgfx::blit(viewId, lit_, 0U, 0U, sourceColor);
    captured_ = true;
}

void SceneGiHistory::CommitResolve(const std::array<float, 16>& viewProjection) noexcept {
    accumViewProjection_ = viewProjection;
    readIndex_ = 1U - readIndex_;
    hasAccum_ = true;
    ++frameIndex_;
}

} // namespace kb::render
