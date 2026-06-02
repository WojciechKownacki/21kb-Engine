#include "RendererTestSupport.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/scene/SceneRenderExtractor.hpp"

#include <array>

namespace kb::render::tests {
namespace {

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
        .worldPosition = kb::scene::Vec3{ x, y, z },
        .worldDirty = false,
    };
}

void RunSceneExtractorUsesOnlyEcsObjectsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Renderable",
        .transform = TransformAt(1.0F, 2.0F, 3.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 42U,
        .materialAssetId = 7U,
    });

    SceneRenderExtractor extractor;
    SceneRenderSnapshot withoutCamera;
    extractor.ExtractInto(scene, 1280, 720, withoutCamera);
    Require(!withoutCamera.camera.has_value(), "SceneRenderExtractor created a camera that is not present in ECS");
    Require(withoutCamera.meshes.size() == 1U, "SceneRenderExtractor did not extract the visible ECS mesh");
    Require(withoutCamera.meshes[0].entityId == mesh.Id(), "SceneRenderExtractor mesh entity id does not match ECS");
    Require(NearlyEqual(withoutCamera.meshes[0].model[12], 1.0F), "SceneRenderExtractor model matrix did not use ECS transform X");
    Require(NearlyEqual(withoutCamera.meshes[0].model[13], 2.0F), "SceneRenderExtractor model matrix did not use ECS transform Y");
    Require(NearlyEqual(withoutCamera.meshes[0].model[14], 3.0F), "SceneRenderExtractor model matrix did not use ECS transform Z");
}

void RunSceneExtractorCameraAndVisibilityTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Camera",
        .transform = TransformAt(0.0F, 2.0F, -6.0F),
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Hidden",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
        .visibility = kb::scene::VisibilityComponent{ .visible = false },
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{});

    SceneRenderExtractor extractor;
    SceneRenderSnapshot snapshot;
    extractor.ExtractInto(scene, 1280, 720, snapshot);
    Require(snapshot.camera.has_value(), "SceneRenderExtractor did not use the primary ECS camera");
    Require(snapshot.meshes.empty(), "SceneRenderExtractor extracted a hidden ECS mesh");
}

void RunSceneExtractorUsesCameraProjectionModeTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Ortho Camera",
        .transform = TransformAt(0.0F, 0.0F, -10.0F),
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{
        .projection = kb::scene::CameraProjection::Orthographic,
        .orthographicHeight = 8.0F,
        .nearClip = 0.1F,
        .farClip = 500.0F,
        .primary = true,
    });

    SceneRenderExtractor extractor;
    SceneRenderSnapshot snapshot;
    extractor.ExtractInto(scene, 1600, 800, snapshot);
    Require(snapshot.camera.has_value(), "SceneRenderExtractor did not extract orthographic camera");

    const std::array<float, 16>& projection = snapshot.camera->projection;
    Require(projection[0] > 0.0F, "Orthographic camera projection has invalid horizontal scale");
    Require(projection[5] > 0.0F, "Orthographic camera projection has invalid vertical scale");
    Require(projection[10] < 0.0F, "Orthographic camera projection does not use reverse-Z");
    Require(NearlyEqual(projection[15], 1.0F), "Orthographic camera projection was submitted as perspective");
}

void RunSceneExtractorExtractIntoReusesSnapshotAndSyncScratchTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity firstMesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Renderable A",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(firstMesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    const kb::scene::SceneEntity secondMesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Renderable B",
        .transform = TransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(secondMesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    SceneRenderExtractor extractor;
    extractor.Reserve(EcsRenderSceneSynchronizerReserveDesc{
        .meshProxies = 8U,
        .transformCacheEntries = 8U,
        .transformResolvingEntries = 8U,
    });
    SceneRenderSnapshot snapshot;
    extractor.ExtractInto(scene, 1280, 720, snapshot);
    const std::size_t meshCapacity = snapshot.meshes.capacity();
    Require(snapshot.meshes.size() == 2U, "SceneRenderExtractor ExtractInto did not extract both visible meshes");
    Require(extractor.SyncStats().meshSeenCapacity >= 8U, "SceneRenderExtractor did not preserve reserved sync scratch");
    Require(extractor.SyncStats().transformCacheCapacity >= 8U, "SceneRenderExtractor did not preserve reserved transform scratch");

    scene.Components().MeshRenderers().Remove(secondMesh);
    extractor.ExtractInto(scene, 1280, 720, snapshot);
    Require(snapshot.meshes.size() == 1U, "SceneRenderExtractor ExtractInto did not remove stale mesh proxy on reuse");
    Require(snapshot.meshes.capacity() >= meshCapacity, "SceneRenderExtractor ExtractInto released reusable snapshot mesh capacity");
}

} // namespace

void RunSceneRenderExtractorTests() {
    RunSceneExtractorUsesOnlyEcsObjectsTest();
    RunSceneExtractorCameraAndVisibilityTest();
    RunSceneExtractorUsesCameraProjectionModeTest();
    RunSceneExtractorExtractIntoReusesSnapshotAndSyncScratchTest();
}

} // namespace kb::render::tests
