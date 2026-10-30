#include "RendererTestSupport.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/scene/SceneGpuDrivenCullingPass.hpp"
#include "kb/render/scene/RenderInstanceBuffer.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"
#include <set>

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render::tests {
void RunGpuFrustumCullingTests() {
#if defined(_WIN32)
    const auto window = CreateWindowExW(0, L"STATIC", L"frustum-headless", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "GPU frustum hidden surface failed");
    bgfx::Init init{};
    init.type = bgfx::RendererType::Direct3D11;
    init.profile = true;
    init.swapChain.nwh = window;
    init.swapChain.width = init.swapChain.height = 64U;
    Require(bgfx::init(init), "GPU frustum requires real D3D11");
    bgfx::setDebug(BGFX_DEBUG_PROFILER);
    {
        SceneRenderer renderer;
        Require(renderer.Initialize(), "GPU frustum renderer initialization failed");
        Require((bgfx::getCaps()->supported & (BGFX_CAPS_COMPUTE | BGFX_CAPS_DRAW_INDIRECT)) ==
            (BGFX_CAPS_COMPUTE | BGFX_CAPS_DRAW_INDIRECT), "GPU frustum requires compute and indirect draws");
        const std::array vertices{
            RenderStaticMeshVertexP3N3UV2{.x=-0.04F, .y=-0.04F, .nz=1.0F},
            RenderStaticMeshVertexP3N3UV2{.x=0.04F, .y=-0.04F, .nz=1.0F},
            RenderStaticMeshVertexP3N3UV2{.x=0.0F, .y=0.04F, .nz=1.0F}};
        const std::array<std::uint16_t,3> indices{0U,1U,2U};
        const RenderBoundsSphere bounds{.radius=0.06F};
        const RenderMeshletDesc meshlet{.indexCount=3U,.vertexCount=3U,.bounds=bounds};
        const RenderMeshDesc mesh{.vertexData=vertices.data(),.vertexCount=3U,.indices=indices.data(),
            .indexCount=3U,.vertexFormat=RenderVertexFormat::P3N3UV2,.bounds=bounds,
            .gpuDriven={.meshlets=&meshlet,.meshletCount=1U,.allowMeshletCulling=false},.doubleSided=true};
        const auto handle = renderer.Resources().RegisterMesh(mesh);
        Require(handle.IsValid(), "GPU frustum mesh failed");
        renderer.ResourceMap().BindMesh(1U,handle);
        RenderMaterialDesc materialDesc{};
        materialDesc.emissiveColor[0]=0.25F; materialDesc.emissiveColor[1]=0.5F; materialDesc.emissiveColor[2]=0.75F;
        const auto material = renderer.Resources().RegisterMaterial(materialDesc);
        renderer.ResourceMap().BindMaterial(2U,material);
        RenderScene scene;
        // Dense static candidates extend far outside the view. Special probes
        // intersect each clip boundary, including the zero-to-one near plane.
        for (std::uint32_t i=0U;i<32768U;++i) {
            MeshRenderProxyDesc proxy{.entityId=i+1U,.meshAssetId=1U,.materialAssetId=2U};
            proxy.model={1,0,0,0,0,1,0,0,0,0,1,0,
                (static_cast<float>(i%256U)-128.0F)*0.08F,
                (static_cast<float>(i/256U)-64.0F)*0.08F,0.5F,1};
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        for (std::uint32_t i=0U;i<6U;++i) {
            MeshRenderProxyDesc proxy{.entityId=40000U+i,.meshAssetId=1U,.materialAssetId=2U};
            proxy.model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0.5F,1};
            proxy.model[12U+i/2U]=i/2U==2U?static_cast<float>(i%2U):(i%2U==0U?-1.0F:1.0F);
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        {
            SceneGpuDrivenCullingPass culling;
            Require(culling.Initialize(), "GPU frustum compute programs failed");
            MeshDrawCommand command;
            command.indexCount = 3U;
            for (std::uint32_t i=0U;i<26U;++i) {
                SceneRenderMeshInstance instance{};
                instance.model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0.5F,1};
                const auto axis=(i/4U)%3U;
                const float boundary=axis==2U?static_cast<float>((i/2U)%2U):((i/2U)%2U==0U?-1.0F:1.0F);
                instance.worldBounds={.center={0,0,0.5F},.radius=i==25U?0.0F:0.06F};
                instance.worldBounds.center[axis]=boundary+(i%2U==0U?-0.061F:0.061F);
                instance.color={static_cast<float>(i+1U),1,1,1};
                command.instances.push_back(instance);
            }
            double dispatchCpu=0.0,syncCpu=0.0;
            for (int step=0;step<8;++step) {
                std::array<SceneGpuCulledDraw,2> draws;
                std::array<std::array<std::uint32_t,8>,2> args{};
                std::array<std::vector<RenderInstanceData>,2> output;
                std::array<std::set<int>,2> expected;
                std::uint32_t ready=0U;
                for (std::uint16_t v=0U;v<2U;++v) {
                    SceneRenderCamera probe{};
                    probe.view=probe.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                    probe.view[12]=static_cast<float>(step-4)*0.05F+v*0.31F;
                    probe.view[14]=static_cast<float>(step%3-1)*0.04F;
                    probe.projection[10]=-1.0F; probe.projection[14]=1.0F; // reverse-Z
                    probe.temporalProjectionOffset={0.001F,-0.001F};
                    probe.projection[8]+=probe.temporalProjectionOffset[0];
                    probe.projection[9]+=probe.temporalProjectionOffset[1];
                    probe.cullingGuardBand={0.01F,0.01F};
                    const auto frustum=MeshPipelineVisibility::BuildFrustum(&probe);
                    for (const auto& i:command.instances) if (MeshPipelineVisibility::IsInsideFrustum(frustum,i.worldBounds))
                        expected[v].insert(static_cast<int>(i.color[0]));
                    const auto dispatchBegin=std::chrono::steady_clock::now();
                    draws[v]=culling.Compact(v,command,&probe);
                    dispatchCpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-dispatchBegin).count();
                    Require(draws[v].IsValid(),"GPU frustum compact allocation failed");
                    output[v].resize(command.instances.size());
                    ready=std::max(ready,bgfx::read(bgfx::BufferRegion{.handle=draws[v].indirect,.size=32U},args[v].data()));
                    ready=std::max(ready,bgfx::read(bgfx::BufferRegion{.handle=draws[v].instances,
                        .size=static_cast<std::uint32_t>(output[v].size()*sizeof(RenderInstanceData))},output[v].data()));
                }
                const auto syncBegin=std::chrono::steady_clock::now();
                auto frame=bgfx::frame();
                for(auto guard=0U;frame<ready&&guard<8U;++guard) frame=bgfx::frame();
                syncCpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-syncBegin).count();
                Require(frame>=ready,"GPU frustum argument readback timed out");
                for (std::size_t v=0U;v<2U;++v) {
                    Require(args[v][0]==3U&&args[v][1]==expected[v].size(),"GPU visibility count differs at frustum boundary");
                    std::set<int> actual;
                    for(std::uint32_t i=0U;i<args[v][1];++i) actual.insert(static_cast<int>(output[v][i].color[0]));
                    Require(actual==expected[v],"GPU compacted identities differ across cameras");
                }
                culling.EndFrame();
            }
            std::cout<<"gpu_frustum_boundary_readback cameras=16 reverse_z=1 jitter_guard=1 identity_mismatches=0 dispatch_cpu_ms="<<dispatchCpu/16.0<<" sync_readback_ms="<<syncCpu/8.0<<"\n";
        }
        const auto color=bgfx::createTexture2D(64U,64U,false,1U,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|BGFX_TEXTURE_BLIT_DST);
        const auto depth=bgfx::createTexture2D(64U,64U,false,1U,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT);
        const auto readback=bgfx::createTexture2D(64U,64U,false,1U,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        const std::array attachments{color,depth};
        const auto target=bgfx::createFrameBuffer(2U,attachments.data(),false);
        Require(bgfx::isValid(target)&&bgfx::isValid(readback),"GPU frustum target failed");
        SceneRenderCamera camera{};
        camera.view=camera.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        SceneRenderLightingConfig lighting{};
        lighting.shadowsEnabled=false;
        lighting.environmentMode=SceneRenderEnvironmentMode::Disabled;
        const SceneGpuDrivenFeatureSupport gpu{true,true,false,true},cpu{};
        double cpuSubmit=0.0,gpuSubmit=0.0,cpuFrame=0.0,gpuFrame=0.0;
        std::uint64_t gpuUpload=0U,reused=0U;
        double cpuGpuTime=0.0,gpuGpuTime=0.0;
        std::uint32_t cpuGpuSamples=0U,gpuGpuSamples=0U;
        std::set<std::uint32_t> gpuFrames;

        const auto capture=[&](bool useGpu, bool timed) {
            bgfx::setViewFrameBuffer(0U,target);
            bgfx::setViewClear(0U,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,0x000000FFU,SceneDepthPolicy::ClearDepth());
            const auto begin=std::chrono::steady_clock::now();
            const auto& support=useGpu?gpu:cpu;
            renderer.SubmitMeshPass(0U,MeshPassType::BaseOpaque,scene,64U,64U,&camera,{},lighting,nullptr,{},&support);
            const auto submitted=std::chrono::steady_clock::now();
            const auto stats=renderer.LastSubmitStats();
            Require(!renderer.LastDiagnostics().HasErrors(),"GPU frustum production diagnostics failed");
            if(useGpu) {
                Require(stats.gpuCullingDispatchCount==2U,"GPU frustum did not execute clear and compact dispatches");
                gpuUpload+=stats.gpuDrivenUploadBytes;
                reused+=stats.meshCommandReuseCount;
            }
            bgfx::blit(240U,bgfx::TextureRegion{.handle=readback},bgfx::TextureRegion{.handle=color,.width=64U,.height=64U});
            std::vector<std::uint8_t> pixels(64U*64U*4U);
            const auto ready=bgfx::read(bgfx::TextureRegion{.handle=readback},pixels.data());
            const auto collectGpu=[&]() {
                const auto* timings=bgfx::getStats();
                if(timings->gpuTimerFreq<=0) return;
                for(std::uint16_t i=0U;i<timings->numViews;++i) {
                    const auto& view=timings->viewStats[i];
                    if(view.view==0U&&view.gpuTimeEnd>view.gpuTimeBegin&&gpuFrames.insert(view.gpuFrameNum).second&&timed) {
                        (useGpu?gpuGpuTime:cpuGpuTime)+=1000.0*static_cast<double>(view.gpuTimeEnd-view.gpuTimeBegin)/static_cast<double>(timings->gpuTimerFreq);
                        ++(useGpu?gpuGpuSamples:cpuGpuSamples);
                    }
                }
            };
            auto frame=bgfx::frame(); collectGpu();
            for(auto guard=0U;frame<ready&&guard<8U;++guard) { frame=bgfx::frame(); collectGpu(); }
            Require(frame>=ready,"GPU frustum readback timed out");
            const auto end=std::chrono::steady_clock::now();
            renderer.TickFrame();
            if(timed) {
                (useGpu?gpuSubmit:cpuSubmit)+=std::chrono::duration<double,std::milli>(submitted-begin).count();
                (useGpu?gpuFrame:cpuFrame)+=std::chrono::duration<double,std::milli>(end-submitted).count();
            }
            return pixels;
        };
        // Keep the modes separate so camera-only GPU reuse remains observable.
        std::vector<std::vector<std::uint8_t>> reference;
        for(int mode=0;mode<2;++mode) {
            for(int step=0;step<16;++step) {
                camera.view[12]=static_cast<float>(step-8)*0.031F;
                camera.view[13]=static_cast<float>(step%5-2)*0.027F;
                camera.view[14]=static_cast<float>(step%3-1)*0.04F;
                camera.cullingGuardBand={0.01F,0.01F};
                auto pixels=capture(mode!=0,step>0);
                if(mode==0) reference.push_back(std::move(pixels));
                else {
                    Require(pixels==reference[static_cast<std::size_t>(step)],"GPU frustum image differs from CPU during camera motion");
                    const auto bright=std::count_if(pixels.begin(),pixels.end(),[](std::uint8_t p){return p>0U;});
                    Require(bright>4096,"GPU frustum image is empty");
                }
            }
        }
        std::cout<<"gpu_frustum frames=16 candidates=32774 pixel_mismatches=0 dispatches=2 readback_wait_in_runtime=0"
            <<" cpu_submit_ms="<<cpuSubmit/15.0<<" gpu_submit_ms="<<gpuSubmit/15.0
            <<" cpu_frame_sync_readback_ms="<<cpuFrame/15.0<<" gpu_frame_sync_readback_ms="<<gpuFrame/15.0
            <<" cpu_gpu_view_ms="<<(cpuGpuSamples?cpuGpuTime/cpuGpuSamples:0.0)<<" gpu_gpu_view_ms="<<(gpuGpuSamples?gpuGpuTime/gpuGpuSamples:0.0)
            <<" gpu_timestamp_samples="<<cpuGpuSamples<<","<<gpuGpuSamples
            <<" upload_bytes="<<gpuUpload<<" reused_commands="<<reused<<'\n';
        Require(cpuGpuSamples>0U&&gpuGpuSamples>0U,"GPU frustum did not collect GPU cost timestamps");
        Require(reused>=15U,"GPU frustum did not retain candidates across camera movement");
        // Replacing/removing entities invalidates the retained input immediately.
        Require(scene.RemoveMesh(1U),"GPU frustum entity removal failed");
        const auto changedCpu=capture(false,false);
        const auto changedGpu=capture(true,false);
        Require(changedCpu==changedGpu,"GPU frustum retained stale scene content");
        bgfx::destroy(target);bgfx::destroy(readback);bgfx::destroy(depth);bgfx::destroy(color);
        renderer.Shutdown();
    }
    bgfx::shutdown();
    DestroyWindow(window);
#else
    Require(false,"GPU frustum acceptance needs a configured graphics surface");
#endif
}
} // namespace kb::render::tests
