#pragma once

#include "kb/render/scene/SceneBackdropUniforms.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

// Unreal's SkyLight with real-time capture: the world backdrop rendered into a cube map and
// prefiltered with GGX for every roughness, so metals and clear coats reflect the sky the camera
// sees. The work is spread over frames - the capture, then one roughness level per frame - into a
// back cube that replaces the shown one once it is complete.
class SceneSkyReflection {
public:
    static constexpr std::uint16_t kFaceSize = 128U;
    static constexpr std::uint8_t kMipCount = 8U; // 128 .. 1 texels

    SceneSkyReflection();
    ~SceneSkyReflection();

    SceneSkyReflection(const SceneSkyReflection&) = delete;
    SceneSkyReflection& operator=(const SceneSkyReflection&) = delete;

    // False when the capture shaders or a renderable half-float cube map are unavailable on this
    // backend; reflections then keep the environment colour (the renderer logs it).
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    // Advances the capture of this backdrop by one step, at most once per frame, on the views from
    // ViewId::SkyReflectionStart. environment: the equirectangular map of an EnvironmentMap backdrop.
    void Update(const SceneBackdropUniformValues& backdrop, bgfx::TextureHandle environment, bool originBottomLeft);
    // A completed capture becomes the shown one here, after the frame that rendered its last level.
    void EndFrame() noexcept;

    // The prefiltered cube of this backdrop, or an invalid handle while it is not captured yet.
    [[nodiscard]] bgfx::TextureHandle ReflectionFor(
        const SceneBackdropUniformValues& backdrop, bgfx::TextureHandle environment) const noexcept;
    [[nodiscard]] static constexpr float MaxMip() noexcept { return static_cast<float>(kMipCount - 1U); }

private:
    struct Key {
        SceneBackdropUniformValues backdrop{};
        std::uint16_t environment = bgfx::kInvalidHandle;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };

    void SubmitFace(bgfx::ViewId viewId, bgfx::FrameBufferHandle target, std::uint16_t size, bgfx::ProgramHandle program,
        std::uint8_t face, bool originBottomLeft);

    bgfx::ProgramHandle captureProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle prefilterProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle faceUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle prefilterUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle horizonUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle zenithUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle paramsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sunUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sunColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle captureSampler_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackEnvironment_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle capture_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, 2> prefiltered_{};
    // The six face targets of this frame's step: made for the step and released after its frame, so
    // the capture holds only a handful of the renderer's frame buffers at a time.
    std::array<bgfx::FrameBufferHandle, 6> stepTargets_{};

    Key target_{};
    bool hasTarget_ = false;
    // 0: capture; 1..kMipCount: prefilter mip step - 1; past that: done.
    std::uint32_t step_ = 0U;
    int shown_ = -1;
    Key shownKey_{};
    bool completed_ = false;
    bool steppedThisFrame_ = false;
};

} // namespace kb::render
