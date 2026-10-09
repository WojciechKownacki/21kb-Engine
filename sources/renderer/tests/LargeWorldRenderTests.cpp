#include "RendererTestSupport.hpp"

#include "engine/math/DVec3.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace kb::render::tests {
namespace {

using kb::math::DVec3;

// Ten thousand kilometres out on two axes: neighbouring floats there are a metre apart.
constexpr double kFar = 10'000'000.0;

[[nodiscard]] bool Near(kb::math::Vec3 actual, kb::math::Vec3 expected, float tolerance) noexcept {
    return std::fabs(actual.x - expected.x) <= tolerance && std::fabs(actual.y - expected.y) <= tolerance && std::fabs(actual.z - expected.z) <= tolerance;
}

// view * (p, 1) for a column-major view matrix.
[[nodiscard]] kb::math::Vec3 ViewPoint(const std::array<float, 16>& view, kb::math::Vec3 point) noexcept {
    return kb::math::Vec3{
        view[0] * point.x + view[4] * point.y + view[8] * point.z + view[12],
        view[1] * point.x + view[5] * point.y + view[9] * point.z + view[13],
        view[2] * point.x + view[6] * point.y + view[10] * point.z + view[14],
    };
}

[[nodiscard]] kb::math::Vec3 ModelTranslation(const std::array<float, 16>& model) noexcept {
    return kb::math::Vec3{ model[12], model[13], model[14] };
}

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 64U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 64U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

// The origin stays while the viewer is close to it, then jumps to the viewer rounded to the grid; a move marks every
// proxy for a re-pull and tells the listeners.
void RunRenderOriginPolicyTest() {
    RenderScene renderScene;
    const RenderOriginPolicy policy{};
    Require(renderScene.RenderOriginFor(DVec3{ 1000.0, -900.0, 512.0 }, policy) == DVec3{}, "The render origin moved for a viewer near it");
    const DVec3 far = renderScene.RenderOriginFor(DVec3{ kFar + 300.0, 12.0, -kFar - 700.0 }, policy);
    // 10000300 / 1024 = 9765.9 and -10000700 / 1024 = -9766.3: both round to 9766 grid steps.
    Require(far == DVec3{ 9766.0 * 1024.0, 0.0, -9766.0 * 1024.0 }, "The render origin did not follow the viewer to the nearest grid point");
    DVec3 previous{ 1.0, 2.0, 3.0 };
    DVec3 current{ 1.0, 2.0, 3.0 };
    const std::uint64_t listener = renderScene.AddRenderOriginListener([&previous, &current](const DVec3& before, const DVec3& after) {
        previous = before;
        current = after;
    });
    const std::uint64_t revision = renderScene.RenderOriginRevision();
    Require(renderScene.SetRenderOrigin(far) && renderScene.RenderOrigin() == far && renderScene.RenderOriginRevision() != revision,
        "The render origin did not move");
    Require(previous == DVec3{} && current == far, "A render origin listener was not told about the move");
    Require(!renderScene.SetRenderOrigin(far), "Setting the same render origin reported a move");
    renderScene.RemoveRenderOriginListener(listener);
    static_cast<void>(renderScene.SetRenderOrigin(DVec3{}));
    Require(current == far, "A removed render origin listener was still told about a move");
}

// Proxies hold camera-relative transforms: a mesh half a millimetre beside the camera's line of sight, ten thousand
// kilometres out, reaches the view matrix half a millimetre beside it.
void RunCameraRelativeSyncTest() {
    kb::scene::Scene scene;
    const DVec3 eye{ kFar + 0.3, 2.0, kFar + 0.7 };
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Far Camera" });
    scene.Transforms().SetLocalTranslation(camera, eye);
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Far Mesh" });
    scene.Transforms().SetLocalTranslation(mesh, eye + DVec3{ 0.0005, 0.0, 5.0 });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U, .materialAssetId = 7U });
    scene.Runtime().SynchronizeTransforms();

    RenderScene renderScene;
    Require(renderScene.SetRenderOrigin(renderScene.RenderOriginFor(eye, RenderOriginPolicy{})), "The test expects the render origin to follow the far camera");
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    const CameraRenderProxy* cameraProxy = renderScene.FindCameraByEntity(camera.Id());
    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(cameraProxy != nullptr && meshProxy != nullptr, "The far camera and mesh have no proxies");
    const kb::math::Vec3 relativeEye = kb::math::RelativeTo(eye, renderScene.RenderOrigin());
    Require(Near(kb::math::Vec3{ cameraProxy->desc.position[0], cameraProxy->desc.position[1], cameraProxy->desc.position[2] }, relativeEye, 1.0e-4F),
        "The far camera proxy is not relative to the render origin");
    Require(Near(ModelTranslation(meshProxy->desc.model), relativeEye + kb::math::Vec3{ 0.0005F, 0.0F, 5.0F }, 1.0e-4F),
        "The far mesh proxy is not relative to the render origin");
    const std::optional<SceneRenderCamera> view = renderScene.BuildPrimaryCamera(64U, 64U);
    Require(view.has_value(), "The far camera did not build a view");
    const kb::math::Vec3 inView = ViewPoint(view->view, ModelTranslation(meshProxy->desc.model));
    Require(Near(inView, kb::math::Vec3{ 0.0005F, 0.0F, 5.0F }, 1.0e-4F), "The far mesh lost its half millimetre in view space");

    // The camera moves on by two kilometres: the origin follows, and the next pull re-derives every proxy, moved or not.
    const DVec3 movedEye = eye + DVec3{ 2000.0, 0.0, 0.0 };
    scene.Transforms().SetLocalTranslation(camera, movedEye);
    scene.Runtime().SynchronizeTransforms();
    const DVec3 movedOrigin = renderScene.RenderOriginFor(movedEye, RenderOriginPolicy{});
    Require(movedOrigin != renderScene.RenderOrigin() && renderScene.SetRenderOrigin(movedOrigin), "The render origin did not follow the camera");
    synchronizer.PullTransforms(scene, renderScene);
    meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr && Near(ModelTranslation(meshProxy->desc.model),
        kb::math::RelativeTo(eye + DVec3{ 0.0005, 0.0, 5.0 }, movedOrigin), 1.0e-4F), "A rebase did not re-derive an unmoved mesh proxy");
    cameraProxy = renderScene.FindCameraByEntity(camera.Id());
    Require(cameraProxy != nullptr && Near(kb::math::Vec3{ cameraProxy->desc.position[0], cameraProxy->desc.position[1], cameraProxy->desc.position[2] },
        kb::math::RelativeTo(movedEye, movedOrigin), 1.0e-4F), "A rebase did not re-derive the moved camera proxy");
}

// The renderer moves the origin to a far camera by itself and publishes it with the visibility feedback, through
// which gameplay projects double-precision world points to the screen.
void RunRendererFollowsFarCameraTest() {
    kb::scene::Scene scene;
    const DVec3 eye{ kFar + 0.25, 3.0, -kFar - 0.5 };
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Far Camera" });
    scene.Transforms().SetLocalTranslation(camera, eye);
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .verticalFovDegrees = 90.0F, .primary = true });
    scene.Runtime().SynchronizeTransforms();

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "The far camera test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{ .id = RenderViewportId{ 1U }, .extent = RenderExtent{ 64U, 64U }, .viewportIndex = 0U },
        },
    };
    for (int frame = 0; frame < 2; ++frame) {
        Require(renderer.BeginFrame(), "The far camera test renderer did not begin a frame");
        Require(renderer.SubmitScene(scene, desc), "The far camera test renderer did not submit");
        renderer.EndFrame();
    }
    const DVec3 origin = kb::scene::SceneRenderFeedback::RenderOrigin(scene);
    Require(origin == DVec3{ std::round(eye.x / 1024.0) * 1024.0, 0.0, std::round(eye.z / 1024.0) * 1024.0 },
        "The renderer did not move the render origin to the far camera");

    // A point a centimetre to the right and half a metre ahead lands a pixel right of the centre (90 degree view,
    // 64 pixels: 32 pixels per unit of x/z).
    const kb::scene::SceneRenderScreenPoint centre = kb::scene::SceneRenderFeedback::WorldToScreen(scene, eye + DVec3{ 0.0, 0.0, 0.5 });
    const kb::scene::SceneRenderScreenPoint right = kb::scene::SceneRenderFeedback::WorldToScreen(scene, eye + DVec3{ 0.01, 0.0, 0.5 });
    Require(centre.valid && centre.onScreen && std::fabs(centre.screenX - 32.0F) <= 0.01F && std::fabs(centre.screenY - 32.0F) <= 0.01F,
        "A far point straight ahead of a far camera did not project to the screen centre");
    Require(right.valid && std::fabs(right.screenX - (32.0F + 0.64F)) <= 0.01F, "A centimetre beside a far camera's line of sight did not survive the projection");
    const kb::scene::SceneRenderCameraRay ray = kb::scene::SceneRenderFeedback::ScreenPointToRay(scene, 32.0F, 32.0F);
    Require(ray.valid && kb::math::Distance(ray.worldOrigin, eye) <= 1.0e-4, "A screen ray of a far camera did not start at the camera");

    // An editor-style world-space camera with its precise eye renders relative to the same origin.
    std::array<float, 16> view{};
    const bx::Vec3 at{ static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z) + 1.0F };
    const bx::Vec3 from{ static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z) };
    bx::mtxLookAt(view.data(), from, at);
    SceneRenderCamera overrideCamera{};
    overrideCamera.view = view;
    bx::mtxProj(overrideCamera.projection.data(), 90.0F, 1.0F, 0.1F, 1000.0F, bgfx::getCaps()->homogeneousDepth);
    RenderSceneSubmitDesc overrideDesc = desc;
    overrideDesc.cameraOverride = overrideCamera;
    overrideDesc.cameraOverrideEye = eye;
    Require(renderer.BeginFrame() && renderer.SubmitScene(scene, overrideDesc), "The world-space camera submit failed");
    renderer.EndFrame();
    const kb::scene::SceneRenderScreenPoint overrideRight = kb::scene::SceneRenderFeedback::WorldToScreen(scene, eye + DVec3{ 0.01, 0.0, 0.5 });
    Require(overrideRight.valid && std::fabs(overrideRight.screenX - (32.0F + 0.64F)) <= 0.01F,
        "A world-space camera with a precise eye lost precision far from the origin");
    renderer.Shutdown();
}

} // namespace

void RunLargeWorldRenderTests() {
    RunRenderOriginPolicyTest();
    RunCameraRelativeSyncTest();
    RunRendererFollowsFarCameraTest();
}

} // namespace kb::render::tests
