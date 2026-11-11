#include "kb/render/scene/SceneLightProbeTexture.hpp"

#include <bx/math.h>

#include <cstdint>
#include <utility>

namespace kb::render {

SceneLightProbeTexture::~SceneLightProbeTexture() {
    Shutdown();
}

bool SceneLightProbeTexture::Supported() noexcept {
    const bgfx::Caps* caps = bgfx::getCaps();
    return caps != nullptr && (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_3D) != 0U;
}

void SceneLightProbeTexture::Shutdown() noexcept {
    if (bgfx::isValid(texture_)) {
        bgfx::destroy(texture_);
        texture_ = BGFX_INVALID_HANDLE;
    }
    grid_.reset();
}

void SceneLightProbeTexture::Update(std::shared_ptr<const LightProbeGrid> grid) {
    if (grid == grid_) {
        return;
    }
    Shutdown();
    grid_ = std::move(grid);
    if (grid_ == nullptr || !grid_->Succeeded() || !Supported()) {
        return;
    }
    const auto [width, height, depth] = grid_->dimensions;
    // 2048 is the smallest 3D texture limit of the desktop backends.
    if (width > 2048U || height > 2048U || depth * kSlots > 2048U) {
        return;
    }
    const std::size_t probes = static_cast<std::size_t>(width) * height * depth;
    const bgfx::Memory* memory = bgfx::alloc(static_cast<std::uint32_t>(probes * kSlots * 4U * sizeof(std::uint16_t)));
    auto* texels = reinterpret_cast<std::uint16_t*>(memory->data);
    for (std::uint32_t slot = 0U; slot < kSlots; ++slot) {
        for (std::size_t probe = 0U; probe < probes; ++probe) {
            const ShL2& sh = grid_->probes[probe];
            std::uint16_t* texel = texels + (slot * probes + probe) * 4U;
            for (std::uint32_t lane = 0U; lane < 4U; ++lane) {
                const std::uint32_t value = slot * 4U + lane;
                float component = 0.0F;
                if (value < 27U) {
                    const kb::math::Vec3& c = sh.c[value / 3U];
                    component = value % 3U == 0U ? c.x : value % 3U == 1U ? c.y : c.z;
                }
                texel[lane] = bx::halfFromFloat(component);
            }
        }
    }
    texture_ = bgfx::createTexture3D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
        static_cast<std::uint16_t>(depth * kSlots), false, bgfx::TextureFormat::RGBA16F,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, memory);
    if (bgfx::isValid(texture_)) {
        bgfx::setName(texture_, "KB Light Probes");
    }
}

void SceneLightProbeTexture::Apply(SceneRenderLightingConfig& config, const kb::math::DVec3& renderOrigin) const noexcept {
    if (!bgfx::isValid(texture_)) {
        return;
    }
    config.lightProbes = texture_;
    config.lightProbeGrid = { static_cast<float>(grid_->origin.x - renderOrigin.x), static_cast<float>(grid_->origin.y - renderOrigin.y),
        static_cast<float>(grid_->origin.z - renderOrigin.z), 1.0F / grid_->spacing };
    config.lightProbeSize = { static_cast<float>(grid_->dimensions[0]), static_cast<float>(grid_->dimensions[1]),
        static_cast<float>(grid_->dimensions[2]), 0.0F };
}

} // namespace kb::render
