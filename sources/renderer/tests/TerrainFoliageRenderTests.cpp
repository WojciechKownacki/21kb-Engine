#include "RendererTestSupport.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {

constexpr std::uint64_t kPlantMesh = 101U;
constexpr std::uint64_t kPlantMaterial = 102U;

std::shared_ptr<const kb::assets::TerrainAsset> MakeTerrain(
    float size, float density, std::uint8_t paint, float fadeStart, float cull, std::uint32_t seed = 5U, bool castsShadow = true) {
    auto terrain = kb::assets::MakeFlatTerrainAsset(129U, size, size);
    terrain.foliageLayers = { kb::assets::TerrainFoliageLayer{
        .meshAssetId = kPlantMesh, .materialAssetId = kPlantMaterial, .density = density,
        .fadeStartDistance = fadeStart, .cullDistance = cull, .seed = seed, .castsShadow = castsShadow } };
    terrain.foliageMapWidth = terrain.foliageMapHeight = 512U;
    terrain.foliageDensity.assign(512U * 512U, paint);
    std::string error;
    Require(kb::assets::IsTerrainAssetValid(terrain, &error), "The foliage test terrain is invalid");
    return std::make_shared<const kb::assets::TerrainAsset>(std::move(terrain));
}

[[nodiscard]] std::array<float, 16> Identity() noexcept {
    return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

void Sync(RenderScene& scene, const std::shared_ptr<const kb::assets::TerrainAsset>& terrain) {
    const TerrainFoliageRenderProxyDesc desc{ .entityId = 7U, .model = Identity(), .terrain = terrain };
    scene.SyncTerrainFoliage(std::span<const TerrainFoliageRenderProxyDesc>{ &desc, 1U }, {});
}

// A camera at (x, height, z) looking straight down, or straight up when inverted.
[[nodiscard]] SceneRenderCamera Camera(float x, float height, float z, bool inverted, float fovDegrees = 60.0F) {
    SceneRenderCamera camera{};
    bx::mtxLookAt(camera.view.data(), bx::Vec3{ x, height, z }, bx::Vec3{ x, inverted ? height + 1.0F : height - 1.0F, z },
        bx::Vec3{ 0.0F, 0.0F, 1.0F });
    SceneDepthPolicy::MakePerspective(camera.projection.data(), fovDegrees, 1.0F, 0.1F, 5000.0F, SceneDepthPolicy::HomogeneousDepth());
    return camera;
}

const RenderBoundsSphere kPlantBounds{ .radius = 0.5F };

[[nodiscard]] MeshPipelineBuildResult BuildPass(
    const RenderScene& scene, const SceneRenderCamera& camera, MeshPassType pass = MeshPassType::BaseOpaque) {
    static RenderMeshResource mesh = [] {
        RenderMeshResource value{};
        value.indexCount = 3U;
        value.vertexCount = 3U;
        value.bounds = kPlantBounds;
        return value;
    }();
    static RenderMaterialResource material{};
    return MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = pass, .drawGroups = &scene.DrawGroups(), .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material, .camera = &camera, .resourceValidation = MeshPipelineResourceValidation::Skip });
}

[[nodiscard]] std::size_t DrawnInstances(const MeshPipelineBuildResult& result) {
    std::size_t count = 0U;
    for (const MeshDrawCommand& command : result.commands) count += command.instances.size();
    return count;
}

[[nodiscard]] std::size_t TotalInstances(const RenderScene& scene) {
    std::size_t count = 0U;
    for (const SceneRenderDrawGroup& group : scene.DrawGroups()) count += group.Instances().size();
    return count;
}

// The plants the camera should draw, told without the pass: inside the frustum and nearer than the draw distance.
[[nodiscard]] std::size_t ExpectedVisible(const RenderScene& scene, const SceneRenderCamera& camera, float cullDistance) {
    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(&camera);
    const std::array<float, 3> eye{
        -(camera.view[0] * camera.view[12] + camera.view[1] * camera.view[13] + camera.view[2] * camera.view[14]),
        -(camera.view[4] * camera.view[12] + camera.view[5] * camera.view[13] + camera.view[6] * camera.view[14]),
        -(camera.view[8] * camera.view[12] + camera.view[9] * camera.view[13] + camera.view[10] * camera.view[14]) };
    std::size_t count = 0U;
    for (const SceneRenderDrawGroup& group : scene.DrawGroups()) {
        for (const SceneRenderMeshInstance& instance : group.Instances()) {
            const float dx = instance.model[12] - eye[0];
            const float dy = instance.model[13] - eye[1];
            const float dz = instance.model[14] - eye[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) >= cullDistance) continue;
            if (MeshPipelineVisibility::IsInsideFrustum(frustum, MeshPipelineVisibility::TransformBounds(kPlantBounds, instance.model))) ++count;
        }
    }
    return count;
}

[[nodiscard]] float LargestScale(const SceneRenderCommandInstance& instance) noexcept {
    float largest = 0.0F;
    for (std::size_t column = 0U; column < 3U; ++column) {
        const float x = instance.model[column * 4U];
        const float y = instance.model[column * 4U + 1U];
        const float z = instance.model[column * 4U + 2U];
        largest = std::max(largest, std::sqrt(x * x + y * y + z * z));
    }
    return largest;
}

void RunCullingAndChunks() {
    // 256 x 256 m at one plant per square metre, in 8 x 8 chunks of 32 m.
    RenderScene scene;
    const auto terrain = MakeTerrain(256.0F, 1.0F, 255U, 40.0F, 60.0F);
    Sync(scene, terrain);
    Require(scene.TerrainFoliageChunkBuildCount() == 64U, "Every chunk of the terrain is made once");
    std::size_t foliageGroups = 0U;
    for (const SceneRenderDrawGroup& group : scene.DrawGroups()) {
        if (group.partitionKind != 3U) continue;
        ++foliageGroups;
        Require(group.cullDistance == 60.0F && group.fadeStartDistance == 40.0F && !group.visibilityClusters.empty(),
            "A foliage draw group lost its draw distance or its clusters");
    }
    Require(foliageGroups == 64U, "The foliage did not become one draw group per chunk");
    const std::size_t total = TotalInstances(scene);
    Require(total > 65000U && total <= 65536U, "A fully painted 256 m terrain should hold about 65536 plants");

    // (a) The plants in view are drawn, and only those.
    const SceneRenderCamera looking = Camera(0.0F, 30.0F, 0.0F, false);
    const MeshPipelineBuildResult seen = BuildPass(scene, looking);
    const std::size_t seenCount = DrawnInstances(seen);
    Require(seenCount > 500U && seenCount < total && seenCount == ExpectedVisible(scene, looking, 60.0F),
        "The plants in view are not the plants inside the frustum and the draw distance");
    Require(seen.stats.culledInstanceCount + seenCount == total, "Culled and drawn plants do not add up to every plant");

    // (b) The camera turned away draws nothing and counts every plant as culled.
    const MeshPipelineBuildResult turned = BuildPass(scene, Camera(0.0F, 30.0F, 0.0F, true));
    Require(DrawnInstances(turned) == 0U && turned.commands.empty() && turned.stats.culledInstanceCount == total,
        "A camera facing away from the foliage still draws plants");

    // (c) Chunks beyond the draw distance are not drawn.
    const MeshPipelineBuildResult beyondDistance = BuildPass(scene, Camera(0.0F, 100.0F, 0.0F, false));
    Require(DrawnInstances(beyondDistance) == 0U && beyondDistance.stats.culledInstanceCount == total, "Plants beyond the draw distance are still drawn");

    // Plants shrink to nothing between the fade start and the draw distance.
    const MeshPipelineBuildResult fading = BuildPass(scene, Camera(0.0F, 50.0F, 0.0F, false));
    Require(DrawnInstances(fading) > 0U, "The fade fixture draws nothing");
    float largest = 0.0F;
    for (const MeshDrawCommand& command : fading.commands) {
        for (const SceneRenderCommandInstance& instance : command.instances) largest = std::max(largest, LargestScale(instance));
    }
    Require(largest < 0.61F, "Plants 50 m away were not shrunk by the fade");
    float nearest = 0.0F;
    for (const MeshDrawCommand& command : seen.commands) {
        for (const SceneRenderCommandInstance& instance : command.instances) nearest = std::max(nearest, LargestScale(instance));
    }
    Require(nearest >= 0.8F, "Plants close to the camera were shrunk");
    // The shadow pass measures from the light, not the viewer: it draws the plants whole, whatever their distance from the camera.
    const MeshPipelineBuildResult shadows = BuildPass(scene, Camera(0.0F, 100.0F, 0.0F, false), MeshPassType::ShadowDepth);
    Require(DrawnInstances(shadows) > 0U, "The foliage casts no shadow");
    for (const MeshDrawCommand& command : shadows.commands) {
        for (const SceneRenderCommandInstance& instance : command.instances) {
            Require(LargestScale(instance) >= 0.79F, "A plant was shrunk in the shadow pass");
        }
    }
    std::cout << "terrain_foliage_culling instances=" << total << " drawn=" << seenCount << " turned_away=" << DrawnInstances(turned)
              << " beyond_distance=" << DrawnInstances(beyondDistance) << " chunk_groups=" << foliageGroups << std::endl;

    // (e) Painting in one chunk makes only that chunk again.
    std::vector<std::uint64_t> cacheIdsBefore;
    for (const SceneRenderDrawGroup& group : scene.DrawGroups()) cacheIdsBefore.push_back(group.cacheId);
    const std::uint64_t buildsBefore = scene.TerrainFoliageChunkBuildCount();
    Sync(scene, terrain);
    Require(scene.TerrainFoliageChunkBuildCount() == buildsBefore, "Unchanged foliage data was made again");
    auto edited = *terrain;
    // The chunk at (3, 4): terrain-space metres 96..128 by 128..160, cleared well inside its own area.
    for (std::uint32_t z = 280U; z < 300U; ++z) {
        for (std::uint32_t x = 215U; x < 235U; ++x) edited.foliageDensity[static_cast<std::size_t>(z) * 512U + x] = 0U;
    }
    Sync(scene, std::make_shared<const kb::assets::TerrainAsset>(std::move(edited)));
    Require(scene.TerrainFoliageChunkBuildCount() == buildsBefore + 1U, "A change inside one chunk made other chunks again");
    std::size_t changedGroups = 0U;
    const std::vector<SceneRenderDrawGroup>& after = scene.DrawGroups();
    Require(after.size() == cacheIdsBefore.size(), "Painting one chunk changed the number of draw groups");
    for (std::size_t index = 0U; index < after.size(); ++index) {
        if (after[index].cacheId != cacheIdsBefore[index]) ++changedGroups;
    }
    Require(changedGroups == 1U && TotalInstances(scene) < total, "Only the painted chunk should have new instances");
    std::cout << "terrain_foliage_rebuild chunks_made_for_one_painted_chunk=" << scene.TerrainFoliageChunkBuildCount() - buildsBefore << std::endl;
}

void RunMillionPlants() {
    // 1024 x 1024 m at one plant per square metre: 1 048 575 plants in 32 x 32 chunks.
    RenderScene scene;
    const auto terrain = MakeTerrain(1024.0F, 1.0F, 255U, 4000.0F, 5000.0F, 3U);
    Sync(scene, terrain);
    const std::size_t total = TotalInstances(scene);
    Require(total >= 1'000'000U, "The large terrain holds fewer than a million plants");
    // A camera over one quarter of the terrain sees part of the chunks.
    const SceneRenderCamera camera = Camera(-256.0F, 500.0F, -256.0F, false);
    const MeshPipelineBuildResult result = BuildPass(scene, camera);
    const std::size_t drawn = DrawnInstances(result);
    std::unordered_set<std::uint64_t> drawnIds;
    drawnIds.reserve(drawn);
    for (const MeshDrawCommand& command : result.commands) {
        for (const SceneRenderCommandInstance& instance : command.instances) drawnIds.insert(instance.entityId);
    }
    std::size_t visibleChunks = 0U;
    for (const SceneRenderDrawGroup& group : scene.DrawGroups()) {
        const auto instances = group.Instances();
        const bool visible = std::any_of(instances.begin(), instances.end(),
            [&drawnIds](const SceneRenderMeshInstance& instance) { return drawnIds.contains(instance.entityId); });
        if (visible) ++visibleChunks;
    }
    Require(drawn > 100000U && visibleChunks > 0U && drawn + result.stats.culledInstanceCount == total,
        "The million plants did not split into drawn and culled ones");
    Require(result.commands.size() <= visibleChunks, "More draw calls than visible chunks");
    std::cout << "terrain_foliage_scale instances=" << total << " drawn=" << drawn << " visible_chunks=" << visibleChunks
              << " draw_calls=" << result.commands.size() << std::endl;
}

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 64U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 64U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

// A terrain asset on disk with a foliage layer, drawn by the renderer as the game does: the scene holds only the terrain.
void RunFoliageThroughRenderer() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_terrain_foliage";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "The foliage test could not create its folder");
    {
        std::ofstream plant(root / "plant.obj");
        plant << "v -0.5 0.1 -0.5\nv 0.5 0.1 -0.5\nv 0.0 0.1 0.5\nf 1 2 3\n";
        Require(plant.good(), "The foliage test could not write its plant mesh");
    }
    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "The foliage test could not register the mesh loader");
    Require(manager.Mounts().Mount("Game", root), "The foliage test could not mount its folder");
    Require(manager.DiscoverMountedAssets() == 1U, "The foliage test did not discover the plant mesh");
    const kb::assets::AssetMetadata* plant = manager.Registry().FindByPath("/Game/plant.obj");
    Require(plant != nullptr, "The foliage test lost the plant mesh");
    auto terrain = kb::assets::MakeFlatTerrainAsset(17U, 16.0F, 16.0F);
    terrain.foliageLayers = { kb::assets::TerrainFoliageLayer{ .meshAssetId = plant->id.value, .density = 1.0F, .seed = 2U } };
    terrain.foliageMapWidth = terrain.foliageMapHeight = 64U;
    terrain.foliageDensity.assign(64U * 64U, 255U);
    std::string message;
    Require(kb::assets::TerrainAssetIO::Save(root / "Ground.kbterrain", terrain, &message), "The foliage test could not save its terrain");
    static_cast<void>(manager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* ground = manager.Registry().FindByPath("/Game/Ground.kbterrain");
    Require(ground != nullptr && ground->type == "RenderMesh", "The foliage test did not discover its terrain");
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Ground" });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = ground->id.value });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "The foliage test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{ .id = RenderViewportId{ 1U }, .extent = RenderExtent{ 64U, 64U }, .viewportIndex = 0U },
        },
        .cameraOverride = Camera(0.0F, 10.0F, 0.0F, false),
    };
    std::uint32_t drawn = 0U;
    for (int frame = 0; frame < 8 && drawn < 100U; ++frame) {
        Require(renderer.BeginFrame(), "The foliage test renderer did not begin a frame");
        Require(renderer.SubmitScene(scene, desc), "The foliage test renderer did not submit the scene");
        drawn = renderer.LastSceneSubmitStats().submittedMeshCount;
        renderer.EndFrame();
    }
    // Some 130 plants lie under a camera 10 m above a 16 m terrain, besides the sections of the terrain itself.
    Require(drawn >= 100U, "The renderer did not draw the foliage of a terrain asset");
    std::cout << "terrain_foliage_renderer drawn=" << drawn << std::endl;
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

#if defined(_WIN32)
[[nodiscard]] std::size_t GreenPixels(const std::vector<std::uint8_t>& pixels) {
    std::size_t count = 0U;
    for (std::size_t index = 0U; index + 3U < pixels.size(); index += 4U) {
        // The read-back target is BGRA or RGBA; green sits in the middle either way.
        if (pixels[index + 1U] >= 128U && pixels[index] < 64U && pixels[index + 2U] < 64U) ++count;
    }
    return count;
}

void RunFoliageOnScreen() {
    const auto window = CreateWindowExW(0, L"STATIC", L"terrain-foliage-headless", WS_OVERLAPPEDWINDOW,
        0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "Foliage hidden surface failed");
    bgfx::Init init{};
    init.type = bgfx::RendererType::Direct3D11;
    init.profile = true;
    init.swapChain.nwh = window;
    init.swapChain.width = init.swapChain.height = 128U;
    Require(bgfx::init(init), "Foliage rendering requires real D3D11");
    {
        SceneRenderer renderer;
        Require(renderer.Initialize(), "Foliage renderer initialization failed");
        // A leaf lying flat above the ground: seen from above, at any turn.
        const std::array vertices{
            RenderStaticMeshVertexP3N3UV2{ .x = -0.5F, .y = 0.1F, .z = -0.5F },
            RenderStaticMeshVertexP3N3UV2{ .x = 0.5F, .y = 0.1F, .z = -0.5F },
            RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 0.1F, .z = 0.5F } };
        const std::array<std::uint16_t, 3> indices{ 0U, 1U, 2U };
        const RenderMeshDesc mesh{ .vertexData = vertices.data(), .vertexCount = 3U, .indices = indices.data(), .indexCount = 3U,
            .vertexFormat = RenderVertexFormat::P3N3UV2, .bounds = { .center = { 0.0F, 0.1F, 0.0F }, .radius = 0.8F },
            .doubleSided = true };
        const auto meshHandle = renderer.Resources().RegisterMesh(mesh);
        Require(meshHandle.IsValid(), "Foliage mesh registration failed");
        renderer.ResourceMap().BindMesh(kPlantMesh, meshHandle);
        RenderMaterialDesc materialDesc{};
        materialDesc.baseColor[0] = materialDesc.baseColor[1] = materialDesc.baseColor[2] = 0.0F;
        materialDesc.emissiveColor[1] = 1.0F;
        const auto material = renderer.Resources().RegisterMaterial(materialDesc);
        renderer.ResourceMap().BindMaterial(kPlantMaterial, material);

        RenderScene scene;
        Sync(scene, MakeTerrain(32.0F, 1.0F, 255U, 40.0F, 60.0F));
        const auto color = bgfx::createTexture2D(128U, 128U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
        const auto depth = bgfx::createTexture2D(128U, 128U, false, 1U, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        const auto readback = bgfx::createTexture2D(128U, 128U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        const std::array attachments{ color, depth };
        const auto target = bgfx::createFrameBuffer(2U, attachments.data(), false);
        Require(bgfx::isValid(target) && bgfx::isValid(readback), "Foliage render target failed");
        SceneRenderLightingConfig lighting{};
        lighting.shadowsEnabled = false;
        lighting.environmentMode = SceneRenderEnvironmentMode::Disabled;
        const SceneGpuDrivenFeatureSupport cpuCulling{};

        const auto capture = [&](const SceneRenderCamera& camera) {
            bgfx::setViewFrameBuffer(0U, target);
            bgfx::setViewClear(0U, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000FFU, SceneDepthPolicy::ClearDepth());
            renderer.SubmitMeshPass(0U, MeshPassType::BaseOpaque, scene, 128U, 128U, &camera, {}, lighting, nullptr, {}, &cpuCulling);
            Require(!renderer.LastDiagnostics().HasErrors(), "Foliage rendering reported errors");
            bgfx::blit(240U, bgfx::TextureRegion{ .handle = readback }, bgfx::TextureRegion{ .handle = color, .width = 128U, .height = 128U });
            std::vector<std::uint8_t> pixels(128U * 128U * 4U);
            const auto ready = bgfx::read(bgfx::TextureRegion{ .handle = readback }, pixels.data());
            auto frame = bgfx::frame();
            for (auto guard = 0U; frame < ready && guard < 8U; ++guard) frame = bgfx::frame();
            Require(frame >= ready, "Foliage read-back timed out");
            renderer.TickFrame();
            return pixels;
        };

        // (a) Over the plants: green pixels, drawn in few draw calls.
        const auto lookingDown = capture(Camera(0.0F, 10.0F, 0.0F, false));
        const SceneRenderSubmitStats seen = renderer.LastSubmitStats();
        const std::size_t green = GreenPixels(lookingDown);
        Require(green > 800U, "The plants are not on the screen");
        Require(seen.submittedMeshCount > 0U && seen.submittedDrawCallCount >= 1U &&
            seen.submittedDrawCallCount <= seen.submittedMeshCount, "The plants were not drawn instanced");
        // (b) Looking away from them: nothing drawn.
        const auto lookingUp = capture(Camera(0.0F, 10.0F, 0.0F, true));
        const SceneRenderSubmitStats away = renderer.LastSubmitStats();
        Require(GreenPixels(lookingUp) == 0U && away.submittedMeshCount == 0U && away.submittedDrawCallCount == 0U,
            "Plants were drawn with the camera turned away");
        // (c) Beyond the draw distance: nothing drawn.
        const auto distant = capture(Camera(0.0F, 100.0F, 0.0F, false));
        const SceneRenderSubmitStats beyond = renderer.LastSubmitStats();
        Require(GreenPixels(distant) == 0U && beyond.submittedMeshCount == 0U && beyond.submittedDrawCallCount == 0U,
            "Plants were drawn beyond the draw distance");
        // (d) A large terrain, all in view: the draw calls stay at most one per visible chunk and layer.
        Sync(scene, MakeTerrain(1024.0F, 1.0F, 255U, 4000.0F, 5000.0F, 3U));
        static_cast<void>(capture(Camera(0.0F, 1200.0F, 0.0F, false, 90.0F)));
        const SceneRenderSubmitStats crowded = renderer.LastSubmitStats();
        Require(crowded.droppedInstanceCount == 0U && crowded.submittedMeshCount > 1000000U &&
            crowded.submittedDrawCallCount >= 1U && crowded.submittedDrawCallCount <= 32U * 32U,
            "A crowded terrain needs more draw calls than it has chunks, or dropped plants");
        std::cout << "terrain_foliage_gpu_crowded drawn=" << crowded.submittedMeshCount << " visible_chunks=1024 draw_calls="
                  << crowded.submittedDrawCallCount << " dropped=" << crowded.droppedInstanceCount << std::endl;
        // Shadows: only the plants within the shadow distance of the viewer, and inside the cascade, are drawn into it.
        const auto resolvedMesh = renderer.Resources().FindMesh(meshHandle);
        Require(resolvedMesh != nullptr, "The foliage test lost its mesh resource");
        static RenderMaterialResource shadowMaterial{};
        const auto shadowPass = [&](const RenderScene& drawn, const SceneRenderCamera& cascade) {
            return MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
                .pass = MeshPassType::ShadowDepth, .drawGroups = &drawn.DrawGroups(), .resolvedMeshResource = resolvedMesh,
                .resolvedMaterialResource = &shadowMaterial, .camera = &cascade,
                .resourceValidation = MeshPipelineResourceValidation::Skip });
        };
        const LightRenderProxyDesc sun{ .entityId = 9U, .kind = RenderLightKind::Directional,
            .rotation = { 0.7071068F, 0.0F, 0.0F, 0.7071068F }, .castsShadow = true, .shadowDistance = 100.0F };
        static_cast<void>(scene.UpsertLight(sun));
        const std::array<float, 3> viewer{ -480.0F, 20.0F, -480.0F };
        SceneRenderLightingConfig shadowLighting{};
        shadowLighting.shadowsEnabled = true;
        const DirectionalShadowSetup setup = DirectionalShadowPassPlanner{}.Build(
            scene, renderer.Resources(), renderer.ResourceMap(), shadowLighting, BGFX_INVALID_HANDLE, 0xFFFFFFFFU, &viewer, 4U);
        Require(setup.valid && setup.binding.cascadeCount >= 1U, "The sun over a million plants planned no cascade");
        std::size_t inRange = 0U;
        std::size_t totalPlants = 0U;
        for (const SceneRenderDrawGroup& group : scene.DrawGroups()) {
            for (const SceneRenderMeshInstance& instance : group.Instances()) {
                ++totalPlants;
                const float dx = instance.model[12] - viewer[0];
                const float dy = instance.model[13] - viewer[1];
                const float dz = instance.model[14] - viewer[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) < 100.0F) ++inRange;
            }
        }
        std::size_t shadowDrawnMost = 0U;
        std::string perCascade;
        for (std::uint32_t cascade = 0U; cascade < setup.binding.cascadeCount; ++cascade) {
            const SceneRenderCamera& cascadeCamera = setup.cascadeCameras[cascade];
            const MeshPipelineBuildResult shadows = shadowPass(scene, cascadeCamera);
            const std::size_t drawnInShadow = DrawnInstances(shadows);
            const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(&cascadeCamera);
            std::size_t expected = 0U;
            for (const SceneRenderDrawGroup& group : scene.DrawGroups()) {
                for (const SceneRenderMeshInstance& instance : group.Instances()) {
                    const float dx = instance.model[12] - viewer[0];
                    const float dy = instance.model[13] - viewer[1];
                    const float dz = instance.model[14] - viewer[2];
                    if (std::sqrt(dx * dx + dy * dy + dz * dz) < 100.0F &&
                        MeshPipelineVisibility::IsInsideFrustum(frustum, MeshPipelineVisibility::TransformBounds(resolvedMesh->bounds, instance.model))) {
                        ++expected;
                    }
                }
            }
            Require(drawnInShadow == expected && drawnInShadow <= inRange,
                "A cascade drew plants beyond the shadow distance or outside its volume");
            shadowDrawnMost = std::max(shadowDrawnMost, drawnInShadow);
            perCascade += " " + std::to_string(drawnInShadow);
        }
        Require(inRange > 0U && inRange < totalPlants / 10U && shadowDrawnMost > 0U, "The shadow range does not hold a part of the plants");
        std::cout << "terrain_foliage_shadows plants=" << totalPlants << " within_shadow_range=" << inRange
                  << " most_in_one_cascade=" << shadowDrawnMost << " drawn_per_cascade:" << perCascade << std::endl;

        // A layer that does not cast shadows adds nothing to a shadow pass, and no cascade is planned for it.
        RenderScene unshadowed;
        Sync(unshadowed, MakeTerrain(256.0F, 1.0F, 255U, 4000.0F, 5000.0F, 3U, false));
        static_cast<void>(unshadowed.UpsertLight(sun));
        const std::array<float, 3> nearby{ -100.0F, 20.0F, -100.0F };
        const DirectionalShadowSetup none = DirectionalShadowPassPlanner{}.Build(
            unshadowed, renderer.Resources(), renderer.ResourceMap(), shadowLighting, BGFX_INVALID_HANDLE, 0xFFFFFFFFU, &nearby, 4U);
        Require(!none.valid && none.casterCount == 0U, "Plants that cast no shadow still planned a cascade");
        for (std::uint32_t cascade = 0U; cascade < setup.binding.cascadeCount; ++cascade) {
            Require(DrawnInstances(shadowPass(unshadowed, setup.cascadeCameras[cascade])) == 0U, "A layer without shadows drew into a shadow pass");
        }
        static_cast<void>(capture(Camera(-100.0F, 10.0F, -100.0F, false)));
        Require(renderer.LastSubmitStats().submittedMeshCount > 0U, "A layer without shadows stopped drawing in the camera pass");
        std::cout << "terrain_foliage_shadows_off plants=" << TotalInstances(unshadowed) << " drawn_in_shadows=0" << std::endl;
        std::cout << "terrain_foliage_gpu green_pixels=" << green << " drawn=" << seen.submittedMeshCount
                  << " draw_calls=" << seen.submittedDrawCallCount << " turned_away_drawn=" << away.submittedMeshCount
                  << " beyond_distance_drawn=" << beyond.submittedMeshCount << std::endl;
        bgfx::destroy(target);
        bgfx::destroy(readback);
        bgfx::destroy(depth);
        bgfx::destroy(color);
        renderer.Shutdown();
    }
    bgfx::shutdown();
    DestroyWindow(window);
}
#endif

} // namespace

void RunTerrainFoliageRenderTests() {
    RunCullingAndChunks();
    RunMillionPlants();
    RunFoliageThroughRenderer();
#if defined(_WIN32)
    RunFoliageOnScreen();
#else
    Require(false, "Foliage rendering acceptance needs a configured graphics surface");
#endif
}

} // namespace kb::render::tests
