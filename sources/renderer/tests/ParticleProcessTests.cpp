#include "RendererTestSupport.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/particles/ParticleProcessAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"
#include "kb/render/particles/ParticleGpuReadback.hpp"
#include <cstdio>
#include <cmath>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace kb::render::tests {
void RunParticleProcessTests(const std::filesystem::path& compiled) {
#if defined(_WIN32)
    class Surface final:public RenderSurface {
    public:
        HWND window=CreateWindowExW(0,L"STATIC",L"Particle process",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        ~Surface() override {if(window) DestroyWindow(window);}
        std::uint32_t Width() const noexcept override{return 64U;}
        std::uint32_t Height() const noexcept override{return 64U;}
        void* NativeWindowHandle() const noexcept override{return window;}
        void* NativeDisplayHandle() const noexcept override{return nullptr;}
    } surface;
    DisplayConfig config;config.syncMode=DisplaySyncMode::Uncapped;config.preferredBgfxRendererType=static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;Require(renderer.Initialize(surface,&config),"Particle process renderer unavailable");
    Require(bgfx::getRendererType()==bgfx::RendererType::Direct3D11,"Particle process test requires D3D11");
    {
        kb::assets::AssetManager assets;
        Require(assets.RegisterLoader(std::make_unique<kb::particles::ParticleProcessAssetLoader>()),"Process loader registration failed");
        Require(assets.RegisterAsset({.id={991U},.type="ParticleProcess",.name="Custom",.virtualPath="/Game/Custom.kbpsim",.physicalPath=compiled,.sourceExtension=".kbpsim"}),"Process registration failed");
        auto payload=assets.Load<kb::particles::ParticleProcessAsset>({991U});
        Require(payload.IsLoaded(),"Compiled process load failed");
        std::vector<std::uint8_t> bytes;std::string error;
        Require(kb::particles::EncodeParticleProcessAsset(*payload,bytes,error),"Process asset encoding failed");
        auto bad=bytes;bad[8]=255U;
        Require(!kb::particles::DecodeParticleProcessAsset(bad,error),"Unsupported process contract accepted");
        kb::scene::ParticleEffectAsset effect;
        effect.effectId=1U;effect.displayName="Process";effect.backendPolicy=kb::scene::ParticleBackendPolicy::GpuVisualRequired;
        effect.emitters.emplace_back();auto& author=effect.emitters[0];author.emitterId=1U;author.name="Custom";
        author.output.material.virtualPath="/Game/Particle.kbmat";author.customProcess.virtualPath="/Game/Custom.kbpsim";
        std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
        auto text=kb::scene::ParticleEffectAssetIO::Serialize(effect,diagnostics);
        Require(text.has_value(),"Custom process serialization failed");
        auto parsed=kb::scene::ParticleEffectAssetIO::Parse(*text);
        Require(parsed.Succeeded() && parsed.asset->emitters[0].customProcess.virtualPath==author.customProcess.virtualPath,"Custom process reference did not round trip");
        effect.backendPolicy=kb::scene::ParticleBackendPolicy::CpuDeterministic;
        Require(!kb::scene::ParticleEffectAssetValidator::ValidateStructure(effect).Succeeded(),"CPU silently accepted custom GPU process");
        ParticleGpuEmitterSimulation simulation;Require(simulation.Initialize(),"GPU particle simulation unavailable");
        kb::particles::ParticleGpuEmitterCommand command;command.key={1U,1U};command.hasParams=true;
        command.params.capacity=4U;command.params.customProcessAssetId=991U;command.params.sort=kb::particles::ParticleRenderSortMode::None;
        command.params.size.fill(1.0F);for(auto& c:command.params.color)c={1.0F,1.0F,1.0F,1.0F};
        command.spawns.push_back({.position={0.0F,0.0F,0.0F},.birthTime=0.0F,.velocity={},.lifetime=10.0F});
        ParticleGpuReadback readback;
        auto run=[&](float now) {
            command.simTime=now;simulation.Apply(1U,std::span{&command,1U},&assets);simulation.Advance(1U,0.0F);
            Require(simulation.RejectedUpdates(1U).empty(),"Custom process rejected");
            simulation.Dispatch(0U,1U,{});static_cast<void>(bgfx::frame());
            auto draws=simulation.Draws(1U);Require(draws.size()==1U && readback.Read(draws[0],draws[0].capacity),"Custom process instance readback failed");
            for (const auto& record:readback.Records()) if (record[3]>0.0F) return record;
            Require(false,"Custom process has no live instance");return ParticleGpuInstanceRecord{};
        };
        // First render time is one fixed step behind latest, as for the built-in integrator.
        auto first=run(1.0F);command.hasParams=false;command.spawns.clear();
        auto second=run(2.0F);
        Require(first[0]>1.9F && first[0]<2.01F && second[0]>3.9F && second[0]<4.01F,"Custom position was not integrated persistently");
        Require(second[1]>0.74F && second[1]<0.76F && second[9]==3.0F,"Custom velocity did not feed the next integration");
        Require(second[12]==0.25F && second[13]==0.5F && second[14]==0.75F && second[3]>2.4F,"Custom color or persistent data missing from draw");
        const auto allocated=simulation.AllocatedBytes();Require(allocated==4U*272U,"Custom buffers not included in budget");
        // Failed replacement preserves the working program and state.
        command.hasParams=true;command.params.customProcessAssetId=992U;
        simulation.Apply(1U,std::span{&command,1U},&assets);
        Require(simulation.RejectedUpdates(1U).size()==1U && simulation.AllocatedBytes()==allocated,"Failed process replacement corrupted lifetime");
        command.hasParams=false;simulation.Apply(1U,std::span{&command,1U},&assets);
        Require(simulation.RejectedUpdates(1U).size()==1U,"Clock-only update erased a rejected process replacement");
        command.hasParams=true;command.params.customProcessAssetId=991U;command.clear=true;command.spawns.push_back({.position={},.birthTime=2.0F,.velocity={},.lifetime=10.0F});
        auto reset=run(3.0F);Require(std::abs(reset[3]-first[3])<0.05F,"Restart retained custom state");
        // Deferred GPU ownership survives manager cache release and immediate scene destruction.
        static_cast<void>(assets.Unload({991U}));payload={};
        simulation.ReleaseScene(1U);Require(simulation.AllocatedBytes()==0U && simulation.Draws(1U).empty(),"Process GPU resources leaked");
        readback.Shutdown();simulation.Shutdown();
#if defined(KB_21KB_PARTICLE_PLUGIN_PATH)
        effect.backendPolicy=kb::scene::ParticleBackendPolicy::GpuVisualRequired;
        effect.emitters[0].spawn.rateOverTime.keyframes={{.time=0.0F,.value=0.0F}};
        effect.emitters[0].spawn.speedMin=effect.emitters[0].spawn.speedMax=0.0F;
        effect.emitters[0].spawn.lifetimeMin=effect.emitters[0].spawn.lifetimeMax=10.0F;
        const auto effectFile=compiled.parent_path()/"Authored.kbvfx";
        Require(kb::scene::ParticleEffectAssetIO::SaveDetailed(effectFile,effect).Succeeded(),"Authored process effect save failed");
        kb::project::ProjectDescriptor project;project.disableEnginePluginsByDefault=true;
        project.plugins.push_back({.name="Rendering.21kbParticle",.binaryPath=KB_21KB_PARTICLE_PLUGIN_PATH,.enabled=true});
        {
            kb::scene::Scene scene(project,kb::scene::SceneMode::Runtime);
            auto& manager=scene.Assets().Manager();
            Require(manager.RegisterAsset({.id={991U},.type="ParticleProcess",.name="Custom",.virtualPath="/Game/Custom.kbpsim",.physicalPath=compiled,.sourceExtension=".kbpsim"}),"Scene process registration failed");
            Require(manager.RegisterAsset({.id={992U},.type="ParticleEffect",.name="Authored",.virtualPath="/Game/Authored.kbvfx",.physicalPath=effectFile,.sourceExtension=".kbvfx"}),"Scene effect registration failed");
            Require(manager.Registry().Upsert({.id={993U},.type="RenderMaterial",.name="Particle",.virtualPath="/Game/Particle.kbmat",.contentHash=1U}),"Scene material registration failed");
            const auto owner=scene.Entities().CreateEntity();scene.Transforms().Set(owner,{});
            Require(simulation.Initialize(),"Authored simulation unavailable");
            renderer.PrepareParticleScene(scene);
            Require(kb::particles::ParticlePlayback::RenderCapabilities(scene).gpuVisualAvailability==kb::particles::ParticleGpuVisualAvailability::Ready,"Renderer did not prepare required GPU policy before the first tick");
            scene.Components().ParticleEffects().Set(owner,{.effectAssetId=992U,.enabled=true,.autoPlay=true});
            constexpr float step=kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
            Require(scene.Runtime().Update(step),"Authored process initial step failed");
            auto ids=kb::particles::ParticlePlayback::LiveInstanceIds(scene);
            Require(ids.size()==1U && kb::particles::ParticlePlayback::Emit(scene,ids[0],1U).Succeeded(),"Authored custom process did not start");
            Require(simulation.Initialize(),"Authored simulation unavailable");
            bool resolved=false;
            for(int i=0;i<3;++i) {
                Require(scene.Runtime().Update(step),"Authored custom process runtime step failed");
                std::vector<kb::particles::ParticleGpuEmitterCommand> commands;
                kb::particles::ParticlePlayback::DrainGpuEmitterCommands(scene,commands);
                for(const auto& c:commands) if(c.hasParams) resolved|=c.params.customProcessAssetId==991U;
                simulation.Apply(scene.Id(),commands,&manager);simulation.Advance(scene.Id(),step);
                simulation.Dispatch(0U,scene.Id(),{});static_cast<void>(bgfx::frame());
            }
            Require(resolved && simulation.RejectedUpdates(scene.Id()).empty(),"Compiler lost authored custom process dependency");
            auto draws=simulation.Draws(scene.Id());Require(draws.size()==1U && readback.Read(draws[0],1U),"Authored process produced no GPU instance");
            Require(readback.Records()[0][12]==0.25F,"Authored process did not execute its external shader");
            readback.Shutdown();simulation.Shutdown();
            renderer.ReleaseScene(scene);
        }
#endif
        std::printf("particle_process source_free=PASS position=%g,%g velocity=%g custom_size=%g restart=PASS rollback=PASS resources=0\n",first[0],second[0],second[9],second[3]);
    }
    renderer.Shutdown();
#else
    static_cast<void>(compiled);Require(false,"Particle process GPU proof requires a graphics surface");
#endif
}
}
