#include "EditorTestSupport.hpp"
#include "scene/GameplayGraphEditorState.hpp"
#include "assets/EditorAssetOpenPolicy.hpp"
#include "app/EditorAssetBrowserNativeCommandMap.hpp"
#include "assets/EditorAssetBrowserContextMenuState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace kb::editor::tests {
void RunEditorGameplayGraphTests() {
    using namespace kb::visual;
    Require(EditorAssetOpenPolicy::CanOpen({.type="VisualGraph"}),"Gameplay graph asset cannot be opened");
    const auto command=EditorAssetContextCommand::NewGameplayGraph;
    Require(EditorAssetBrowserNativeCommandMap::Command(EditorAssetBrowserNativeCommandMap::Id(command))==command,"Gameplay graph creation has no native command");
    EditorAssetBrowserContextMenuState menu;menu.OpenForBackground(0,0,"/Game");
    const auto items=menu.Items(false,false);
    Require(std::ranges::any_of(items,[&](const auto& item) { return item.command==command; }),"Gameplay graph creation is missing from the content menu");
    const auto root=std::filesystem::temp_directory_path()/("kb_graph_editor_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Assets");
    const auto file=root/"Assets/Motion.kbgraph";
    VisualGraphAuthoringDocument initial;initial.Create("Motion");std::string error;
    Require(initial.Save(file,error),"Gameplay graph fixture could not be saved");
    kb::scene::Scene source;
    Require(source.Assets().MountProject(root) && source.Assets().Discover()==1,"Gameplay graph discovery failed");
    const auto* metadata=source.Assets().Manager().Registry().FindByPath("/Game/Motion.kbgraph");
    Require(metadata!=nullptr,"Gameplay graph metadata is missing");
    const auto asset=metadata->id;
    const auto owner=source.Entities().CreateObject({.name="Actor"}).Entity();
    GameplayGraphEditorState state;
    Require(state.Load(source,asset) && !state.document.Dirty(),"Gameplay graph editor did not open cleanly");
    const auto binding=std::ranges::find(state.catalog.Entries(),"Function.Transform.Translate",&VisualGraphNodeCatalogEntry::symbol);
    Require(binding!=state.catalog.Entries().end(),"Gameplay graph catalog is missing a live transform binding");
    const auto translate=state.document.AddNode(*binding,300,32,error);
    const auto self=state.document.AddNode(*state.catalog.Find("Value:Self"),32,140,error);
    const auto amount=state.document.AddNode(*state.catalog.Find("Literal:Float"),32,250,error);
    const auto zero=state.document.AddNode(*state.catalog.Find("Literal:Float"),32,360,error);
    const auto after=state.document.AddNode(*state.catalog.Find("BuiltIn:Sequence"),600,32,error);
    Require(translate && self && amount && zero && after && state.document.SetLiteral(amount,"0.5",error),"Gameplay graph editor could not author nodes");
    Require(state.document.Connect(1,"then",translate,"exec",error) && state.document.Connect(translate,"then",after,"exec",error) &&
        state.document.Connect(self,"value",translate,"entity",error) && state.document.Connect(amount,"value",translate,"x",error) &&
        state.document.Connect(zero,"value",translate,"y",error) && state.document.Connect(zero,"value",translate,"z",error),"Gameplay graph editor could not wire a movement graph");
    Require(state.Validate() && state.Save(source),"Authored gameplay graph failed binding validation/save");
    const auto bytes=[&]() { std::ifstream input(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>{input},{}); };
    const auto saved=bytes();
    Require(state.ToggleBreakpoint(translate) && !state.ToggleBreakpoint(amount),"Gameplay graph execution breakpoint policy failed");
    Require(state.BuildPreview(source,{},owner),"Gameplay graph isolated preview did not build");
    Require(state.Execute(1.0F/60) && state.paused && state.PauseLocation().nodeId==translate,"Gameplay graph preview did not stop at its breakpoint");
    Require(state.PreviewScene()->Transforms().Get(state.PreviewOwner()).localPosition.x==0 && source.Transforms().Get(owner).localPosition.x==0,"Paused graph mutated its owner before executing");
    Require(state.Step() && state.paused && state.PauseLocation().nodeId==after,"Graph flow step did not evaluate data inputs atomically and stop at the next node");
    Require(state.PreviewScene()->Transforms().Get(state.PreviewOwner()).localPosition.x==0.5F &&
        state.PinValue(translate,"moved",true)=="true" && state.PinValue(translate,"x",false)=="0.5","Graph preview did not expose executed output/input values");
    Require(source.Transforms().Get(owner).localPosition.x==0 && bytes()==saved,"Graph preview modified the authored scene or asset file");
    Require(state.Step() && !state.paused && state.PreviewScene()->Transforms().Get(state.PreviewOwner()).localPosition.x==0.5F,"Resuming a paused graph repeated a previous operation");
    Require(state.document.SetLiteral(amount,"2",error),"Graph literal edit failed");state.Edited();
    Require(!state.PreviewScene() && state.document.Dirty() && bytes()==saved,"Editing did not discard stale graph preview or preserve the source file");
    Require(state.document.Undo() && !state.document.Dirty() && state.document.Redo() && state.document.Dirty(),"Graph Undo/Redo lost the saved document marker");
    Require(state.Save(source) && state.document.Load(file,error) && !state.document.Dirty(),"Edited graph did not save/reload");
    Require(!state.BuildPreview(source,{},{}),"Graph preview accepted a missing owner");
    state.StopPreview();
    std::error_code ignored;std::filesystem::remove_all(root,ignored);
}
} // namespace kb::editor::tests
