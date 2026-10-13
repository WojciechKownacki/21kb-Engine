#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

// The alpha of the HDR scene colour is the share of the opaque surface still visible through the
// translucency drawn over it. Opaque surfaces (and the clear) write 1; a translucent layer that hides the
// surface behind it multiplies the share by 1 - its opacity (the source alpha); a layer that only tints
// the surface behind it keeps the share. The TAA resolve trusts a pixel's reprojected history only by
// this share, so glass, water and particles do not ghost (FSR2's auto-reactive mask, Unreal's
// Responsive AA). The colour factors stay whatever the blend mode needs.
class SceneColorAlphaPolicy {
public:
    SceneColorAlphaPolicy() = delete;

    // dst.a *= 1 - src.a.
    [[nodiscard]] static constexpr std::uint64_t HidingBlend(std::uint64_t source, std::uint64_t destination) noexcept {
        return BGFX_STATE_BLEND_FUNC_SEPARATE(source, destination, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }

    // dst.a unchanged.
    [[nodiscard]] static constexpr std::uint64_t KeepingBlend(std::uint64_t source, std::uint64_t destination) noexcept {
        return BGFX_STATE_BLEND_FUNC_SEPARATE(source, destination, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
    }
};

} // namespace kb::render
