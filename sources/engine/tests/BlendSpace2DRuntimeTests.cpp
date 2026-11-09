#include "TestSupport.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "engine/scene/BlendSpace2DAssetLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

#include <fstream>
#include <array>
#include <chrono>
#include <iterator>
#include <limits>

namespace kb::tests {
namespace {
using namespace kb::scene;

AnimatorController Controller(std::string reference) {
    AnimatorController controller;
    controller.parameters = {{.name="X",.floatDefault=0.25F},{.name="Y",.floatDefault=0.25F}};
    AnimatorControllerLayer layer{.name="Base",.defaultState="Blend"};
    layer.states.push_back({.id=1,.name="Blend",.blendParameter="X",.blendSpaceReference=std::move(reference),.blendParameterY="Y"});
    controller.layers.push_back(std::move(layer));
    return controller;
}

void VerifyAssetIO(const std::filesystem::path& root) {
    BlendSpace2DAsset asset{{{0,0,"/Game/A.kbanim"},{1,0,"/Game/B.kbanim"},{0,1,"/Game/C.kbanim"}}};
    std::string error;
    const auto file = root/"Assets"/"Motion.kbblend2d";
    Require(BlendSpace2DAssetIO::Save(file,asset,error),"BlendSpace2D atomic save failed");
    const auto loaded = BlendSpace2DAssetIO::Load(file,error);
    Require(loaded && loaded->samples.size()==3 && loaded->samples[1].x==1 && loaded->samples[1].clipReference==asset.samples[1].clipReference,
        "BlendSpace2D roundtrip lost sample coordinates or references");
    const auto bytes = [&]() { std::ifstream input{file,std::ios::binary}; return std::string{std::istreambuf_iterator<char>{input},{}}; };
    const auto before = bytes();
    auto invalid=asset; invalid.samples[1].x=0;
    Require(!BlendSpace2DAssetIO::Save(file,invalid,error) && !error.empty() && bytes()==before,"Invalid BlendSpace2D save replaced valid bytes");
    invalid=asset; invalid.samples[1].clipReference="invalid\nreference";
    Require(!BlendSpace2DAssetIO::Save(file,invalid,error) && bytes()==before,"Multiline clip reference was accepted");
    for (const std::string text : {"kbblend2d 2\n", "sample 0 0 \"A\"\n", "kbblend2d 1 trailing\n", "kbblend2d 1\nsample nan 0 \"A\"\n"}) {
        Require(!BlendSpace2DAssetIO::Load({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()},error) && !error.empty(),
            "Malformed BlendSpace2D source was accepted");
    }
    auto controller = Controller("/Game/Motion.kbblend2d");
    const auto controllerFile=root/"Assets"/"Motion.kbanimcontroller";
    Require(AnimationAssetIO::SaveController(controllerFile,controller,&error),"Version4 2D controller save failed");
    const auto parsed=AnimationAssetIO::LoadController(controllerFile,&error);
    Require(parsed && parsed->layers[0].states[0].blendSpaceReference=="/Game/Motion.kbblend2d" &&
        parsed->layers[0].states[0].blendParameterY=="Y","Version4 2D controller roundtrip lost bindings");
    controller.parameters[1].type=AnimatorParameterType::Bool;
    Require(!AnimationAssetIO::ValidateController(controller,&error),"2D state accepted a non-float axis");
    controller=Controller("/Game/Motion.kbblend2d"); controller.layers[0].states[0].blendChildren={{0,"A"},{1,"B"}};
    Require(!AnimationAssetIO::ValidateController(controller,&error),"2D state accepted conflicting motion sources");
}

void VerifyRuntime(const std::filesystem::path& root, bool skeletal, bool rootMotion) {
    const auto project=root/(skeletal?"Skeletal":rootMotion?"RootMotion":"Generic");
    std::filesystem::create_directories(project/"Assets");
    Scene scene;
    Require(scene.Assets().MountProject(project),"BlendSpace2D project did not mount");
    SkeletonAsset skeleton;
    std::uint64_t skeletonId=0;
    if (skeletal) {
        const kb::math::Mat4 identity{.columns={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
        skeleton.bones.push_back({.id=1,.parentIndex=-1,.name="Root",.inverseBind=identity});
        Require(SkeletonAssetIO::Save(project/"Assets"/"Rig.kbskeleton",skeleton),"2D test skeleton save failed");
        static_cast<void>(scene.Assets().Discover());
        const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Rig.kbskeleton");
        Require(metadata!=nullptr,"2D test skeleton was not discovered"); skeletonId=metadata->id.value;
    }
    const std::array<float,3> durations{1,2,4};
    const std::array<Vec3,3> positions{{{0,0,0},{10,0,0},{0,20,0}}};
    BlendSpace2DAsset asset;
    for (std::size_t i=0;i<3;++i) {
        const auto reference="/Game/Clip"+std::to_string(i)+".kbanim";
        asset.samples.push_back({i==1?1.0F:0.0F,i==2?1.0F:0.0F,reference});
        AnimationClip clip; clip.durationSeconds=durations[i];
        const LocalTransform value{.position=positions[i]};
        if (skeletal) {
            clip.targetSkeletonAssetId=skeletonId;
            clip.targetSkeletonCompatibilitySignature=SkeletonCompatibilitySignature(skeleton);
            clip.skeletalTracks.push_back({.boneId=1,.keyframes={{0,value},{durations[i],value}}});
            clip.morphTracks.push_back({"Expression",{{0,static_cast<float>(i)*0.5F},{durations[i],static_cast<float>(i)*0.5F}}});
        } else if (rootMotion) {
            const LocalTransform end{.position={2.0F+static_cast<float>(i)*4.0F,0,0}};
            clip.tracks.push_back({.targetPath="",.keyframes={{0,{}},{durations[i],end}}});
        } else clip.tracks.push_back({.targetPath=".",.keyframes={{0,value},{durations[i],value}}});
        if (skeletal) clip.curves.push_back({"Weight",{{0,static_cast<float>(i)*10.0F},{durations[i],static_cast<float>(i)*10.0F}}});
        clip.events.push_back({durations[i]*0.1F,100+i});
        Require(AnimationAssetIO::SaveClip(project/"Assets"/("Clip"+std::to_string(i)+".kbanim"),clip),"2D source clip save failed");
    }
    std::string error;
    Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Motion.kbblend2d",asset,error) &&
        AnimationAssetIO::SaveController(project/"Assets"/"Motion.kbanimcontroller",Controller("/Game/Motion.kbblend2d"),&error),
        "2D runtime fixture save failed");
    static_cast<void>(scene.Assets().Discover());
    const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Motion.kbanimcontroller");
    Require(metadata!=nullptr,"2D controller was not discovered"); const auto controllerId=metadata->id.value;
    const auto owner=scene.Entities().CreateObject({.name="Actor"});
    if (skeletal) Require(scene.Components().SkeletonBindings().Set(owner.Entity(),{
        .skeletonAssetId=skeletonId,.skeletonCompatibilitySignature=SkeletonCompatibilitySignature(skeleton),.enabled=true}),"2D skeleton binding failed");
    scene.Components().Animators().Set(owner.Entity(),{.controllerAssetId=controllerId,
        .rootMotionOwner=rootMotion?AnimatorRootMotionOwner::Animator:AnimatorRootMotionOwner::None});
    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0));
    Require(scene.Runtime().DrainSceneSystemErrors().empty() && scene.Animators().Exists(owner.Entity()),"2D animator did not attach");
    Require(scene.Animators().SeekNormalized(owner.Entity(),0),"2D animator did not sample its initial phase");
    const auto pose=[&]() {
        if (skeletal) { const auto view=scene.Animators().InstanceSkeleton(owner.Entity()); Require(view.has_value(),"2D skeletal pose is missing");return view->currentLocalPose.positions[0]; }
        return scene.Transforms().Get(owner.Entity()).localPosition;
    };
    if (!rootMotion) {
        Require(NearlyEqual(pose().x,2.5F) && NearlyEqual(pose().y,5.0F),"2D barycentric pose did not reach the scene");
        if (skeletal) {
            const auto curve=scene.Animators().Curve(owner.Entity(),"Weight");
            Require(curve && NearlyEqual(*curve,7.5F),"2D named curve weights differ from the pose");
            Require(NearlyEqual(scene.Animators().InstanceSkeleton(owner.Entity())->morphWeights.currentWeights[0],0.375F),"2D morph weights differ from the pose");
        }
    }
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(scene.Runtime().DrainSceneSystemErrors().empty(),"2D animation update failed");
    const auto events=scene.Animators().DrainEvents();
    Require(events.size()==1 && events[0].eventId==100,"2D state did not emit only its dominant clip's crossed event");
    if (rootMotion) Require(NearlyEqual(pose().x,0.625F),"2D root motion does not use phase and blend weights together");
    else {
        Require(scene.Animators().SetFloat(owner.Entity(),"X",1) && scene.Animators().SetFloat(owner.Entity(),"Y",1) &&
            scene.Animators().SeekNormalized(owner.Entity(),0.5F),"2D boundary query setup failed");
        static_cast<void>(scene.Runtime().Update(0));
        Require(scene.Runtime().DrainSceneSystemErrors().empty(),"2D boundary pose evaluation failed");
        Require(NearlyEqual(pose().x,5) && NearlyEqual(pose().y,10),"2D outside-hull projection did not reach the scene");
        Require(!scene.Animators().SetFloat(owner.Entity(),"X",std::numeric_limits<float>::quiet_NaN()),"2D axis accepted a nonfinite value");
    }
    const auto* space=scene.Assets().Manager().Registry().FindByPath("/Game/Motion.kbblend2d");
    Require(space!=nullptr,"2D runtime source missing");
    const auto spaceMetadata=*space;
    BlendSpace2DAssetLoader loader;
    Require(loader.DiscoverDependencies(*space,scene.Assets().Manager().Registry()).size()==3 &&
        !loader.ValidateDependencies(*space,scene.Assets().Manager().Registry()),"2D clip dependencies were not discovered");
    const auto spaceId=space->id;
    if (!rootMotion) {
        Require(scene.Animators().SetFloat(owner.Entity(),"X",0.5F) && scene.Animators().SetFloat(owner.Entity(),"Y",0.25F) &&
            scene.Animators().SeekNormalized(owner.Entity(),0.5F),"2D hot reload setup failed");
        static_cast<void>(scene.Runtime().Update(0));
        Require(NearlyEqual(pose().x,5) && NearlyEqual(pose().y,5),"2D hot reload initial query failed");
        asset.samples[1].x=2;
        Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Motion.kbblend2d",asset,error) &&
            scene.Assets().Manager().RefreshAsset(spaceId),"2D hot reload asset did not refresh");
        static_cast<void>(scene.Runtime().Update(0));
        const auto state=scene.Animators().State(owner.Entity(),"Base");
        Require(scene.Runtime().DrainSceneSystemErrors().empty() && state && NearlyEqual(state->normalizedTime,0.5F) &&
            NearlyEqual(pose().x,2.5F) && NearlyEqual(pose().y,5),"2D hot reload lost its phase, parameters or geometry plan");
        Require(scene.Animators().DrainEvents().empty(),"2D hot reload emitted scrub events");
    }
    asset.samples[0].clipReference="/Game/Missing.kbanim";
    Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Motion.kbblend2d",asset,error),"Missing-clip fixture save failed");
    Require(loader.ValidateDependencies(spaceMetadata,scene.Assets().Manager().Registry()).has_value(),"Missing 2D clip was not diagnosed");
}

void VerifyMissingChannels(const std::filesystem::path& root) {
    const auto project=root/"Channels";std::filesystem::create_directories(project/"Assets");
    for (const bool right : {false,true}) {
        AnimationClip clip;
        const LocalTransform value{.position={right?20.0F:10.0F,0,0}};
        clip.tracks.push_back({.targetPath=right?"Right":"Left",.keyframes={{0,value},{1,value}}});
        Require(AnimationAssetIO::SaveClip(project/"Assets"/(right?"Right.kbanim":"Left.kbanim"),clip),"Disjoint-channel fixture clip did not save");
    }
    std::string error;
    const BlendSpace2DAsset asset{{{0,0,"/Game/Left.kbanim"},{1,0,"/Game/Right.kbanim"}}};
    Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Motion.kbblend2d",asset,error) &&
        AnimationAssetIO::SaveController(project/"Assets"/"Motion.kbanimcontroller",Controller("/Game/Motion.kbblend2d")),"Disjoint-channel fixture did not save");
    Scene scene;Require(scene.Assets().MountProject(project),"Disjoint-channel project did not mount");
    static_cast<void>(scene.Assets().Discover());
    const auto* controller=scene.Assets().Manager().Registry().FindByPath("/Game/Motion.kbanimcontroller");Require(controller,"Disjoint-channel controller missing");
    const auto owner=scene.Entities().CreateObject({.name="Actor"});
    const auto left=scene.Entities().CreateObject({.name="Left",.parent=owner});
    const auto right=scene.Entities().CreateObject({.name="Right",.parent=owner});
    scene.Transforms().TryGet(left.Entity())->localPosition.x=3;scene.Transforms().MarkModified(left.Entity());
    scene.Transforms().TryGet(right.Entity())->localPosition.x=5;scene.Transforms().MarkModified(right.Entity());
    scene.Components().Animators().Set(owner.Entity(),{.controllerAssetId=controller->id.value});scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0));
    for (const auto expected:std::array<std::array<float,3>,3>{{{1,3,20},{0,10,5},{0.5F,6.5F,12.5F}}}) {
        Require(scene.Animators().SetFloat(owner.Entity(),"X",expected[0]),"Disjoint-channel query failed");
        static_cast<void>(scene.Runtime().Update(0));
        Require(scene.Runtime().DrainSceneSystemErrors().empty() && NearlyEqual(scene.Transforms().Get(left.Entity()).localPosition.x,expected[1]) &&
            NearlyEqual(scene.Transforms().Get(right.Entity()).localPosition.x,expected[2]),"Inactive 2D channels retained a stale pose instead of their bind fallback");
    }
}

void VerifyEmptyAndMixed(const std::filesystem::path& root) {
    const auto project=root/"Empty";std::filesystem::create_directories(project/"Assets");
    AnimationClip clip;clip.tracks.push_back({.targetPath=".",.keyframes={{0,{}},{1,{}}}});
    Require(AnimationAssetIO::SaveClip(project/"Assets"/"Clip.kbanim",clip),"Empty-space fixture clip save failed");
    std::string error;BlendSpace2DAsset asset;
    Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Empty.kbblend2d",asset,error) &&
        AnimationAssetIO::SaveController(project/"Assets"/"Controller.kbanimcontroller",Controller("/Game/Empty.kbblend2d")),"Empty-space fixture save failed");
    Scene scene;Require(scene.Assets().MountProject(project) && scene.Assets().Discover()==3,"Empty-space fixture discovery failed");
    const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Controller.kbanimcontroller");
    const auto* space=scene.Assets().Manager().Registry().FindByPath("/Game/Empty.kbblend2d");
    Require(metadata && space,"Empty-space fixture metadata missing");const auto controllerId=metadata->id,spaceId=space->id;
    const auto owner=scene.Entities().CreateObject({.name="Actor"});
    scene.Components().Animators().Set(owner.Entity(),{.controllerAssetId=controllerId.value});
    scene.Runtime().SetPlaying(true);static_cast<void>(scene.Runtime().Update(0));
    Require(!scene.Runtime().DrainSceneSystemErrors().empty() && !scene.Animators().Exists(owner.Entity()),"Referenced empty space did not fail safely");
    asset.samples={{0,0,"/Game/Clip.kbanim"}};
    Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Empty.kbblend2d",asset,error) &&
        scene.Assets().Manager().RefreshAsset(spaceId) && scene.Assets().Manager().RefreshAsset(controllerId),"Empty-space repair did not refresh");
    static_cast<void>(scene.Runtime().Update(0));
    Require(scene.Runtime().DrainSceneSystemErrors().empty() && scene.Animators().Exists(owner.Entity()),"Single-sample repair did not attach after an empty-space failure");
    SkeletonAsset skeleton;
    const kb::math::Mat4 identity{.columns={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    skeleton.bones.push_back({.id=1,.parentIndex=-1,.name="Root",.inverseBind=identity});
    Require(SkeletonAssetIO::Save(project/"Assets"/"Rig.kbskeleton",skeleton),"Mixed-space skeleton did not save");
    static_cast<void>(scene.Assets().Discover());
    const auto* rig=scene.Assets().Manager().Registry().FindByPath("/Game/Rig.kbskeleton");Require(rig,"Mixed-space skeleton metadata missing");
    const auto rigId=rig->id.value;
    AnimationClip bone;bone.targetSkeletonAssetId=rigId;bone.targetSkeletonCompatibilitySignature=SkeletonCompatibilitySignature(skeleton);
    bone.skeletalTracks.push_back({.boneId=1,.keyframes={{0,{}},{1,{}}}});
    Require(AnimationAssetIO::SaveClip(project/"Assets"/"Bone.kbanim",bone),"Mixed-space skeletal clip did not save");
    for (const bool reverse:{false,true}) {
        asset.samples={{0,0,reverse?"/Game/Bone.kbanim":"/Game/Clip.kbanim"},{1,0,reverse?"/Game/Clip.kbanim":"/Game/Bone.kbanim"}};
        Require(BlendSpace2DAssetIO::Save(project/"Assets"/"Empty.kbblend2d",asset,error),"Mixed-space fixture did not save");
        // A failed reload retains the previous valid instance. Use a fresh scene
        // here to check that neither sample ordering can create a mixed instance.
        Scene mixed;Require(mixed.Assets().MountProject(project),"Mixed-space project did not mount");
        static_cast<void>(mixed.Assets().Discover());
        const auto actor=mixed.Entities().CreateObject({.name="Actor"});
        Require(mixed.Components().SkeletonBindings().Set(actor.Entity(),{.skeletonAssetId=rigId,
            .skeletonCompatibilitySignature=bone.targetSkeletonCompatibilitySignature,.enabled=true}),"Mixed-space owner could not bind its skeleton");
        mixed.Components().Animators().Set(actor.Entity(),{.controllerAssetId=controllerId.value});mixed.Runtime().SetPlaying(true);
        static_cast<void>(mixed.Runtime().Update(0));const auto errors=mixed.Runtime().DrainSceneSystemErrors();
        if (errors.size()!=1 || errors[0].find("cannot mix")==std::string::npos) for (const auto& message:errors) std::cerr << message << '\n';
        Require(errors.size()==1 && errors[0].find("cannot mix")!=std::string::npos && !mixed.Animators().Exists(actor.Entity()),"Mixed clip families were accepted or depended on sample ordering");
    }
}
} // namespace

void RunBlendSpace2DRuntimeTests() {
    const auto root=std::filesystem::temp_directory_path()/("21kb-blend-space-runtime-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Assets");
    VerifyAssetIO(root);
    VerifyRuntime(root,false,false);
    VerifyRuntime(root,true,false);
    VerifyRuntime(root,false,true);
    VerifyMissingChannels(root);
    VerifyEmptyAndMixed(root);
}
} // namespace kb::tests
