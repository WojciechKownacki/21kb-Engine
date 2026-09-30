#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include <thread>
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/ecs/SystemSchedulerTrace.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace kb::scene;
using Clock = std::chrono::steady_clock;
struct Case {
    std::string name;
    std::size_t objects = 0, statics = 0, dynamics = 0, lights = 0;
    std::uint32_t swarm = 0;
    unsigned dirtyPercent = 0, rays = 0;
    bool active = false, dense = false, ccd = false, meshes = false, shadows = false;
    bool batchUpdates = false;
    float spacing = 4.0F;
};
const std::vector<Case> cases{
    {.name="empty"},
    {.name="foliage_100k", .swarm=100000, .meshes=true},
    {.name="foliage_1m", .swarm=1000000, .meshes=true},
    {.name="static_10k", .objects=10000},
    {.name="static_50k", .objects=50000},
    {.name="static_100k", .objects=100000},
    {.name="dirty1_10k", .objects=10000, .dirtyPercent=1},
    {.name="dirty1_50k", .objects=50000, .dirtyPercent=1},
    {.name="dirty1_100k", .objects=100000, .dirtyPercent=1},
    {.name="dirty100_10k", .objects=10000, .dirtyPercent=100},
    {.name="dirty100_50k", .objects=50000, .dirtyPercent=100},
    {.name="dirty100_100k", .objects=100000, .dirtyPercent=100},
    {.name="parent_move_10k", .objects=10000, .dirtyPercent=101},
    {.name="parent_move_50k", .objects=50000, .dirtyPercent=101},
    {.name="parent_move_100k", .objects=100000, .dirtyPercent=101},
    {.name="batch_dirty_10k", .objects=10000, .dirtyPercent=100, .batchUpdates=true},
    {.name="batch_dirty_50k", .objects=50000, .dirtyPercent=100, .batchUpdates=true},
    {.name="batch_dirty_100k", .objects=100000, .dirtyPercent=100, .batchUpdates=true},
    {.name="colliders_1k", .statics=1000},
    {.name="colliders_10k", .statics=10000},
    {.name="colliders_50k", .statics=50000},
    {.name="physics_128", .statics=10000, .dynamics=128, .active=true},
    {.name="physics_1024", .statics=10000, .dynamics=1024, .active=true},
    {.name="physics_4096", .statics=10000, .dynamics=4096, .active=true},
    {.name="sleeping_4096", .statics=10000, .dynamics=4096},
    {.name="dense_1024", .statics=1000, .dynamics=1024, .active=true, .dense=true},
    {.name="dense_4096", .statics=1000, .dynamics=4096, .active=true, .dense=true},
    {.name="ccd_1024", .statics=10000, .dynamics=1024, .active=true, .ccd=true},
    {.name="raycasts_100", .statics=10000, .rays=100},
    {.name="raycasts_1000", .statics=10000, .rays=1000},
    {.name="raycasts_1000_50k", .statics=50000, .rays=1000},
    {.name="render_1k", .objects=1000, .meshes=true},
    {.name="render_10k", .objects=10000, .meshes=true},
    {.name="geometry_1k", .objects=1000, .meshes=true},
    {.name="geometry_10k", .objects=10000, .meshes=true},
    {.name="world_50k", .objects=50000, .meshes=true, .spacing=8.0F},
    {.name="world_100k", .objects=100000, .meshes=true, .spacing=8.0F},
    {.name="lights_32", .objects=10000, .lights=32, .meshes=true},
    {.name="lights_128", .objects=10000, .lights=128, .meshes=true},
    {.name="lights_512", .objects=10000, .lights=512, .meshes=true},
    {.name="shadow_10k", .objects=10000, .meshes=true, .shadows=true},
    {.name="shadow_50k", .objects=50000, .meshes=true, .shadows=true, .spacing=8.0F},
    {.name="combined_10k", .objects=10000, .statics=10000, .dynamics=512, .lights=32, .active=true, .meshes=true, .shadows=true},
    {.name="combined_50k", .objects=50000, .statics=10000, .dynamics=1024, .lights=128, .active=true, .meshes=true, .shadows=true, .spacing=8.0F},
    {.name="city_dense_10k", .objects=10000, .statics=10000, .dynamics=512, .lights=32, .active=true, .meshes=true, .shadows=true},
    {.name="city_dense_50k", .objects=50000, .statics=10000, .dynamics=1024, .lights=128, .active=true, .meshes=true, .shadows=true},
    {.name="culling_far_10k", .objects=10000, .meshes=true},
    {.name="culling_side_10k", .objects=10000, .meshes=true},
    {.name="physics_idle_world_100k", .objects=100000, .statics=1},
};
double Ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
void Require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
void CheckErrors(Scene& scene) {
    const auto errors=scene.Runtime().DrainSceneSystemErrors();
    for (const auto& error: errors) std::cerr << error << '\n';
    Require(errors.empty(), "Runtime produced errors");
}
kb::project::ProjectDescriptor Descriptor(bool physics) {
    kb::project::ProjectDescriptor desc;
    desc.disableEnginePluginsByDefault=true;
    if (physics) desc.plugins.push_back({.name="Physics.Jolt", .binaryPath=PERF_PHYSICS_DLL, .enabled=true});
    return desc;
}
SceneDocument Document(const Case& c, std::uint64_t meshId=0, bool camera=false) {
    SceneDocument doc; doc.name=c.name; doc.guid="scene:"+c.name; doc.worldType="Runtime";
    auto& prefab=doc.worldPrefab;
    prefab.Reserve(c.objects+c.statics+c.dynamics+c.lights+4);
    ScenePrefabNodeDesc owner; owner.name="WorldOwner";
    const auto ownerIndex=prefab.AddNode(owner);
    const auto add=[&](ScenePrefabNodeDesc node) { node.parentNode=ownerIndex; return prefab.AddNode(std::move(node)); };
    for(std::size_t i=0;i<c.objects;++i) {
        ScenePrefabNodeDesc node; node.name="Building_"+std::to_string(i);
        node.transform.localPosition={ (static_cast<float>(i%128)-64.0F)*c.spacing, 1.0F, static_cast<float>(i/128)*c.spacing };
        if(c.name.starts_with("culling_far_")) node.transform.localPosition.z+=2500.0F;
        if(c.name.starts_with("culling_side_")) node.transform.localPosition.x+=5000.0F;
        if(c.name.starts_with("city_")) {
            const float height=2.0F+static_cast<float>((i*73)%11)*2.0F;
            node.transform.localScale={2.5F,height,2.5F}; node.transform.localPosition.y=height*0.5F;
        }
        if(c.meshes) node.components.meshRenderer=MeshRendererComponent{.meshAssetId=meshId,.castsShadow=c.shadows};
        static_cast<void>(add(std::move(node)));
    }
    if(c.swarm) {
        ScenePrefabNodeDesc node; node.name="VegetationOwner";
        node.components.geometrySwarm=GeometrySwarmComponent{
            .meshAssetId=meshId, .instanceCount=c.swarm, .columns=static_cast<std::uint16_t>(c.swarm==100000?316:1000), .rows=1, .layers=static_cast<std::uint16_t>(c.swarm==100000?317:1000),
            .spacing={2.0F,0.0F,2.0F}, .instanceScale=1.0F, .castsShadow=false, .enabled=meshId!=0};
        static_cast<void>(add(std::move(node)));
    }
    for(std::size_t i=0;i<c.statics;++i) {
        ScenePrefabNodeDesc node; node.name="Collider_"+std::to_string(i);
        node.transform.localPosition={ (static_cast<float>(i%128)-64.0F)*4.0F, 0.5F, static_cast<float>(i/128)*4.0F+500.0F };
        node.components.collider=ColliderComponent{};
        static_cast<void>(add(std::move(node)));
    }
    if(c.dynamics) {
        ScenePrefabNodeDesc ground; ground.name="PhysicsGround";
        ground.transform.localPosition={0,-0.5F,0};
        ground.components.collider=ColliderComponent{.boxSize={2048.0F,1.0F,2048.0F}};
        if(c.meshes) { ground.transform.localScale={2048.0F,1.0F,2048.0F}; ground.components.collider->boxSize={1,1,1}; ground.components.meshRenderer=MeshRendererComponent{.meshAssetId=meshId,.castsShadow=c.shadows}; }
        static_cast<void>(add(std::move(ground)));
    }
    for(std::size_t i=0;i<c.dynamics;++i) {
        ScenePrefabNodeDesc node; node.name="Dynamic_"+std::to_string(i);
        const std::size_t layer=c.dense ? i/256 : 0;
        node.transform.localPosition={ (static_cast<float>(i%16)-8.0F)*(c.dense?1.05F:3.0F), 0.5F+static_cast<float>(layer)*1.05F, static_cast<float>((i/16)%256)*(c.dense?1.05F:3.0F) };
        if(c.dense) node.transform.localPosition.z=static_cast<float>((i/16)%16)*1.05F;
        node.components.collider=ColliderComponent{};
        node.components.rigidbody=RigidbodyComponent{.useContinuousCollision=c.ccd};
        if(c.name.starts_with("city_")) {
            node.transform.localPosition.y=5.0F+static_cast<float>(i%7)*0.5F;
            node.components.collider->restitution=0.95F; node.components.collider->friction=0.1F;
            node.components.rigidbody->linearVelocity={1,0,0.5F};
        }
        if(c.meshes) node.components.meshRenderer=MeshRendererComponent{.meshAssetId=meshId,.castsShadow=c.shadows};
        static_cast<void>(add(std::move(node)));
    }
    if(camera) {
        ScenePrefabNodeDesc view; view.name="BenchmarkCamera";
        view.transform.localPosition={0,12,-40};
        view.components.camera=CameraComponent{.verticalFovDegrees=75.0F,.farClip=1200.0F,.primary=true,.clearColor={0.05F,0.1F,0.2F}};
        static_cast<void>(add(std::move(view)));
        ScenePrefabNodeDesc sun; sun.name="Sun";
        sun.transform.localRotation={0.35F,0.15F,0.0F,0.9246621F};
        sun.components.light=LightComponent{.kind=LightKind::Directional,.intensity=2.0F,.castsShadow=c.shadows};
        static_cast<void>(add(std::move(sun)));
    }
    for(std::size_t i=0;i<c.lights;++i) {
        ScenePrefabNodeDesc node; node.name="PointLight_"+std::to_string(i);
        node.transform.localPosition={ (static_cast<float>(i%16)-8.0F)*8.0F,4,static_cast<float>(i/16)*8.0F+5 };
        node.components.light=LightComponent{.kind=LightKind::Point,.color={0.3F+static_cast<float>(i%3)*0.3F,0.5F,0.8F},.intensity=12.0F,.range=20.0F,.castsShadow=false};
        static_cast<void>(add(std::move(node)));
    }
    return doc;
}
void Generate(const std::filesystem::path& root) {
    std::filesystem::create_directories(root/"Benchmarks");
    std::filesystem::create_directories(root/"Assets/Scenes");
    std::filesystem::create_directories(root/"Sources");
    auto descriptor=Descriptor(true); descriptor.disableEnginePluginsByDefault=false; descriptor.targetPlatforms={"Windows"};
    descriptor.plugins.push_back({.name="Rendering.BasicLighting", .binaryPath=PERF_LIGHTING_DLL, .enabled=true});
    Require(kb::project::ProjectManager::SaveProject(root/"Project.21kbproject",descriptor),"Project save failed");
    kb::project::ProjectSettings settings; settings.name="OpenWorldPerf"; settings.gameName="21kb performance laboratory"; settings.defaultMap="/Game/Scenes/render_1k.21kbscene";
    std::string error;
    Require(kb::project::ProjectSettingsStore::Save(kb::project::ProjectSettingsStore::FilePath(root),settings,error),error);
    const auto obj=root/"Sources/BenchmarkCube.obj";
    std::ofstream cube(obj);
    cube << "o BenchmarkCube\nv -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\nv -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\nf 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";
    cube.close();
    Scene author{Descriptor(false)};
    Require(author.Assets().MountProject(root),"Mount failed");
    const std::array files{obj};
    const auto imported=kb::assets::AssetImportService::ImportFiles(author.Assets().Manager(),files,"/Game/Meshes");
    Require(imported.Succeeded() && !imported.items.empty(),"Cube import failed");
    const auto meshId=imported.items.front().id.value;
    const auto spherePath=root/"Sources/BenchmarkSphere.obj";
    std::ofstream sphere(spherePath);
    constexpr unsigned rings=16, segments=32;
    for(unsigned ring=0;ring<=rings;++ring) for(unsigned segment=0;segment<=segments;++segment) {
        const float latitude=static_cast<float>(ring)*3.14159265F/static_cast<float>(rings);
        const float longitude=static_cast<float>(segment)*6.2831853F/static_cast<float>(segments);
        const float x=std::sin(latitude)*std::cos(longitude),y=std::cos(latitude),z=std::sin(latitude)*std::sin(longitude);
        sphere << "v " << x << ' ' << y << ' ' << z << '\n';
    }
    for(unsigned ring=0;ring<rings;++ring) for(unsigned segment=0;segment<segments;++segment) {
        const auto a=ring*(segments+1)+segment+1,b=a+segments+1;
        if(ring>0) sphere << "f " << a << ' ' << a+1 << ' ' << b << '\n';
        if(ring+1<rings) sphere << "f " << a+1 << ' ' << b+1 << ' ' << b << '\n';
    }
    sphere.close();
    const std::array sphereFiles{spherePath};
    const auto sphereImport=kb::assets::AssetImportService::ImportFiles(author.Assets().Manager(),sphereFiles,"/Game/Meshes");
    Require(sphereImport.Succeeded() && !sphereImport.items.empty(),"Sphere import failed");
    const auto sphereId=sphereImport.items.front().id.value;
    const auto plantPath=root/"Sources/BenchmarkPlant.obj";
    std::ofstream plantMaterial(root/"Sources/BenchmarkPlant.mtl");
    plantMaterial << "newmtl Leaves\nKd 0.10 0.45 0.07\n";
    plantMaterial.close();
    std::ofstream plant(plantPath);
    plant << "mtllib BenchmarkPlant.mtl\nusemtl Leaves\n"
          << "v -0.6 0 0\nv 0.6 0 0\nv 0 2 0\nv 0 0 -0.6\nv 0 0 0.6\nv 0 2 0\n"
          << "f 1 2 3\nf 3 2 1\nf 4 5 6\nf 6 5 4\n";
    plant.close();
    const std::array plantFiles{plantPath};
    const auto plantImport=kb::assets::AssetImportService::ImportFiles(author.Assets().Manager(),plantFiles,"/Game/Meshes");
    Require(plantImport.Succeeded() && !plantImport.items.empty(),"Plant import failed");
    const auto plantId=plantImport.items.front().id.value;
    const auto cameraScript=root/"Sources/CameraFlyby.lua";
    std::ofstream cameraSource(cameraScript);
    cameraSource << "local frame = 0\nfunction Tick(self, dt)\n  frame = frame + 1\n  self:SetProperty(\"Transform\", \"localPosition.z\", -40 + frame * 0.75)\nend\n";
    cameraSource.close();
    std::filesystem::create_directories(root/"Assets/Scripts");
    std::filesystem::copy_file(cameraScript,root/"Assets/Scripts/CameraFlyby.lua",std::filesystem::copy_options::overwrite_existing);
    static_cast<void>(author.Assets().Discover());
    const auto* cameraMetadata=author.Assets().Manager().Registry().FindByPath("/Game/Scripts/CameraFlyby.lua");
    Require(cameraMetadata!=nullptr && cameraMetadata->type=="LuaScript","Camera script discovery failed");
    const auto cameraAssetId=cameraMetadata->id.value;
    std::ofstream manifest(root/"Benchmarks/scenarios.csv");
    manifest << "name,objects,statics,dynamics,lights,dirty_percent,rays,active,dense,ccd,meshes,shadows,spacing,swarm\n";
    for(const auto& c:cases) {
        manifest << c.name << ',' << c.objects << ',' << c.statics << ',' << c.dynamics << ',' << c.lights << ',' << c.dirtyPercent << ',' << c.rays << ',' << c.active << ',' << c.dense << ',' << c.ccd << ',' << c.meshes << ',' << c.shadows << ',' << c.spacing << ',' << c.swarm << '\n';
        if(c.meshes) {
            const auto selectedMesh=c.swarm?plantId:(c.name.starts_with("geometry_")?sphereId:meshId);
            auto doc=Document(c,selectedMesh,true);
            if(c.name.starts_with("city_")) for(std::uint32_t i=0;i<doc.worldPrefab.NodeCount();++i) {
                auto* node=doc.worldPrefab.TryGetMutableNode(i);
                if(node->name=="BenchmarkCamera") node->components.behaviour=BehaviourComponent{.behaviourAssetId=cameraAssetId,.backend=BehaviourBackend::Lua};
            }
            Require(SceneDocumentService::Save(doc,root/"Assets/Scenes"/(c.name+".21kbscene")),"Scene save failed: "+c.name);
            std::cout << "Generated " << c.name << std::endl;
        }
    }
}
void Cpu(const std::filesystem::path& root,const Case& c,unsigned repetition,bool trace=false) {
    std::filesystem::create_directories(root/"Results/raw");
    std::ofstream out(root/"Results/raw"/(c.name+(trace?"_profiled_cpu_":"_cpu_")+std::to_string(repetition)+".csv"));
    out << "frame,total_ms,mutation_ms,runtime_ms,query_ms,transform_ms,fixed_capture_ms,updated,inspected,fixed_steps,working_set_mb,private_mb,awake_bodies,hits\n";
    out << std::fixed << std::setprecision(6);
    const auto docStart=Clock::now(); auto document=Document(c); const double documentMs=Ms(docStart);
    const auto sceneStart=Clock::now(); auto scene=std::make_unique<Scene>(Descriptor(c.statics+c.dynamics>0)); const double constructorMs=Ms(sceneStart);
    scene->Runtime().SetEcsProfilerEnabled(trace);
    std::ofstream traceOutput;
    if(trace) { traceOutput.open(root/"Results/raw"/(c.name+"_trace_"+std::to_string(repetition)+".csv")); traceOutput << "frame,system,cpu_ms,entities,jobs\n"; }
    if(c.statics+c.dynamics) Require(scene->IsModuleActive("Physics.Jolt"),"Jolt module is not active");
    const auto spawnStart=Clock::now();
    const auto instance=scene->Prefabs().Instantiate(document.worldPrefab);
    Require(!instance.Empty(),"Instantiation failed"); scene->Runtime().SynchronizeTransforms(); const double spawnMs=Ms(spawnStart);
    const std::size_t dynamicStart=1+c.objects+c.statics+(c.dynamics?1:0);
    std::vector<SceneEntity> dynamic;
    for(std::size_t i=0;i<c.dynamics;++i) dynamic.push_back(instance.ObjectAt(static_cast<std::uint32_t>(dynamicStart+i)).Entity());
    std::vector<SceneEntity> batchEntities;
    if(c.batchUpdates) for(std::size_t i=0;i<c.objects;++i) batchEntities.push_back(instance.ObjectAt(static_cast<std::uint32_t>(i+1)).Entity());
    const auto firstStart=Clock::now(); static_cast<void>(scene->Runtime().Update(1.0F/60.0F)); CheckErrors(*scene); const double firstMs=Ms(firstStart);
    if(!dynamic.empty()) Require(PhysicsBackend::GetVelocity(*scene,dynamic.front()).found,"No real simulated body");
    std::array<PhysicsCastResult,8> hitStorage{}; kb::library::ArrayNonAlloc<PhysicsCastResult> hits{hitStorage};
    constexpr unsigned warmup=180, frames=360;
    std::size_t hitsTotal=0;
    for(unsigned frame=0;frame<warmup+frames;++frame) {
        const auto frameStart=Clock::now();
        const std::size_t dirty=c.dirtyPercent==101?0:c.objects*c.dirtyPercent/100;
        if(c.dirtyPercent==101) {
            const auto owner=instance.ObjectAt(0).Entity();
            auto t=scene->Transforms().Get(owner); t.localPosition.x=(frame&1U)?0.1F:0.0F; scene->Transforms().Set(owner,t);
        }
        if(c.batchUpdates) {
            for(const auto entity:batchEntities) {
                auto* transform=scene->Transforms().TryGet(entity); Require(transform!=nullptr,"Batch lost a transform");
                transform->localPosition.y=1.0F+((frame&1U)?0.1F:0.0F);
            }
            scene->Transforms().MarkModified(batchEntities);
        } else for(std::size_t i=0;i<dirty;++i) {
            const auto entity=instance.ObjectAt(static_cast<std::uint32_t>(i+1)).Entity();
            auto t=scene->Transforms().Get(entity); t.localPosition.y=1.0F+((frame&1U)?0.1F:0.0F); scene->Transforms().Set(entity,t);
        }
        if(c.active) for(const auto body:dynamic) Require(PhysicsBackend::Wake(*scene,body),"Wake failed");
        const double mutationMs=Ms(frameStart); const auto updateStart=Clock::now();
        static_cast<void>(scene->Runtime().Update(1.0F/60.0F)); const double updateMs=Ms(updateStart);
        const auto queryStart=Clock::now(); std::size_t frameHits=0;
        for(unsigned ray=0;ray<c.rays;++ray) {
            const auto i=static_cast<std::size_t>((ray*73U+frame)%c.statics);
            RaycastAllNonAlloc(*scene,{(static_cast<float>(i%128)-64.0F)*4.0F,10.0F,static_cast<float>(i/128)*4.0F+500.0F},{0,-1,0},20,kPhysicsAllLayers,hits);
            frameHits+=hits.Count();
        }
        const double queryMs=Ms(queryStart), totalMs=Ms(frameStart); CheckErrors(*scene);
        if(frame>=warmup) {
            const auto report=scene->Runtime().HotPathReport(); PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
            Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=0,"Memory sample failed");
            std::size_t awake=0; for(const auto body:dynamic) if(!PhysicsBackend::IsSleeping(*scene,body)) ++awake;
            out << frame-warmup << ',' << totalMs << ',' << mutationMs << ',' << updateMs << ',' << queryMs << ',' << static_cast<double>(report.runtimeTransformSyncNanoseconds)/1e6 << ',' << static_cast<double>(report.runtimeFixedCaptureStartNanoseconds+report.runtimeFixedCaptureEndNanoseconds)/1e6 << ',' << report.transformHierarchyUpdatedCount << ',' << report.transformHierarchyInspectedCount << ',' << scene->Runtime().LastFixedStepCount() << ',' << static_cast<double>(memory.WorkingSetSize)/1048576 << ',' << static_cast<double>(memory.PrivateUsage)/1048576 << ',' << awake << ',' << frameHits << '\n';
            hitsTotal+=frameHits;
            if(trace) for(const auto& system:scene->Runtime().LastEcsProfilerTrace().systemCounters) {
                traceOutput << frame-warmup << ',' << system.systemName << ',' << static_cast<double>(system.cpuTimeNanoseconds)/1e6 << ',' << system.entitiesProcessed << ',' << system.jobsCount << '\n';
            }
        }
    }
    if(c.rays) Require(hitsTotal>=static_cast<std::size_t>(c.rays)*frames,"Raycast test did not hit authored colliders");
    // Instance stores lightweight handles; its destruction does not destroy scene objects.
    const auto teardownStart=Clock::now(); scene.reset(); const double teardownMs=Ms(teardownStart);
    std::ofstream setup(root/"Results/raw"/(c.name+"_setup_"+std::to_string(repetition)+".csv"));
    setup << "document_ms,constructor_ms,spawn_ms,first_update_ms,teardown_ms\n" << documentMs << ',' << constructorMs << ',' << spawnMs << ',' << firstMs << ',' << teardownMs << '\n';
    std::cout << c.name << " completed, spawn=" << spawnMs << "ms first=" << firstMs << "ms teardown=" << teardownMs << "ms\n";
}
void Transition(const std::filesystem::path& root,const Case& c,unsigned repetition) {
    std::filesystem::create_directories(root/"Results/raw");
    std::filesystem::create_directories(root/"Assets/Scenes");
    const auto path=root/"Assets/Scenes"/("stream_"+c.name+".21kbscene");
    const auto document=Document(c);
    const auto saveStart=Clock::now(); Require(SceneDocumentService::Save(document,path),"Streaming fixture save failed"); const double saveMs=Ms(saveStart);
    Scene scene{Descriptor(c.statics+c.dynamics>0)};
    std::ofstream out(root/"Results/raw"/(c.name+"_transition_"+std::to_string(repetition)+".csv"));
    out << "iteration,file_read_ms,replace_ms,first_update_ms,entities,file_bytes,save_ms\n";
    for(unsigned iteration=0;iteration<6;++iteration) {
        const auto readStart=Clock::now(); const auto loaded=SceneDocumentService::Load(path); const double readMs=Ms(readStart); Require(loaded.succeeded,"Streaming fixture load failed");
        const auto replaceStart=Clock::now(); const auto owned=SceneDocumentService::LoadIntoSceneOwned(scene,loaded.document); const double replaceMs=Ms(replaceStart); Require(owned.succeeded,"Scene replacement failed");
        const auto updateStart=Clock::now(); static_cast<void>(scene.Runtime().Update(1.0F/60.0F)); const double updateMs=Ms(updateStart); CheckErrors(scene);
        const auto expected=1+c.objects+c.statics+c.dynamics+(c.dynamics?1U:0U)+c.lights;
        Require(scene.Entities().Count()==expected,"Replacement leaked entities");
        out << iteration << ',' << readMs << ',' << replaceMs << ',' << updateMs << ',' << expected << ',' << std::filesystem::file_size(path) << ',' << saveMs << '\n';
    }
}
void Stream(const std::filesystem::path& root,const Case& c,unsigned repetition) {
    std::filesystem::create_directories(root/"Results/raw");
    std::filesystem::create_directories(root/"Assets/Scenes");
    const auto path=root/"Assets/Scenes"/("async_"+c.name+".21kbscene");
    const auto document=Document(c);
    Require(SceneDocumentService::Save(document,path),"Async fixture save failed");
    Scene scene{Descriptor(c.statics+c.dynamics>0)};
    scene.LoadedContent().ConfigureStreaming({.maxPendingLoads=2,.maxOperationsPerFrame=256,.maxMillisecondsPerFrame=2.0F});
    std::ofstream out(root/"Results/raw"/(c.name+"_async_"+std::to_string(repetition)+".csv"));
    out << "cycle,phase,frame,update_ms,stream_ms,operations,progress,entities\n";
    for(unsigned cycle=0;cycle<3;++cycle) {
        const auto id=scene.LoadedContent().LoadAsync(path); Require(id!=0,"Async load rejected");
        for(unsigned phase=0;phase<2;++phase) {
            if(phase) Require(scene.LoadedContent().UnloadAsync(id),"Async unload rejected");
            const auto deadline=Clock::now()+std::chrono::seconds(90);
            unsigned frame=0;
            while(scene.LoadedContent().Status(id)!=(phase?SceneLoadStatus::Unknown:SceneLoadStatus::Ready)) {
                Require(Clock::now()<deadline,"Async transition timed out");
                Require(scene.LoadedContent().Status(id)!=SceneLoadStatus::Failed,scene.LoadedContent().Error(id));
                const auto start=Clock::now(); static_cast<void>(scene.Runtime().Update(1.0F/60.0F)); const double duration=Ms(start);
                const auto stats=scene.LoadedContent().StreamingStats();
                Require(stats.operations<=256,"Streaming operation budget exceeded");
                CheckErrors(scene);
                out << cycle << ',' << phase << ',' << frame++ << ',' << duration << ',' << stats.milliseconds << ',' << stats.operations << ',' << scene.LoadedContent().Progress(id) << ',' << scene.Entities().Count() << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Require(scene.Entities().Count()==(phase?0:document.worldPrefab.NodeCount()+1),"Async transition leaked or lost entities");
        }
    }
}

}
int main(int argc,char** argv) {
    try {
        Require(argc>=3,"usage: kb_openworld_perf generate|cpu <project-root> [case] [repetition]");
        const std::filesystem::path root=argv[2];
        if(std::string(argv[1])=="generate") { Generate(root); return 0; }
        Require(argc==5 && (std::string(argv[1])=="cpu" || std::string(argv[1])=="cpu-trace" || std::string(argv[1])=="transition" || std::string(argv[1])=="stream"),"Invalid arguments");
        const auto found=std::find_if(cases.begin(),cases.end(),[&](const Case& c){return c.name==argv[3];});
        Require(found!=cases.end(),"Unknown case");
        const auto repetition=static_cast<unsigned>(std::stoul(argv[4]));
        if(std::string(argv[1])=="stream") Stream(root,*found,repetition);
        else if(std::string(argv[1])=="transition") Transition(root,*found,repetition);
        else Cpu(root,*found,repetition,std::string(argv[1])=="cpu-trace");
        return 0;
    } catch(const std::exception& e) { std::cerr << "BENCHMARK FAILED: " << e.what() << '\n'; return 1; }
}
