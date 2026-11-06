#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"
#include "assets/EditorAssetBrowserContextMenuState.hpp"
#include "assets/EditorAssetOpenPolicy.hpp"
#include "app/EditorAssetBrowserNativeCommandMap.hpp"
#include "scene/BlendSpace2DEditorDocumentState.hpp"
#include "scene/EditorBlendSpace2DPreviewSession.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>

namespace kb::editor::tests {
void RunEditorBlendSpace2DTests() {
    using namespace kb::scene;
    const auto command=EditorAssetContextCommand::NewBlendSpace2D;
    Require(EditorAssetBrowserNativeCommandMap::Command(EditorAssetBrowserNativeCommandMap::Id(command))==command,
        "BlendSpace2D creation command is not reachable through the native menu");
    EditorAssetBrowserContextMenuState menu;menu.OpenForBackground(0,0,"/Game");
    const auto items=menu.Items(false,false);
    Require(std::ranges::any_of(items,[&](const auto& item) { return item.command==command; }),"BlendSpace2D creation is missing from the content menu");
    Require(EditorAssetOpenPolicy::CanOpen({.type=kBlendSpace2DAssetType}),"BlendSpace2D asset cannot be opened through the content menu");
    const auto root=std::filesystem::temp_directory_path()/("kb_blend_editor_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Assets");
    BlendSpace2DAsset asset{{{0,0,"/Game/Clip0.kbanim"},{1,0,"/Game/Clip1.kbanim"},{0,1,"/Game/Clip2.kbanim"}}};
    std::string error;
    const auto file=root/"Assets"/"Motion.kbblend2d";
    Require(BlendSpace2DAssetIO::Save(file,asset,error),"BlendSpace2D editor fixture did not save");
    for (std::size_t i=0;i<3;++i) {
        AnimationClip clip;
        LocalTransform end{.position={i==1?8.0F:0.0F,i==2?8.0F:0.0F,0}};
        clip.tracks.push_back({.targetPath=".",.keyframes={{0,{}},{1,end}}});
        clip.events={{0.5F,i+1}};
        Require(AnimationAssetIO::SaveClip(root/"Assets"/("Clip"+std::to_string(i)+".kbanim"),clip),"BlendSpace2D preview clip did not save");
    }
    Scene source;
    Require(source.Assets().MountProject(root) && source.Assets().Discover()==4,"BlendSpace2D editor discovery failed");
    const auto* metadata=source.Assets().Manager().Registry().FindByPath("/Game/Motion.kbblend2d");
    Require(metadata!=nullptr,"BlendSpace2D editor metadata is missing");const auto id=metadata->id;
    BlendSpace2DEditorDocumentState document;
    Require(document.Open(id,asset,error) && !document.Dirty(),"BlendSpace2D document did not open cleanly");
    Require(!document.Move(1,0,0,error) && !document.CanUndo() && !document.Dirty(),"Duplicate position entered document history");
    Require(!document.Move(1,std::numeric_limits<float>::quiet_NaN(),0,error),"Nonfinite sample move was accepted");
    document.BeginGroup();
    Require(document.Move(1,2,0,error) && document.Move(1,3,0,error),"Grouped sample move failed");document.EndGroup();
    Require(document.Undo() && document.WorkingCopy()->samples[1].x==1 && !document.Dirty(),"Grouped move did not undo to the saved document");
    Require(document.Redo() && document.WorkingCopy()->samples[1].x==3,"Grouped move did not redo to its last position");
    document.MarkSaved();
    Require(document.Undo() && document.Apply([](auto& value) { value.samples[0].y=-1;return true; },error) && document.Dirty() && !document.CanRedo(),
        "History branch reused a discarded saved state");
    for (int i=0;i<140;++i) Require(document.Move(1,static_cast<float>(i+4),0,error),"Bounded history edit failed");
    int undoCount=0;while (document.Undo()) ++undoCount;
    Require(undoCount==127 && document.Dirty(),"BlendSpace2D history did not retain its bounded dirty-state contract");

    const auto owner=source.Entities().CreateObject({.name="Actor"});
    source.Transforms().TryGet(owner.Entity())->localPosition={2,3,0};source.Transforms().MarkModified(owner.Entity());
    int released=0;EditorBlendSpace2DPreviewSession preview;
    preview.SetReleaseHandler([&](const Scene&) { ++released; });
    Require(preview.Open(source,owner.Entity(),{},id,asset,0.25F,0.25F,0.5F,error),"BlendSpace2D isolated preview did not open");
    const auto pose=[&]() { return preview.Scene()->Transforms().Get(preview.Owner()).localPosition; };
    Require(std::abs(pose().x-1)<1e-5F && std::abs(pose().y-1)<1e-5F,"Preview initial phase was not evaluated");
    Require(source.Transforms().Get(owner.Entity()).localPosition.x==2 && source.Transforms().Get(owner.Entity()).localPosition.y==3,
        "Preview changed the source scene");
    Require(preview.SetCoordinates(1,0,error) && std::abs(pose().x-4)<1e-5F && std::abs(pose().y)<1e-5F,"Preview did not evaluate changed coordinates immediately");
    Require(preview.Seek(0.25F,error) && std::abs(pose().x-2)<1e-5F,"Preview phase seek did not evaluate its pose");
    Require(preview.MutableScene()->Animators().DrainEvents().empty(),"Preview seek emitted crossed events");
    preview.SetPlaying(true);Require(preview.Tick(0.25F,error) && std::abs(pose().x-4)<1e-5F,"Preview playback did not advance");
    const auto events=preview.MutableScene()->Animators().DrainEvents();
    Require(events.size()==1 && events[0].eventId==2,"Preview playback did not retain dominant clip event semantics");
    preview.SetPlaying(false);Require(!preview.Tick(0.25F,error) && std::abs(pose().x-4)<1e-5F,"Paused preview advanced");
    const auto* previous=preview.Scene();auto invalid=asset;invalid.samples[0].clipReference="/Game/Missing.kbanim";
    Require(!preview.Open(source,owner.Entity(),{},id,invalid,0,0,0,error) && preview.Scene()==previous && released==0,
        "Failed preview replacement destroyed the last valid renderer scene");
    auto working=asset;working.samples[1].x=2;
    Require(preview.Open(source,owner.Entity(),{},id,working,1,0,0.5F,error) && std::abs(pose().x-2)<1e-5F && released==1,
        "Working-copy preview did not retain changed sample coordinates");
    const auto disk=BlendSpace2DAssetIO::Load(file,error);
    Require(disk && disk->samples[1].x==1,"Working preview modified asset bytes");
    preview.Clear();Require(!preview.Scene() && released==2,"Preview cleanup did not release its renderer scene exactly once");
}
} // namespace kb::editor::tests
