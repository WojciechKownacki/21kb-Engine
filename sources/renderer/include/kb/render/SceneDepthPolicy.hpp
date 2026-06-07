#pragma once

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cstdint>

namespace kb::render {

enum class SceneDepthClipSpace : std::uint8_t {
    ZeroToOne,
    MinusOneToOne,
};

class SceneDepthPolicy {
public:
    SceneDepthPolicy() = delete;

    static constexpr float kMinimumClipDistance = 0.0001F;
    static constexpr float kMinimumAspect = 0.0001F;
    static constexpr float kMinimumFovDegrees = 1.0F;
    static constexpr float kMaximumFovDegrees = 179.0F;

    [[nodiscard]] static constexpr float ClearDepth() noexcept {
        return 0.0F;
    }

    [[nodiscard]] static constexpr float NearDepth() noexcept {
        return 1.0F;
    }

    [[nodiscard]] static constexpr float FarDepth() noexcept {
        return 0.0F;
    }

    [[nodiscard]] static constexpr std::uint64_t DepthTestState() noexcept {
        return BGFX_STATE_DEPTH_TEST_GEQUAL;
    }

    [[nodiscard]] static constexpr std::uint64_t SceneWriteState() noexcept {
        return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | DepthTestState() | BGFX_STATE_MSAA;
    }

    [[nodiscard]] static constexpr std::uint64_t SceneDepthReadState() noexcept {
        return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | DepthTestState() | BGFX_STATE_MSAA;
    }

    [[nodiscard]] static constexpr std::uint64_t SceneOverlayState(bool depthTested) noexcept {
        return depthTested ? SceneDepthReadState() : (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA);
    }

    [[nodiscard]] static constexpr std::uint64_t SceneMeshState(bool doubleSided) noexcept {
        return doubleSided ? SceneWriteState() : (SceneWriteState() | BGFX_STATE_CULL_CCW);
    }

    [[nodiscard]] static float SanitizeNearClip(float nearClip) noexcept {
        return std::max(nearClip, kMinimumClipDistance);
    }

    [[nodiscard]] static float SanitizeFarClip(float nearClip, float farClip) noexcept {
        return std::max(farClip, SanitizeNearClip(nearClip) + kMinimumClipDistance);
    }

    [[nodiscard]] static float SanitizeAspect(float aspect) noexcept {
        return std::max(aspect, kMinimumAspect);
    }

    [[nodiscard]] static float SanitizePerspectiveFov(float verticalFovDegrees) noexcept {
        return std::clamp(verticalFovDegrees, kMinimumFovDegrees, kMaximumFovDegrees);
    }

    [[nodiscard]] static float SanitizeOrthographicHeight(float orthographicHeight) noexcept {
        return std::max(orthographicHeight, kMinimumClipDistance);
    }

    [[nodiscard]] static bool HomogeneousDepth() noexcept {
        const bgfx::Caps* caps = bgfx::getCaps();
        return caps != nullptr && caps->homogeneousDepth;
    }

    [[nodiscard]] static constexpr SceneDepthClipSpace ClipSpaceForHomogeneousDepth(bool homogeneousDepth) noexcept {
        return homogeneousDepth ? SceneDepthClipSpace::MinusOneToOne : SceneDepthClipSpace::ZeroToOne;
    }

    [[nodiscard]] static constexpr const char* ClipSpaceName(SceneDepthClipSpace clipSpace) noexcept {
        switch (clipSpace) {
        case SceneDepthClipSpace::ZeroToOne:
            return "zero-to-one";
        case SceneDepthClipSpace::MinusOneToOne:
            return "minus-one-to-one";
        }

        return "unknown";
    }

    [[nodiscard]] static constexpr const char* BackendDepthConvention(bgfx::RendererType::Enum renderer) noexcept {
        switch (renderer) {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
        case bgfx::RendererType::Vulkan:
        case bgfx::RendererType::Metal:
            return "bgfx normally exposes zero-to-one clip depth; use caps.homogeneousDepth as the runtime authority";
        case bgfx::RendererType::OpenGL:
        case bgfx::RendererType::OpenGLES:
            return "bgfx normally exposes minus-one-to-one homogeneous clip depth; use caps.homogeneousDepth as the runtime authority";
        default:
            return "depth clip convention is backend-defined; use caps.homogeneousDepth as the runtime authority";
        }
    }

    static void MakePerspective(float* outProjection, float verticalFovDegrees, float aspect, float nearClip, float farClip, bool homogeneousDepth) noexcept {
        if (outProjection == nullptr) {
            return;
        }

        const float sanitizedNear = SanitizeNearClip(nearClip);
        const float sanitizedFar = SanitizeFarClip(sanitizedNear, farClip);
        bx::mtxProj(
            outProjection,
            SanitizePerspectiveFov(verticalFovDegrees),
            SanitizeAspect(aspect),
            sanitizedFar,
            sanitizedNear,
            homogeneousDepth);
    }

    static void MakeOrthographic(float* outProjection, float height, float aspect, float nearClip, float farClip, bool homogeneousDepth) noexcept {
        if (outProjection == nullptr) {
            return;
        }

        const float sanitizedAspect = SanitizeAspect(aspect);
        const float halfHeight = SanitizeOrthographicHeight(height) * 0.5F;
        const float halfWidth = halfHeight * sanitizedAspect;
        const float sanitizedNear = SanitizeNearClip(nearClip);
        const float sanitizedFar = SanitizeFarClip(sanitizedNear, farClip);
        bx::mtxOrtho(
            outProjection,
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            sanitizedFar,
            sanitizedNear,
            0.0F,
            homogeneousDepth);
    }
};

} // namespace kb::render
