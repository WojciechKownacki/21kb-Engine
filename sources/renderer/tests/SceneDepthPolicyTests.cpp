#include "RendererTestSupport.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"

#include <array>

namespace kb::render::tests {
namespace {

void SceneDepthPolicyUsesReverseZConventions() {
    Require(NearlyEqual(SceneDepthPolicy::ClearDepth(), 0.0F), "SceneDepthPolicy clear depth is not reverse-Z");
    Require(NearlyEqual(SceneDepthPolicy::NearDepth(), 1.0F), "SceneDepthPolicy near depth is not reverse-Z");
    Require(NearlyEqual(SceneDepthPolicy::FarDepth(), 0.0F), "SceneDepthPolicy far depth is not reverse-Z");
    Require(SceneDepthPolicy::DepthTestState() == BGFX_STATE_DEPTH_TEST_GEQUAL, "SceneDepthPolicy does not use GEQUAL depth test");
}

void SceneSubmitDescDefaultsToReverseZClearDepth() {
    const RenderSceneSubmitDesc desc{};
    Require(NearlyEqual(desc.clearDepth, SceneDepthPolicy::ClearDepth()), "RenderSceneSubmitDesc default clear depth ignores SceneDepthPolicy");
}

void SceneMeshStateUsesReverseZDepthTest() {
    constexpr std::uint64_t state = SceneDepthPolicy::SceneMeshState(true);
    Require((state & BGFX_STATE_WRITE_Z) != 0U, "Scene mesh state does not write depth");
    Require((state & BGFX_STATE_DEPTH_TEST_MASK) == BGFX_STATE_DEPTH_TEST_GEQUAL, "Scene mesh state does not use reverse-Z depth test");
}

void SceneOverlayStateUsesExplicitDepthPolicy() {
    constexpr std::uint64_t depthTested = SceneDepthPolicy::SceneOverlayState(true);
    constexpr std::uint64_t screenSpace = SceneDepthPolicy::SceneOverlayState(false);

    Require((depthTested & BGFX_STATE_WRITE_Z) == 0U, "Depth-tested overlays must not write scene depth");
    Require((depthTested & BGFX_STATE_DEPTH_TEST_MASK) == BGFX_STATE_DEPTH_TEST_GEQUAL, "Depth-tested overlays do not use reverse-Z depth test");
    Require((screenSpace & BGFX_STATE_WRITE_Z) == 0U, "Screen-space overlays must not write scene depth");
    Require((screenSpace & BGFX_STATE_DEPTH_TEST_MASK) == 0U, "Screen-space overlays must not depth test against scene depth");
}

void SceneDepthPolicySanitizesProjectionInputs() {
    Require(NearlyEqual(SceneDepthPolicy::SanitizeNearClip(-1.0F), SceneDepthPolicy::kMinimumClipDistance), "SceneDepthPolicy accepted a negative near clip");
    Require(SceneDepthPolicy::SanitizeFarClip(10.0F, 1.0F) > SceneDepthPolicy::SanitizeNearClip(10.0F), "SceneDepthPolicy did not keep far clip beyond near clip");
    Require(NearlyEqual(SceneDepthPolicy::SanitizeAspect(0.0F), SceneDepthPolicy::kMinimumAspect), "SceneDepthPolicy accepted zero aspect");
    Require(NearlyEqual(SceneDepthPolicy::SanitizePerspectiveFov(0.0F), SceneDepthPolicy::kMinimumFovDegrees), "SceneDepthPolicy accepted too-small FOV");
    Require(NearlyEqual(SceneDepthPolicy::SanitizePerspectiveFov(200.0F), SceneDepthPolicy::kMaximumFovDegrees), "SceneDepthPolicy accepted too-large FOV");
    Require(NearlyEqual(SceneDepthPolicy::SanitizeOrthographicHeight(0.0F), SceneDepthPolicy::kMinimumClipDistance), "SceneDepthPolicy accepted zero orthographic height");
}

void SceneDepthPolicyPerspectiveProjectionUsesReverseZ() {
    std::array<float, 16> projection{};
    SceneDepthPolicy::MakePerspective(projection.data(), 60.0F, 16.0F / 9.0F, 0.1F, 1000.0F, false);

    Require(projection[0] > 0.0F, "Perspective projection has invalid horizontal scale");
    Require(projection[5] > 0.0F, "Perspective projection has invalid vertical scale");
    Require(projection[10] < 0.0F, "Perspective projection does not use reversed depth slope");
    Require(projection[11] > 0.0F, "Perspective projection has invalid handedness");
    Require(projection[14] > 0.0F, "Perspective projection does not use reversed depth offset");
}

void SceneDepthPolicyPerspectiveProjectionSupportsHomogeneousDepth() {
    std::array<float, 16> projection{};
    SceneDepthPolicy::MakePerspective(projection.data(), 60.0F, 16.0F / 9.0F, 0.1F, 1000.0F, true);

    Require(projection[0] > 0.0F, "Homogeneous perspective projection has invalid horizontal scale");
    Require(projection[5] > 0.0F, "Homogeneous perspective projection has invalid vertical scale");
    Require(projection[10] < 0.0F, "Homogeneous perspective projection does not use reversed depth slope");
    Require(projection[11] > 0.0F, "Homogeneous perspective projection has invalid handedness");
    Require(projection[14] > 0.0F, "Homogeneous perspective projection does not use reversed depth offset");
}

void SceneDepthPolicyOrthographicProjectionUsesReverseZ() {
    std::array<float, 16> projection{};
    SceneDepthPolicy::MakeOrthographic(projection.data(), 10.0F, 2.0F, 0.1F, 1000.0F, false);

    Require(projection[0] > 0.0F, "Orthographic projection has invalid horizontal scale");
    Require(projection[5] > 0.0F, "Orthographic projection has invalid vertical scale");
    Require(projection[10] < 0.0F, "Orthographic projection does not use reversed depth slope");
    Require(projection[14] > 0.0F, "Orthographic projection does not use reversed depth offset");
    Require(NearlyEqual(projection[15], 1.0F), "Orthographic projection has invalid homogeneous coordinate");
}

void SceneDepthPolicyOrthographicProjectionSupportsHomogeneousDepth() {
    std::array<float, 16> projection{};
    SceneDepthPolicy::MakeOrthographic(projection.data(), 10.0F, 2.0F, 0.1F, 1000.0F, true);

    Require(projection[0] > 0.0F, "Homogeneous orthographic projection has invalid horizontal scale");
    Require(projection[5] > 0.0F, "Homogeneous orthographic projection has invalid vertical scale");
    Require(projection[10] < 0.0F, "Homogeneous orthographic projection does not use reversed depth slope");
    Require(projection[14] > 0.0F, "Homogeneous orthographic projection does not use reversed depth offset");
    Require(NearlyEqual(projection[15], 1.0F), "Homogeneous orthographic projection has invalid homogeneous coordinate");
}

void SceneDepthPolicyDocumentsBackendClipSpace() {
    Require(SceneDepthPolicy::ClipSpaceForHomogeneousDepth(false) == SceneDepthClipSpace::ZeroToOne, "Non-homogeneous bgfx depth should map to zero-to-one clip space");
    Require(SceneDepthPolicy::ClipSpaceForHomogeneousDepth(true) == SceneDepthClipSpace::MinusOneToOne, "Homogeneous bgfx depth should map to minus-one-to-one clip space");
    Require(SceneDepthPolicy::ClipSpaceName(SceneDepthClipSpace::ZeroToOne)[0] != '\0', "Zero-to-one clip-space name is empty");
    Require(SceneDepthPolicy::ClipSpaceName(SceneDepthClipSpace::MinusOneToOne)[0] != '\0', "Minus-one-to-one clip-space name is empty");
    Require(SceneDepthPolicy::BackendDepthConvention(bgfx::RendererType::Direct3D11)[0] != '\0', "D3D11 depth convention is undocumented");
    Require(SceneDepthPolicy::BackendDepthConvention(bgfx::RendererType::Direct3D12)[0] != '\0', "D3D12 depth convention is undocumented");
    Require(SceneDepthPolicy::BackendDepthConvention(bgfx::RendererType::Vulkan)[0] != '\0', "Vulkan depth convention is undocumented");
    Require(SceneDepthPolicy::BackendDepthConvention(bgfx::RendererType::Metal)[0] != '\0', "Metal depth convention is undocumented");
    Require(SceneDepthPolicy::BackendDepthConvention(bgfx::RendererType::OpenGL)[0] != '\0', "OpenGL depth convention is undocumented");
}

} // namespace

void RunSceneDepthPolicyTests() {
    SceneDepthPolicyUsesReverseZConventions();
    SceneSubmitDescDefaultsToReverseZClearDepth();
    SceneMeshStateUsesReverseZDepthTest();
    SceneOverlayStateUsesExplicitDepthPolicy();
    SceneDepthPolicySanitizesProjectionInputs();
    SceneDepthPolicyPerspectiveProjectionUsesReverseZ();
    SceneDepthPolicyPerspectiveProjectionSupportsHomogeneousDepth();
    SceneDepthPolicyOrthographicProjectionUsesReverseZ();
    SceneDepthPolicyOrthographicProjectionSupportsHomogeneousDepth();
    SceneDepthPolicyDocumentsBackendClipSpace();
}

} // namespace kb::render::tests
