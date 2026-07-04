#include "kb/render/overlay/SceneGridPass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace kb::render {
namespace {

struct PosVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct GridCamera {
    std::array<float, 4> cameraPos{ 0.0F, 5.0F, 5.0F, 0.0F };
    std::array<float, 4> basisRight{ 1.0F, 0.0F, 0.0F, 1.0F };
    std::array<float, 4> basisUp{ 0.0F, 1.0F, 0.0F, 1.0F };
    std::array<float, 4> basisForward{ 0.0F, 0.0F, 1.0F, 0.0F };
    std::array<float, 4> gridOrigin{ 0.0F, 0.0F, 0.01F, 0.0F };
    std::array<float, 16> viewProjection{};
};

constexpr float kPlaneY = 0.0F;
constexpr float kFarFadeStartMeters = 220.0F;
constexpr float kFarFadeEndMeters = 1600.0F;
constexpr float kMinorLineWidthPixels = 0.64F;
constexpr float kMajorLineWidthPixels = 0.86F;
constexpr float kAxisLineWidthPixels = 1.16F;
constexpr float kMinorAlpha = 0.28F;
constexpr float kMajorAlpha = 0.48F;
constexpr float kAxisAlpha = 0.46F;

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] bgfx::VertexLayout FullscreenLayout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] bool IsFinite(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool IsPerspectiveProjection(const std::array<float, 16>& projection) noexcept {
    return std::abs(projection[15]) < 0.0001F;
}

[[nodiscard]] float SafeReciprocal(float value, float fallback) noexcept {
    return std::abs(value) > 0.000001F ? 1.0F / value : fallback;
}

[[nodiscard]] float ExtractAspect(const std::array<float, 16>& projection) noexcept {
    if (!IsFinite(projection[0]) || !IsFinite(projection[5]) || std::abs(projection[0]) <= 0.000001F) {
        return 1.0F;
    }
    return std::max(projection[5] / projection[0], SceneDepthPolicy::kMinimumAspect);
}

[[nodiscard]] float ExtractNearClip(const std::array<float, 16>& projection) noexcept {
    if (!IsFinite(projection[10]) || !IsFinite(projection[14])) {
        return 0.01F;
    }

    const float denominator = 1.0F - projection[10];
    if (std::abs(denominator) <= 0.000001F) {
        return 0.01F;
    }
    return SceneDepthPolicy::SanitizeNearClip(projection[14] / denominator);
}

[[nodiscard]] float ExtractOrthoNearClip(const std::array<float, 16>& projection, bool homogeneousDepth) noexcept {
    if (!IsFinite(projection[10]) || !IsFinite(projection[14]) || std::abs(projection[10]) <= 0.000001F) {
        return 0.01F;
    }

    if (homogeneousDepth) {
        const float range = -2.0F / projection[10];
        const float nearClip = ((projection[14] * range) - range) * 0.5F;
        return SceneDepthPolicy::SanitizeNearClip(nearClip);
    }

    const float range = -1.0F / projection[10];
    return SceneDepthPolicy::SanitizeNearClip((projection[14] - 1.0F) * range);
}

[[nodiscard]] float ExtractOrthoFarClip(const std::array<float, 16>& projection, bool homogeneousDepth) noexcept {
    if (!IsFinite(projection[10]) || !IsFinite(projection[14]) || std::abs(projection[10]) <= 0.000001F) {
        return 100000.0F;
    }

    const float range = homogeneousDepth ? (-2.0F / projection[10]) : (-1.0F / projection[10]);
    const float farClip = homogeneousDepth
        ? ((projection[14] * range) + range) * 0.5F
        : projection[14] * range;
    return std::clamp(farClip, 1.0F, 100000.0F);
}

[[nodiscard]] float SnapToAnchor(float worldCoord, float anchorSpacing) noexcept {
    if (!IsFinite(worldCoord) || !IsFinite(anchorSpacing) || anchorSpacing <= 0.0F) {
        return 0.0F;
    }
    return std::floor(worldCoord / anchorSpacing) * anchorSpacing;
}

[[nodiscard]] GridCamera CameraFromMatrices(const SceneRenderCamera& camera, float minorSpacingMeters, std::uint32_t majorEvery) noexcept {
    float inverseView[16]{};
    bx::mtxInverse(inverseView, camera.view.data());

    const bool orthographic = !IsPerspectiveProjection(camera.projection);
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    const float aspect = ExtractAspect(camera.projection);
    const float halfHeight = orthographic
        ? std::max(0.5F * SafeReciprocal(camera.projection[5], 20.0F), 0.0001F)
        : SafeReciprocal(camera.projection[5], 1.0F);
    const float halfWidth = halfHeight * aspect;
    const float nearClip = orthographic ? ExtractOrthoNearClip(camera.projection, homogeneousDepth) : ExtractNearClip(camera.projection);
    const float farClip = orthographic ? ExtractOrthoFarClip(camera.projection, homogeneousDepth) : 0.0F;

    const float minorSpacing = std::clamp(minorSpacingMeters, 0.01F, 1000.0F);
    const float majorSpacing = minorSpacing * static_cast<float>(std::clamp(majorEvery, 2U, 1000U));
    const float anchorSpacing = majorSpacing * 10.0F;
    std::array<float, 16> viewProjection{};
    bx::mtxMul(viewProjection.data(), camera.view.data(), camera.projection.data());

    return GridCamera{
        .cameraPos = { inverseView[12], inverseView[13], inverseView[14], kPlaneY },
        .basisRight = { inverseView[0], inverseView[1], inverseView[2], halfWidth },
        .basisUp = { inverseView[4], inverseView[5], inverseView[6], halfHeight },
        .basisForward = { inverseView[8], inverseView[9], inverseView[10], orthographic ? 1.0F : 0.0F },
        .gridOrigin = {
            SnapToAnchor(inverseView[12], anchorSpacing),
            SnapToAnchor(inverseView[14], anchorSpacing),
            nearClip,
            farClip,
        },
        .viewProjection = viewProjection,
    };
}

void DestroyUniform(bgfx::UniformHandle& uniform) noexcept {
    if (bgfx::isValid(uniform)) {
        bgfx::destroy(uniform);
        uniform = BGFX_INVALID_HANDLE;
    }
}

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "true" : "false";
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] bool SameHandle(bgfx::TextureHandle lhs, bgfx::TextureHandle rhs) noexcept {
    return lhs.idx == rhs.idx;
}

void ConfigureOverlayView(const SceneGridPassDesc& desc, bgfx::FrameBufferHandle frameBuffer) {
    const std::array<float, 16> identity = IdentityMatrix();
    const RenderViewportRect outputRect = desc.outputRect.extent.IsValid()
        ? desc.outputRect
        : RenderViewportRect{ .extent = desc.extent };
    bgfx::setViewName(desc.viewId, "KB Editor Grid");
    bgfx::setViewFrameBuffer(desc.viewId, frameBuffer);
    bgfx::setViewTransform(desc.viewId, identity.data(), identity.data());
    bgfx::setViewRect(
        desc.viewId,
        ClampToViewExtent(outputRect.x),
        ClampToViewExtent(outputRect.y),
        ClampToViewExtent(outputRect.extent.width),
        ClampToViewExtent(outputRect.extent.height));
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(desc.viewId);
}

} // namespace

SceneGridPass::~SceneGridPass() {
    Shutdown();
}

bool SceneGridPassDesc::IsValid() const noexcept {
    return extent.IsValid() && (!outputRect.extent.IsValid() || outputRect.IsValid()) && camera != nullptr &&
           std::isfinite(minorSpacingMeters) && minorSpacingMeters > 0.0F && majorEvery >= 2U &&
           (buildDepthFrameBuffer ? (bgfx::isValid(colorTexture) && bgfx::isValid(depthTexture)) : bgfx::isValid(frameBuffer));
}

bool SceneGridPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_editor_grid.sc", "fs_editor_grid.sc");
    cameraPosUniform_ = bgfx::createUniform("u_editorGridCameraPos", bgfx::UniformType::Vec4);
    basisRightUniform_ = bgfx::createUniform("u_editorGridBasisRight", bgfx::UniformType::Vec4);
    basisUpUniform_ = bgfx::createUniform("u_editorGridBasisUp", bgfx::UniformType::Vec4);
    basisForwardUniform_ = bgfx::createUniform("u_editorGridBasisForward", bgfx::UniformType::Vec4);
    gridParamsUniform_ = bgfx::createUniform("u_editorGridParams", bgfx::UniformType::Vec4);
    gridOriginUniform_ = bgfx::createUniform("u_editorGridOrigin", bgfx::UniformType::Vec4);
    gridWidthsUniform_ = bgfx::createUniform("u_editorGridWidths", bgfx::UniformType::Vec4);
    gridStyleUniform_ = bgfx::createUniform("u_editorGridStyle", bgfx::UniformType::Vec4);
    depthParamsUniform_ = bgfx::createUniform("u_editorGridDepthParams", bgfx::UniformType::Vec4);
    viewProjectionUniform_ = bgfx::createUniform("u_editorGridViewProjection", bgfx::UniformType::Mat4);
    fullscreenLayout_ = FullscreenLayout();
    initialized_ = true;
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGridPass::Shutdown() noexcept {
    if (!initialized_) {
        InvalidateFrameBuffers();
        return;
    }
    DestroyUniform(gridStyleUniform_);
    DestroyUniform(viewProjectionUniform_);
    DestroyUniform(depthParamsUniform_);
    DestroyUniform(gridWidthsUniform_);
    DestroyUniform(gridOriginUniform_);
    DestroyUniform(gridParamsUniform_);
    DestroyUniform(basisForwardUniform_);
    DestroyUniform(basisUpUniform_);
    DestroyUniform(basisRightUniform_);
    DestroyUniform(cameraPosUniform_);
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    InvalidateFrameBuffers();
    fullscreenLayout_ = {};
    initialized_ = false;
}

void SceneGridPass::InvalidateFrameBuffers() noexcept {
    if (bgfx::isValid(depthFrameBuffer_)) {
        bgfx::destroy(depthFrameBuffer_);
        WriteRendererDebugLog("grid_trace", "Invalidated depth-tested grid framebuffer");
    }
    depthFrameBuffer_ = BGFX_INVALID_HANDLE;
    depthFrameBufferColor_ = BGFX_INVALID_HANDLE;
    depthFrameBufferDepth_ = BGFX_INVALID_HANDLE;
}

bool SceneGridPass::Submit(const SceneGridPassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        std::ostringstream message;
        message << "Submit rejected"
                << " initialized=" << BoolText(IsInitialized())
                << " descValid=" << BoolText(desc.IsValid())
                << " viewId=" << desc.viewId
                << " fb=" << HandleValue(desc.frameBuffer)
                << " colorTex=" << HandleValue(desc.colorTexture)
                << " depthTex=" << HandleValue(desc.depthTexture)
                << " rebuild=" << BoolText(desc.buildDepthFrameBuffer)
                << " camera=" << BoolText(desc.camera != nullptr)
                << " extent=" << desc.extent.width << 'x' << desc.extent.height;
        WriteRendererDebugLog("grid_trace", message.str());
        return false;
    }

    constexpr std::array<PosVertex, 3U> triangle{
        PosVertex{ -1.0F, -1.0F, 1.0F },
        PosVertex{ 3.0F, -1.0F, 1.0F },
        PosVertex{ -1.0F, 3.0F, 1.0F },
    };
    constexpr std::uint32_t vertexCount = static_cast<std::uint32_t>(triangle.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, fullscreenLayout_) < vertexCount) {
        return false;
    }

    const float minorSpacing = std::clamp(desc.minorSpacingMeters, 0.01F, 1000.0F);
    const float majorSpacing = minorSpacing * static_cast<float>(std::clamp(desc.majorEvery, 2U, 1000U));
    const GridCamera camera = CameraFromMatrices(*desc.camera, minorSpacing, desc.majorEvery);
    const std::array<float, 4> gridParams{ minorSpacing, majorSpacing, kFarFadeStartMeters, kFarFadeEndMeters };
    const std::array<float, 4> gridWidths{ kMinorLineWidthPixels, kMajorLineWidthPixels, kAxisLineWidthPixels, 0.0F };
    const std::array<float, 4> gridStyle{ kMinorAlpha, kMajorAlpha, kAxisAlpha, 0.0F };
    const std::array<float, 4> depthParams{ 1.0F, 0.0F, SceneDepthPolicy::HomogeneousDepth() ? 1.0F : 0.0F, 0.0F };
    bgfx::FrameBufferHandle frameBuffer = desc.frameBuffer;
    if (desc.buildDepthFrameBuffer) {
        if (!bgfx::isValid(depthFrameBuffer_) ||
            !SameHandle(depthFrameBufferColor_, desc.colorTexture) ||
            !SameHandle(depthFrameBufferDepth_, desc.depthTexture)) {
            if (bgfx::isValid(depthFrameBuffer_)) {
                bgfx::destroy(depthFrameBuffer_);
                depthFrameBuffer_ = BGFX_INVALID_HANDLE;
            }
            bgfx::Attachment attachments[2]{};
            attachments[0].init(desc.colorTexture);
            attachments[1].init(desc.depthTexture);
            depthFrameBuffer_ = bgfx::createFrameBuffer(2U, attachments, false);
            depthFrameBufferColor_ = desc.colorTexture;
            depthFrameBufferDepth_ = desc.depthTexture;
            std::ostringstream message;
            message << "Created depth-tested grid framebuffer"
                    << " fb=" << HandleValue(depthFrameBuffer_)
                    << " colorTex=" << HandleValue(depthFrameBufferColor_)
                    << " depthTex=" << HandleValue(depthFrameBufferDepth_);
            WriteRendererDebugLog("grid_trace", message.str());
        }
        frameBuffer = depthFrameBuffer_;
    }

    if (!bgfx::isValid(frameBuffer)) {
        std::ostringstream message;
        message << "Submit failed invalid depth-tested framebuffer"
                << " rebuild=" << BoolText(desc.buildDepthFrameBuffer)
                << " inputFb=" << HandleValue(desc.frameBuffer)
                << " colorTex=" << HandleValue(desc.colorTexture)
                << " depthTex=" << HandleValue(desc.depthTexture);
        WriteRendererDebugLog("grid_trace", message.str());
        return false;
    }
    {
        std::ostringstream message;
        message << "Submit depth-tested grid"
                << " viewId=" << desc.viewId
                << " fb=" << HandleValue(frameBuffer)
                << " inputFb=" << HandleValue(desc.frameBuffer)
                << " rebuild=" << BoolText(desc.buildDepthFrameBuffer)
                << " colorTex=" << HandleValue(desc.colorTexture)
                << " depthTex=" << HandleValue(desc.depthTexture)
                << " extent=" << desc.extent.width << 'x' << desc.extent.height
                << " homogeneousDepth=" << BoolText(SceneDepthPolicy::HomogeneousDepth());
        WriteRendererDebugLog("grid_trace", message.str());
    }

    ConfigureOverlayView(desc, frameBuffer);
    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, fullscreenLayout_);
    std::memcpy(vertices.data, triangle.data(), sizeof(PosVertex) * triangle.size());

    bgfx::setUniform(cameraPosUniform_, camera.cameraPos.data());
    bgfx::setUniform(basisRightUniform_, camera.basisRight.data());
    bgfx::setUniform(basisUpUniform_, camera.basisUp.data());
    bgfx::setUniform(basisForwardUniform_, camera.basisForward.data());
    bgfx::setUniform(gridParamsUniform_, gridParams.data());
    bgfx::setUniform(gridOriginUniform_, camera.gridOrigin.data());
    bgfx::setUniform(gridWidthsUniform_, gridWidths.data());
    bgfx::setUniform(gridStyleUniform_, gridStyle.data());
    bgfx::setUniform(depthParamsUniform_, depthParams.data());
    bgfx::setUniform(viewProjectionUniform_, camera.viewProjection.data());
    bgfx::setState(SceneDepthPolicy::SceneOverlayState(true) | BGFX_STATE_BLEND_ALPHA);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SceneGridPass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && bgfx::isValid(cameraPosUniform_) &&
           bgfx::isValid(basisRightUniform_) && bgfx::isValid(basisUpUniform_) &&
           bgfx::isValid(basisForwardUniform_) && bgfx::isValid(gridParamsUniform_) &&
           bgfx::isValid(gridOriginUniform_) && bgfx::isValid(gridWidthsUniform_) &&
           bgfx::isValid(gridStyleUniform_) && bgfx::isValid(depthParamsUniform_) && bgfx::isValid(viewProjectionUniform_) &&
           fullscreenLayout_.getStride() == sizeof(PosVertex);
}

} // namespace kb::render
