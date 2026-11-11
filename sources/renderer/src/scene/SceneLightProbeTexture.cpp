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
    return caps != nullptr && (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0U;
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
    const std::uint32_t atlasWidth = width * kSlots;
    const std::uint32_t atlasHeight = height * depth;
    const std::uint32_t limit = bgfx::getCaps()->limits.maxTextureSize;
    if (atlasWidth > limit || atlasHeight > limit) {
        return;
    }
    const bgfx::Memory* memory = bgfx::alloc(atlasWidth * atlasHeight * 4U * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    auto* texels = reinterpret_cast<std::uint16_t*>(memory->data);
    for (std::uint32_t row = 0U; row < atlasHeight; ++row) {
        // Row `row` is y of layer z; probes are stored x fastest, then y, then z.
        const std::size_t layerStart = static_cast<std::size_t>(row / height) * width * height + static_cast<std::size_t>(row % height) * width;
        for (std::uint32_t column = 0U; column < atlasWidth; ++column) {
            const std::uint32_t slot = column / width;
            const ShL2& sh = grid_->probes[layerStart + column % width];
            std::uint16_t* texel = texels + (static_cast<std::size_t>(row) * atlasWidth + column) * 4U;
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
    texture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(atlasWidth), static_cast<std::uint16_t>(atlasHeight), false, 1U,
        bgfx::TextureFormat::RGBA16F, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, memory);
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

void LightProbeUniforms::Create(const char* samplerName) {
    sampler_ = bgfx::createUniform(samplerName, bgfx::UniformType::Sampler);
    grid_ = bgfx::createUniform("u_lightProbeGrid", bgfx::UniformType::Vec4);
    size_ = bgfx::createUniform("u_lightProbeSize", bgfx::UniformType::Vec4);
}

void LightProbeUniforms::Destroy() noexcept {
    for (bgfx::UniformHandle* handle : { &sampler_, &grid_, &size_ }) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
        }
        *handle = BGFX_INVALID_HANDLE;
    }
}

bool LightProbeUniforms::IsValid() const noexcept {
    return bgfx::isValid(sampler_) && bgfx::isValid(grid_) && bgfx::isValid(size_);
}

void LightProbeUniforms::Set(bgfx::TextureHandle texture, const std::array<float, 4>& grid, const std::array<float, 4>& size,
    std::uint8_t stage, bgfx::TextureHandle fallback) const {
    bgfx::setUniform(grid_, grid.data());
    bgfx::setUniform(size_, size.data());
    const bgfx::TextureHandle bound = bgfx::isValid(texture) ? texture : fallback;
    if (bgfx::isValid(bound)) {
        bgfx::setTexture(stage, sampler_, bound);
    }
}

} // namespace kb::render
