#include "kb/render/scene/SceneSkyReflection.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <array>
#include <cstring>

namespace kb::render {
namespace {

struct FaceVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

[[nodiscard]] bgfx::VertexLayout FaceVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

template <typename Handle>
void DestroyHandle(Handle& handle) noexcept {
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

[[nodiscard]] bool HalfFloatCubeRenderable() noexcept {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return false;
    }
    const std::uint32_t support = caps->formats[bgfx::TextureFormat::RGBA16F];
    return (support & BGFX_CAPS_FORMAT_TEXTURE_CUBE) != 0U && (support & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0U &&
        (support & BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN) != 0U;
}

} // namespace

SceneSkyReflection::SceneSkyReflection() {
    // Value-initialised handles would be index 0, a live handle; every slot starts invalid.
    prefiltered_.fill(BGFX_INVALID_HANDLE);
    stepTargets_.fill(BGFX_INVALID_HANDLE);
}

SceneSkyReflection::~SceneSkyReflection() {
    Shutdown();
}

bool SceneSkyReflection::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    if (!HalfFloatCubeRenderable()) {
        WriteRendererDebugLog("sky_reflection",
            "Sky reflection unavailable: this backend cannot render to a half-float cube map with generated mips");
        return false;
    }
    captureProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_sky_capture.sc");
    prefilterProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_sky_prefilter.sc");
    if (!bgfx::isValid(captureProgram_) || !bgfx::isValid(prefilterProgram_)) {
        WriteRendererDebugLog("sky_reflection",
            "Sky reflection unavailable: fs_sky_capture.sc / fs_sky_prefilter.sc have no variant for this backend");
        Shutdown();
        return false;
    }
    faceUniform_ = bgfx::createUniform("u_skyCubeFace", bgfx::UniformType::Vec4);
    prefilterUniform_ = bgfx::createUniform("u_skyPrefilter", bgfx::UniformType::Vec4);
    horizonUniform_ = bgfx::createUniform("u_deferredBackdropHorizon", bgfx::UniformType::Vec4);
    zenithUniform_ = bgfx::createUniform("u_deferredBackdropZenith", bgfx::UniformType::Vec4);
    paramsUniform_ = bgfx::createUniform("u_deferredBackdropParams", bgfx::UniformType::Vec4);
    sunUniform_ = bgfx::createUniform("u_deferredBackdropSun", bgfx::UniformType::Vec4);
    sunColorUniform_ = bgfx::createUniform("u_deferredBackdropSunColor", bgfx::UniformType::Vec4);
    environmentSampler_ = bgfx::createUniform("s_deferredBackdropEnvironment", bgfx::UniformType::Sampler);
    captureSampler_ = bgfx::createUniform("s_skyCapture", bgfx::UniformType::Sampler);
    const std::array<std::uint8_t, 4> black{ 0U, 0U, 0U, 255U };
    fallbackEnvironment_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0U,
        bgfx::copy(black.data(), static_cast<std::uint32_t>(black.size())));

    capture_ = bgfx::createTextureCube(kFaceSize, true, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT);
    bgfx::setName(capture_, "KB Sky Capture");
    for (std::size_t buffer = 0U; buffer < prefiltered_.size(); ++buffer) {
        prefiltered_[buffer] = bgfx::createTextureCube(kFaceSize, true, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT);
        bgfx::setName(prefiltered_[buffer], buffer == 0U ? "KB Sky Reflection A" : "KB Sky Reflection B");
    }
    if (!IsInitialized()) {
        WriteRendererDebugLog("sky_reflection", "Sky reflection unavailable: its cube maps or targets could not be created");
        Shutdown();
        return false;
    }
    return true;
}

void SceneSkyReflection::Shutdown() noexcept {
    for (bgfx::FrameBufferHandle& face : stepTargets_) {
        DestroyHandle(face);
    }
    for (bgfx::TextureHandle& texture : prefiltered_) {
        DestroyHandle(texture);
    }
    DestroyHandle(capture_);
    DestroyHandle(fallbackEnvironment_);
    DestroyHandle(captureSampler_);
    DestroyHandle(environmentSampler_);
    DestroyHandle(sunColorUniform_);
    DestroyHandle(sunUniform_);
    DestroyHandle(paramsUniform_);
    DestroyHandle(zenithUniform_);
    DestroyHandle(horizonUniform_);
    DestroyHandle(prefilterUniform_);
    DestroyHandle(faceUniform_);
    DestroyHandle(prefilterProgram_);
    DestroyHandle(captureProgram_);
    hasTarget_ = false;
    step_ = 0U;
    shown_ = -1;
    completed_ = false;
    steppedThisFrame_ = false;
}

bool SceneSkyReflection::IsInitialized() const noexcept {
    if (!bgfx::isValid(captureProgram_) || !bgfx::isValid(prefilterProgram_) || !bgfx::isValid(capture_) ||
        !bgfx::isValid(fallbackEnvironment_) || !bgfx::isValid(captureSampler_) || !bgfx::isValid(environmentSampler_)) {
        return false;
    }
    for (const bgfx::TextureHandle texture : prefiltered_) {
        if (!bgfx::isValid(texture)) return false;
    }
    return true;
}

void SceneSkyReflection::SubmitFace(bgfx::ViewId viewId, bgfx::FrameBufferHandle target, std::uint16_t size,
    bgfx::ProgramHandle program, std::uint8_t face, bool originBottomLeft) {
    const bgfx::VertexLayout layout = FaceVertexLayout();
    if (bgfx::getAvailTransientVertexBuffer(3, layout) < 3U) {
        WriteRendererDebugLog("sky_reflection", "Sky reflection face skipped: no transient vertices left this frame");
        return;
    }
    // The fullscreen triangle of the deferred lighting: uv 0,0 at the top-left corner of the target.
    constexpr std::array<FaceVertex, 3> triangle{
        FaceVertex{ -1.0F, 1.0F, 0.0F, 0.0F, 0.0F },
        FaceVertex{ 3.0F, 1.0F, 0.0F, 2.0F, 0.0F },
        FaceVertex{ -1.0F, -3.0F, 0.0F, 0.0F, 2.0F },
    };
    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, 3, layout);
    std::memcpy(vertices.data, triangle.data(), sizeof(FaceVertex) * triangle.size());
    bgfx::setViewName(viewId, "KB Sky Reflection");
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, size, size);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(viewId, nullptr, nullptr);
    const std::array<float, 4> faceValue{ static_cast<float>(face), originBottomLeft ? 1.0F : 0.0F, 0.0F, 0.0F };
    bgfx::setUniform(faceUniform_, faceValue.data());
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(viewId, program);
}

void SceneSkyReflection::Update(const SceneBackdropUniformValues& backdrop, bgfx::TextureHandle environment, bool originBottomLeft) {
    if (!IsInitialized() || steppedThisFrame_ || completed_ || !backdrop.HasSky()) {
        return;
    }
    const Key key{ .backdrop = backdrop, .environment = environment.idx };
    if (!hasTarget_ || !(key == target_)) {
        target_ = key;
        hasTarget_ = true;
        step_ = 0U;
    }
    if (shown_ >= 0 && shownKey_ == target_) {
        return;
    }
    steppedThisFrame_ = true;
    // The shown cube stays bound to the lighting while the other one is filled.
    const std::size_t back = shown_ < 0 ? 0U : static_cast<std::size_t>(1 - shown_);
    for (std::uint8_t face = 0U; face < 6U; ++face) {
        bgfx::Attachment attachment;
        if (step_ == 0U) {
            // The capture's mips feed the filtered importance sampling of the prefilter.
            attachment.init(capture_, bgfx::Access::Write, face, 1, 0, BGFX_RESOLVE_AUTO_GEN_MIPS);
        } else {
            attachment.init(prefiltered_[back], bgfx::Access::Write, face, 1, static_cast<std::uint16_t>(step_ - 1U),
                BGFX_RESOLVE_NONE);
        }
        stepTargets_[face] = bgfx::createFrameBuffer(1, &attachment, false);
        if (!bgfx::isValid(stepTargets_[face])) {
            // The step is repeated next frame; a partly rendered level would be shown as complete.
            WriteRendererDebugLog("sky_reflection", "Sky reflection step postponed: no frame buffer left for a cube face");
            for (bgfx::FrameBufferHandle& target : stepTargets_) {
                DestroyHandle(target);
            }
            return;
        }
    }
    for (std::uint8_t face = 0U; face < 6U; ++face) {
        const bgfx::ViewId viewId = static_cast<bgfx::ViewId>(ViewId::SkyReflectionStart + face);
        if (step_ == 0U) {
            bgfx::setUniform(horizonUniform_, backdrop.horizon.data());
            bgfx::setUniform(zenithUniform_, backdrop.zenith.data());
            bgfx::setUniform(paramsUniform_, backdrop.params.data());
            bgfx::setUniform(sunUniform_, backdrop.sun.data());
            bgfx::setUniform(sunColorUniform_, backdrop.sunColor.data());
            bgfx::setTexture(0U, environmentSampler_, bgfx::isValid(environment) ? environment : fallbackEnvironment_);
            SubmitFace(viewId, stepTargets_[face], kFaceSize, captureProgram_, face, originBottomLeft);
        } else {
            const std::uint8_t mip = static_cast<std::uint8_t>(step_ - 1U);
            const std::array<float, 4> prefilter{
                static_cast<float>(mip) / static_cast<float>(kMipCount - 1U),
                static_cast<float>(kFaceSize),
                static_cast<float>(kMipCount),
                0.0F,
            };
            bgfx::setUniform(prefilterUniform_, prefilter.data());
            bgfx::setTexture(0U, captureSampler_, capture_);
            const std::uint16_t size = static_cast<std::uint16_t>(kFaceSize >> mip);
            SubmitFace(viewId, stepTargets_[face], size, prefilterProgram_, face, originBottomLeft);
        }
    }
    ++step_;
    if (step_ > kMipCount) {
        completed_ = true;
    }
}

void SceneSkyReflection::EndFrame() noexcept {
    steppedThisFrame_ = false;
    for (bgfx::FrameBufferHandle& face : stepTargets_) {
        DestroyHandle(face);
    }
    if (completed_) {
        shown_ = shown_ < 0 ? 0 : 1 - shown_;
        shownKey_ = target_;
        completed_ = false;
    }
}

bgfx::TextureHandle SceneSkyReflection::ReflectionFor(
    const SceneBackdropUniformValues& backdrop, bgfx::TextureHandle environment) const noexcept {
    if (shown_ < 0 || !backdrop.HasSky()) {
        return BGFX_INVALID_HANDLE;
    }
    const Key key{ .backdrop = backdrop, .environment = environment.idx };
    return key == shownKey_ ? prefiltered_[static_cast<std::size_t>(shown_)] : bgfx::TextureHandle{ bgfx::kInvalidHandle };
}

} // namespace kb::render
