#include "RendererTestSupport.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/SceneGBuffer.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/SceneDeferredLightingPass.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {
#if defined(_WIN32) && defined(KB_TEST_GRAPH_SHADERC_PATH)
struct Target {
    bgfx::TextureHandle color = BGFX_INVALID_HANDLE, depth = BGFX_INVALID_HANDLE, readback = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    Target() {
        color = bgfx::createTexture2D(64U, 64U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
        depth = bgfx::createTexture2D(64U, 64U, false, 1U, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        readback = bgfx::createTexture2D(64U, 64U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        const std::array textures{color, depth};
        framebuffer = bgfx::createFrameBuffer(2U, textures.data(), false);
        Require(bgfx::isValid(framebuffer) && bgfx::isValid(readback), "Light grid GPU target failed");
    }
    ~Target() {
        bgfx::destroy(framebuffer);
        bgfx::destroy(readback);
        bgfx::destroy(depth);
        bgfx::destroy(color);
    }
    std::array<int, 3> Read(bgfx::ViewId view = 240U) const {
        bgfx::blit(view, readback, 0U, 0U, color, 0U, 0U, 64U, 64U);
        std::vector<std::uint8_t> pixels(64U * 64U * 4U);
        const auto ready = bgfx::readTexture(readback, pixels.data());
        auto frame = bgfx::frame();
        for (auto guard = 0U; frame < ready && guard < 8U; ++guard) frame = bgfx::frame();
        Require(frame >= ready, "Light grid readback did not complete");
        constexpr auto center = (32U * 64U + 32U) * 4U;
        return {pixels[center], pixels[center + 1U], pixels[center + 2U]};
    }
};

RenderMaterialGraphProgramBinding CookGraph(SceneRenderer& renderer) {
    RenderMaterialGraphDocument graph{};
    graph.nodes.push_back(RenderMaterialGraphNode{.id = 1U, .kind = RenderMaterialGraphNodeKind::MaterialOutput});
    const auto compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{.assetId = 20U});
    Require(compiled.Succeeded(), "Light grid graph compile failed");
    const auto cache = std::filesystem::absolute("build/perf-release/light_grid_cook_tests");
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = {KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR};
    request.cacheRoot = cache.generic_string();
    request.pass = "BaseOpaque";
    request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;
    const std::array backends{RenderMaterialGraphShaderBackend::Dxbc};
    const auto cooked = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request);
    Require(cooked.Succeeded(), "Light grid graph cook failed");
    renderer.SetGraphShaderCacheRoot(cache.generic_string());
    return BuildRenderMaterialGraphProgramBinding(20U, 1U, compiled.shader, {}).binding;
}
#endif
}

void RunSceneLightGridGpuTests() {
#if defined(_WIN32) && defined(KB_TEST_GRAPH_SHADERC_PATH)
    const auto window = CreateWindowExW(0, L"STATIC", L"light-grid-headless", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "Light grid hidden surface failed");
    bgfx::Init init{};
    init.type = bgfx::RendererType::Direct3D11;
    init.platformData.nwh = window;
    init.resolution.width = init.resolution.height = 64U;
    Require(bgfx::init(init), "Light grid test requires real D3D11");
    {
        SceneRenderer renderer;
        Require(renderer.Initialize(), "Light grid scene renderer did not initialize");
        const std::array vertices{
            RenderStaticMeshVertexP3N3UV2{.x = -1.0F, .y = -1.0F, .z = 0.5F, .ny = 0.0F, .nz = 1.0F},
            RenderStaticMeshVertexP3N3UV2{.x = 1.0F, .y = -1.0F, .z = 0.5F, .ny = 0.0F, .nz = 1.0F},
            RenderStaticMeshVertexP3N3UV2{.x = 0.0F, .y = 1.0F, .z = 0.5F, .ny = 0.0F, .nz = 1.0F}};
        constexpr std::array<std::uint16_t, 3> indices{0U, 1U, 2U};
        const RenderMeshDesc mesh{.vertexData = vertices.data(), .vertexCount = 3U,
            .indices = indices.data(), .indexCount = 3U, .vertexFormat = RenderVertexFormat::P3N3UV2};
        const auto handle = renderer.Resources().RegisterMesh(mesh);
        Require(handle.IsValid(), "Light grid test mesh did not register");
        renderer.ResourceMap().BindMesh(1U, handle);
        RenderMaterialDesc material{};
        material.doubleSided = true;
        renderer.ResourceMap().BindMaterial(2U, renderer.Resources().RegisterMaterial(material));
        renderer.ResourceMap().BindMaterial(3U, renderer.Resources().RegisterMaterial(material, CookGraph(renderer)));
        RenderScene scene;
        MeshRenderProxyDesc proxy{.entityId = 10000U, .meshAssetId = 1U, .materialAssetId = 2U};
        proxy.model = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        proxy.color = {1.0F, 1.0F, 1.0F, 1.0F};
        proxy.layer = 3U;
        static_cast<void>(scene.UpsertMesh(proxy));
        SceneRenderCamera camera{};
        camera.view = {1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,1,1};
        camera.projection = proxy.model;
        SceneRenderLightingConfig lighting{};
        lighting.maxForwardLights = 32U;
        lighting.lightingPath = SceneRenderLightingPath::ClusteredForwardPlus;
        lighting.environmentMode = SceneRenderEnvironmentMode::Disabled;
        lighting.shadowsEnabled = false;
        lighting.clusterDimensions = {8U, 8U, 8U};
        Target first, second;
        SceneGBuffer firstGbuffer, secondGbuffer;
        Require(firstGbuffer.Ensure(64U, 64U) && secondGbuffer.Ensure(64U, 64U), "Light grid GBuffer setup failed");
        SceneDeferredLightingPass deferred;
        Require(deferred.Initialize(), "Light grid deferred renderer did not initialize");
        bool deferredMode = false;
        SceneRenderSubmitStats lightingStats{};
        const auto fill = [&](std::uint32_t count, std::array<float, 3> color) {
            for (std::uint64_t id = 1U; id <= 1024U; ++id) static_cast<void>(scene.RemoveLight(id));
            for (std::uint64_t id = 1U; id <= count; ++id) static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
                .entityId = id, .position = {0.0F, 0.0F, 1.0F}, .color = color,
                .intensity = 1.5F / count, .range = 4.0F}));
        };
        const auto submit = [&](const Target& target, bgfx::ViewId view, const SceneRenderCamera& activeCamera) {
            auto& gbuffer = &target == &first ? firstGbuffer : secondGbuffer;
            const auto meshView = static_cast<bgfx::ViewId>(deferredMode ? view * 2U : view);
            bgfx::setViewFrameBuffer(meshView, deferredMode ? gbuffer.FrameBuffer() : target.framebuffer);
            bgfx::setViewClear(meshView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000FFU, SceneDepthPolicy::ClearDepth());
            renderer.SubmitMeshPass(meshView, deferredMode ? MeshPassType::GBuffer : MeshPassType::BaseOpaque,
                scene, 64U, 64U, &activeCamera, {}, lighting);
            lightingStats = renderer.LastSubmitStats();
            if (deferredMode) {
                lightingStats = {};
                Require(deferred.Submit(SceneDeferredLightingPassDesc{
                    .viewId = static_cast<bgfx::ViewId>(meshView + 1U), .frameBuffer = target.framebuffer,
                    .gbuffer = &gbuffer, .renderScene = &scene, .camera = &activeCamera,
                    .lightingConfig = lighting, .extent = {64U, 64U}}, lightingStats),
                    "Light grid deferred production submit failed");
            }
            Require(!renderer.LastDiagnostics().HasErrors(), "Light grid production submit emitted an error");
            Require(renderer.LastSubmitStats().submittedMeshCount == 1U, "Light grid probe did not draw its mesh");
        };
        const auto capture = [&]() {
            submit(first, 0U, camera);
            bgfx::frame();
            renderer.TickFrame();
            deferred.EndFrame();
            return first.Read();
        };
        for (const auto useDeferred : {false, true}) {
          deferredMode = useDeferred;
          lighting.lightingPath = useDeferred ? SceneRenderLightingPath::Deferred : SceneRenderLightingPath::ClusteredForwardPlus;
          for (const auto materialId : {2U, 3U}) {
            proxy.materialAssetId = materialId;
            static_cast<void>(scene.UpsertMesh(proxy));
            fill(1U, {1.0F, 1.0F, 1.0F});
            const auto reference = capture();
            fill(512U, {1.0F, 1.0F, 1.0F});
            const auto many = capture();
            std::cout << "light_grid_probe deferred=" << deferredMode << " material=" << materialId << " reference=" << reference[0] << ',' << reference[1] << ',' << reference[2]
                << " many=" << many[0] << ',' << many[1] << ',' << many[2] << '\n';
            Require(lightingStats.submittedForwardLightCount == 512U,
                "Production renderer did not bind all 512 lights");
            Require(materialId != 3U || renderer.GraphMaterialGpuDrawCount() == 1U,
                "Light grid graph used a builtin fallback");
            for (std::size_t channel = 0U; channel < 3U; ++channel)
                Require(reference[channel] > 30 && reference[channel] < 240 &&
                    std::abs(reference[channel] - many[channel]) <= 2,
                    "512 actual lights did not reproduce reference radiance");
            for (std::uint64_t id = 33U; id <= 512U; ++id) static_cast<void>(scene.RemoveLight(id));
            const auto capped = capture();
            Require(many[0] > capped[0] * 6, "Spatial-light pixel test did not distinguish the 32-light cap");
            fill(512U, {1.0F, 0.0F, 0.0F});
            submit(first, 0U, camera);
            fill(512U, {0.0F, 0.0F, 1.0F});
            submit(second, 1U, camera);
            bgfx::frame();
            renderer.TickFrame();
            deferred.EndFrame();
            const auto red = first.Read(), blue = second.Read();
            Require(red[0] > 30 && red[2] < 3 && blue[2] > 30 && blue[0] < 3,
                "Two views overwrote each other's light atlas in the same frame");
            auto maskedCamera = camera;
            maskedCamera.cullingMask = 2U;
            submit(first, 0U, maskedCamera);
            bgfx::frame();
            renderer.TickFrame();
            deferred.EndFrame();
            const auto dark = first.Read();
            Require(dark[0] == 0 && dark[1] == 0 && dark[2] == 0, "Masked view retained stale light-grid data");
            for (const auto shift : {-0.03125F, 0.03125F}) {
                proxy.model[12] = shift;
                static_cast<void>(scene.UpsertMesh(proxy));
                camera.view[12] = -shift;
                const auto boundary = capture();
                Require(std::abs(boundary[2] - blue[2]) <= 2 && boundary[0] < 3,
                    "Moving camera/mesh across a light cell boundary lost illumination");
            }
            proxy.model[12] = camera.view[12] = 0.0F;
            static_cast<void>(scene.UpsertMesh(proxy));
            for (std::uint64_t id = 1U; id <= 512U; ++id) {
                auto light = scene.FindLightByEntity(id)->desc;
                light.position[0] = 1000.0F;
                static_cast<void>(scene.UpsertLight(light));
            }
            const auto movedAway = capture();
            Require(movedAway[0] == 0 && movedAway[1] == 0 && movedAway[2] == 0,
                "Moved lights left stale illumination outside the rebuilt grid");
            fill(1024U, {0.0F, 0.0F, 1.0F});
            const auto grown = capture();
            Require(lightingStats.submittedForwardLightCount == 1024U && std::abs(grown[2] - blue[2]) <= 2,
                "Light atlas growth lost lights or changed their radiance");
            std::cout << "light_grid_gpu deferred=" << deferredMode << " material=" << materialId << " reference=" << reference[0]
                << " lights512=" << many[0] << " capped32=" << capped[0] << " multi_view/boundary/motion/growth=PASS\n";
          }
        }
        deferred.Shutdown();
        renderer.Shutdown();
    }
    bgfx::frame();
    bgfx::shutdown();
    DestroyWindow(window);
#else
    std::cout << "light_grid_gpu: requires Windows/D3D11 and shaderc\n";
#endif
}
} // namespace kb::render::tests
