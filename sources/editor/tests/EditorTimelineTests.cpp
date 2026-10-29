#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"
#include "scene/TimelineEditorDocumentState.hpp"
#include "scene/TimelineEditorModel.hpp"
#include "scene/EditorTimelinePreviewSession.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/script/ScriptSharedState.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace kb::editor::tests {
void RunEditorTimelineTests() {
    using namespace kb::scene;
    const auto root=std::filesystem::temp_directory_path()/
        ("kb_timeline_editor_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Assets");
    TimelineAsset initial; initial.bindings={{"Target","."}};
    initial.transformTracks={{"Target",{{0,{}},{1,{.position={10,0,0}}}}}};
    initial.eventTracks={{"Target",{{0.25F,1,"First","one"},{0.75F,2,"Last","two"}}}};
    const auto path=root/"Assets/Sequence.kbtimeline";
    Require(TimelineAssetIO::Save(path,initial),"Timeline editor fixture save failed");
    {
        std::ofstream script(root/"Assets/Receiver.lua");
        script << R"lua(function Created(self)
  SetShared("count", 0)
  Events.Subscribe("OnTimelineEvent", function(event)
    SetShared("count", GetShared("count") + 1)
    SetShared("last", event.args.name)
    SetShared("payload", event.args.payload)
  end)
end
)lua";
    }
    Scene source;
    Require(source.Assets().MountProject(root) && source.Assets().Discover()==2,"Timeline editor asset discovery failed");
    const auto* meta=source.Assets().Manager().Registry().FindByPath("/Game/Sequence.kbtimeline");
    const auto* script=source.Assets().Manager().Registry().FindByPath("/Game/Receiver.lua");
    Require(meta && script,"Timeline editor metadata missing");
    const auto owner=source.Entities().CreateObject({.name="Target"});
    source.Components().Behaviours().Set(owner.Entity(),{.behaviourAssetId=script->id.value,.backend=BehaviourBackend::Lua,.enabled=true});
    TimelineEditorDocumentState document;
    Require(document.Open(meta->id,initial) && !document.Dirty(),"Timeline editor document open failed");
    const auto edit=[&](const std::string& label,const std::string& value) {
        const auto rows=TimelineEditorModel::Rows(*document.WorkingCopy());
        for (const auto& row:rows) if (row.label==label && row.edit)
            return document.Apply([&](auto& asset) { return row.edit(asset,value); });
        return false;
    };
    Require(!edit("Duration (seconds)","nan") && !edit("Duration (seconds)","0") && !document.Dirty(),"Invalid Timeline edit entered history");
    document.BeginGroup();
    Require(edit("Duration (seconds)","2") && edit("Duration (seconds)","3"),"Grouped Timeline edit failed");
    document.EndGroup();
    Require(document.Undo() && document.WorkingCopy()->durationSeconds==1 && !document.Dirty(),"Grouped Timeline Undo failed");
    Require(document.Redo() && document.WorkingCopy()->durationSeconds==3,"Grouped Timeline Redo failed");
    document.MarkSaved();
    Require(document.Undo() && edit("Duration (seconds)","4") && !document.CanRedo() && document.Dirty(),"Timeline redo branch retained an invalid saved cursor");
    Require(document.Open(meta->id,initial),"Timeline document reset failed");
    Require(edit("    Position (x y z)","2 3 4") && !edit("    Position (x y z)","2 3 4 extra"),"Transform row parsing failed");
    Require(document.Undo(),"Transform row Undo failed");
    Require(edit("  Name","Renamed") && document.WorkingCopy()->eventTracks[0].binding=="Renamed" &&
        document.WorkingCopy()->transformTracks[0].binding=="Renamed" && document.Undo(),"Binding rename did not update track references atomically");
    const auto headings=TimelineEditorModel::Rows(*document.WorkingCopy());
    Require(headings[4].addKey && document.Apply([&](auto& asset) { return headings[4].addKey(asset,0.5F); }) &&
        document.WorkingCopy()->transformTracks[0].keyframes.size()==3 && document.Undo(),"Timeline track key insertion failed");
    for (const auto kind:{TimelineEditorTrackKind::Audio,TimelineEditorTrackKind::Animation,TimelineEditorTrackKind::Sequence,
            TimelineEditorTrackKind::Property,TimelineEditorTrackKind::Marker}) {
        const std::string reference=kind==TimelineEditorTrackKind::Property ? "UICanvasGroup.opacity" : "/Game/Clip.asset";
        Require(document.Apply([&](auto& asset) { return TimelineEditorModel::Add(asset,kind,"Target",reference,source.Runtime().EcsWorld(),&initial); }),
            "Typed Timeline track creation failed");
        Require(document.Undo(),"Typed Timeline track Undo failed");
    }
    Require(!document.Apply([&](auto& asset) { return TimelineEditorModel::Add(asset,TimelineEditorTrackKind::Property,"Target","Missing.field",source.Runtime().EcsWorld()); }),
        "Unknown reflected property was accepted");
    {
        TimelineAsset empty; empty.bindings={{"Target","."}};
        TimelineEditorDocumentState created;
        Require(created.Open(meta->id,empty),"Empty Timeline document failed to open");
        for (const auto kind : {TimelineEditorTrackKind::Transform,TimelineEditorTrackKind::Event,TimelineEditorTrackKind::Marker}) {
            Require(created.Apply([&](auto& asset) { return TimelineEditorModel::Add(asset,kind,"Target","",source.Runtime().EcsWorld()); }) &&
                created.Undo(),"New Timeline track or marker violates its runtime contract");
        }
    }
    Require(edit("    Payload","quoted \"payload\"\nnext line"),"Timeline event payload edit failed");
    Require(TimelineAssetIO::Save(path,*document.WorkingCopy()),"Timeline atomic save failed");
    document.MarkSaved();
    const auto loaded=TimelineAssetIO::Load(path);
    Require(loaded && loaded->eventTracks[0].keyframes[0].payload=="quoted \"payload\"\nnext line","Timeline saved payload roundtrip failed");
    const auto bytes=[&]() { std::ifstream file(path,std::ios::binary); return std::string{std::istreambuf_iterator<char>{file},{}}; };
    const auto originalBytes=bytes(); auto invalid=*loaded; invalid.durationSeconds=0;
    Require(!TimelineAssetIO::Save(path,invalid) && bytes()==originalBytes,"Invalid Timeline save replaced the existing file");
    auto working=*loaded; working.transformTracks[0].keyframes[1].transform.position.x=20;
    unsigned releases=0;
    EditorTimelinePreviewSession preview; std::string error;
    preview.SetReleaseHandler([&](const Scene&) { ++releases; });
    Require(preview.Open(source,owner.Entity(),{},meta->id,working,error),error.c_str());
    Require(preview.Seek(0.5F,error) && NearlyEqual(preview.Scene()->Transforms().Get(preview.Owner()).localPosition.x,10),"Working-copy preview did not use isolated edited pose");
    Require(NearlyEqual(source.Transforms().Get(owner.Entity()).localPosition.x,0),"Timeline preview mutated the source scene");
    Require(preview.Scripts()->SharedState().Get("count")->AsInt()==0,"Scrubbing dispatched Timeline events");
    Require(preview.Seek(0,error) && preview.Play(1,false,error) && preview.Tick(0.5F,error),error.c_str());
    Require(preview.Scripts()->SharedState().Get("count")->AsInt()==1 && preview.Scripts()->SharedState().Get("last")->AsString()=="First","Editor preview did not dispatch the runtime event schema");
    preview.Pause(); Require(!preview.Playing() && !preview.Tick(0.1F,error) && NearlyEqual(preview.Time(),0.5),"Paused Timeline preview advanced");
    Require(preview.Seek(1,error) && preview.Play(-1,false,error) && preview.Tick(0.5F,error),error.c_str());
    Require(preview.Scripts()->SharedState().Get("last")->AsString()=="Last","Reverse preview event traversal failed");
    Require(preview.Seek(0,error) && preview.Play(1,true,error) && preview.Tick(0.25F,error),error.c_str());
    const auto* stable=preview.Scene(); const auto time=preview.Time();
    auto bad=working; bad.bindings[0].defaultPath="Missing";
    Require(!preview.Open(source,owner.Entity(),{},meta->id,bad,error) && preview.Scene()==stable && NearlyEqual(preview.Time(),time),"Failed preview replacement discarded the valid session");
    Require(releases==0,"Failed Timeline preview replacement released the retained renderer scene");
    Require(TimelineAssetIO::Save(path,working) && source.Assets().Manager().RefreshAsset(meta->id),"Timeline final save/reload failed");
    const auto fresh=TimelineAssetIO::Load(path);
    Require(fresh && preview.Open(source,owner.Entity(),{},meta->id,*fresh,error),"Saved Timeline did not reopen in the editor preview");
    Require(preview.Play(1,false,error) && preview.Tick(0.5F,error),"Saved Timeline playback failed");
    Require(NearlyEqual(preview.Scene()->Transforms().Get(preview.Owner()).localPosition.x,10) &&
        preview.Scripts()->SharedState().Get("count")->AsInt()==1,"Saved Timeline differs from the authored preview");
    preview.Clear(); Require(!preview.Scene() && !preview.Playing() && releases==2,"Timeline preview did not release its runtime and renderer resources");
    std::filesystem::remove_all(root);
}
} // namespace kb::editor::tests
