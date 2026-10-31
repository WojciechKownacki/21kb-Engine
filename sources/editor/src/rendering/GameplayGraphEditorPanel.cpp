#include "rendering/GameplayGraphEditorPanel.hpp"
#if defined(_WIN32)
#include "scene/EditorSceneContext.hpp"
#include "scene/GameplayGraphEditorState.hpp"
#include "rendering/material_graph/MaterialGraphCanvas.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "engine/visual/VisualGraphLiteral.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace kb::editor {
namespace {
using namespace kb::visual;
constexpr std::array<const char*,18> buttons{"Save","Undo","Redo","Add node","Delete node","Edit value",
    "Disconnect","Attach selected","Run event","Pause / Resume","Step","Breakpoint",
    "Validate","Revert","Fit","Zoom +","Zoom -","Event"};
RECT Rect(DockRect r) { return {r.x,r.y,r.x+r.width,r.y+r.height}; }
bool Hit(DockRect r,int x,int y) { return r.Contains(x,y); }
DockRect Work(DockRect r) { if (r.width>=650) r.width-=260; return r; }
DockRect Button(DockRect r,std::size_t index) {
    const int width=std::max(1,(r.width-12)/6);
    return {r.x+6+static_cast<int>(index%6)*width,r.y+26+static_cast<int>(index/6)*28,width-3,24};
}
DockRect Canvas(DockRect r) { r=Work(r);return {r.x+6,r.y+114,std::max(1,r.width-12),std::max(1,r.height-190)}; }
MaterialGraphCanvas BuildCanvas(const GameplayGraphEditorState& state,DockRect content) {
    MaterialGraphCanvas canvas; const auto r=Canvas(content);
    canvas.SetViewport({static_cast<float>(r.x),static_cast<float>(r.y),static_cast<float>(r.width),static_cast<float>(r.height)});
    canvas.SetView(state.panX,state.panY,state.zoom);
    const auto& graph=state.document.Snapshot().graph;
    for (const auto& node:graph.nodes) {
        MaterialGraphCanvasNode view;
        view.stableId=std::to_string(node.id);
        view.title=std::to_string(node.id)+" "+(node.kind==VisualGraphNodeKind::Event?ToString(node.lifecycle):node.symbol.empty()?ToString(node.kind):VisualGraphLiteral::IsSymbol(node.symbol)?"Literal":node.symbol);
        for (const auto& point:state.document.Snapshot().positions) if (point.nodeId==node.id) { view.x=point.x;view.y=point.y; }
        for (const auto& pin:graph.pins) if (pin.nodeId==node.id) {
            MaterialGraphCanvasPin entry;entry.label=pin.name;entry.stableId=pin.name;
            entry.hasCustomColor=true;entry.customColor=pin.type==VisualGraphValueType::Void?MaterialGraphCanvasColor{0.9F,0.9F,0.9F,1}:MaterialGraphCanvasColor{0.3F,0.7F,0.9F,1};
            (pin.direction==VisualGraphPinDirection::Output?view.outputs:view.inputs).push_back(std::move(entry));
        }
        if (VisualGraphLiteral::IsSymbol(node.symbol)) {
            const auto text=VisualGraphLiteral::Text(node.symbol);
            view.valueFields.push_back({.editable=false,.label="Value",.text=text.value_or("Invalid")});
        }
        static_cast<void>(canvas.AddNode(std::move(view)));
    }
    for (const auto& node:canvas.Nodes()) canvas.AddOccluderWorld(canvas.NodeBoundsWorld(node));
    for (std::size_t e=0;e<graph.edges.size();++e) {
        const auto& edge=graph.edges[e];std::optional<std::uint32_t> from,to,output,input;
        for (std::size_t i=0;i<graph.nodes.size();++i) {
            if (graph.nodes[i].id==edge.fromNode) { from=static_cast<std::uint32_t>(i);const auto& pins=canvas.Nodes()[i].outputs;for (std::size_t p=0;p<pins.size();++p) if (pins[p].label==edge.fromPin) output=static_cast<std::uint32_t>(p); }
            if (graph.nodes[i].id==edge.toNode) { to=static_cast<std::uint32_t>(i);const auto& pins=canvas.Nodes()[i].inputs;for (std::size_t p=0;p<pins.size();++p) if (pins[p].label==edge.toPin) input=static_cast<std::uint32_t>(p); }
        }
        if (from && to && output && input) canvas.AddLink({*from,*output,*to,*input,std::to_string(e)});
    }
    return canvas;
}
void Fit(GameplayGraphEditorState& state,DockRect content) {
    const auto canvas=BuildCanvas(state,content);if (canvas.Nodes().empty()) return;
    float minX=1000000,minY=1000000,maxX=-1000000,maxY=-1000000;
    for (const auto& node:canvas.Nodes()) { const auto r=canvas.NodeBoundsWorld(node);minX=std::min(minX,r.x);minY=std::min(minY,r.y);maxX=std::max(maxX,r.x+r.width);maxY=std::max(maxY,r.y+r.height); }
    const auto r=Canvas(content);state.zoom=std::clamp(std::min((static_cast<float>(r.width)-32)/(maxX-minX),(static_cast<float>(r.height)-32)/(maxY-minY)),0.1F,1.5F);
    state.panX=minX-16/state.zoom;state.panY=minY-16/state.zoom;
}
std::optional<VisualGraphNodeCatalogEntry> PickNode(HWND owner,const GameplayGraphEditorState& state) {
    const auto filter=EditorTextEntryDialog::Show(owner,{.title="Gameplay Graph",.label="Find a node",.value="",.hint="Enter part of a function name, literal, Event, Branch or Wait"});
    if (!filter) return std::nullopt;
    std::vector<VisualGraphNodeCatalogEntry> entries;
    const auto lower=[](std::string text) { for (auto& c:text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text; };
    const auto wanted=lower(*filter);
    for (const auto& entry:state.catalog.Entries()) {
        if (entry.symbol.empty() && entry.kind!=VisualGraphNodeKind::Event && entry.kind!=VisualGraphNodeKind::CustomEvent && entry.kind!=VisualGraphNodeKind::Sequence &&
            entry.kind!=VisualGraphNodeKind::Branch && entry.kind!=VisualGraphNodeKind::Wait && entry.kind!=VisualGraphNodeKind::Comment) continue;
        if (lower(entry.displayName).find(wanted)!=std::string::npos || lower(entry.category).find(wanted)!=std::string::npos) entries.push_back(entry);
    }
    std::ranges::sort(entries,{},&VisualGraphNodeCatalogEntry::displayName);
    if (entries.empty()) { MessageBoxW(owner,L"No matching nodes.",L"Gameplay Graph",MB_OK);return std::nullopt; }
    const auto menu=CreatePopupMenu();
    for (std::size_t i=0;i<std::min<std::size_t>(entries.size(),80);++i) {
        const auto label=entries[i].displayName;const int size=MultiByteToWideChar(CP_UTF8,0,label.data(),static_cast<int>(label.size()),nullptr,0);
        std::wstring wide(static_cast<std::size_t>(size),L' ');MultiByteToWideChar(CP_UTF8,0,label.data(),static_cast<int>(label.size()),wide.data(),size);
        AppendMenuW(menu,MF_STRING,i+1,wide.c_str());
    }
    if (entries.size()>80) AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"Refine the search to see more nodes");
    POINT point{};GetCursorPos(&point);const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,owner,nullptr);DestroyMenu(menu);
    if (!selected || selected>std::min<std::size_t>(entries.size(),80)) return std::nullopt;
    auto entry=entries[selected-1];
    if (entry.kind==VisualGraphNodeKind::CustomEvent || entry.kind==VisualGraphNodeKind::Comment || entry.kind==VisualGraphNodeKind::Event) {
        const bool lifecycle=entry.kind==VisualGraphNodeKind::Event;
        const auto value=EditorTextEntryDialog::Show(owner,{.title="Gameplay Graph",.label=lifecycle?"Lifecycle event":"Node name",.value=lifecycle?"Ready":"CustomEvent"});
        if (!value) return std::nullopt;
        if (lifecycle) { if (!TryParseVisualGraphLifecycleEvent(*value,entry.lifecycle)) return std::nullopt; }
        else entry.symbol=*value;
    }
    return entry;
}
bool EditValue(HWND owner,GameplayGraphEditorState& state) {
    const auto* node=state.document.Snapshot().graph.FindNode(state.selected);if (!node) return false;
    if (node->kind==VisualGraphNodeKind::Event) {
        const auto text=EditorTextEntryDialog::Show(owner,{.title="Gameplay Graph",.label="Lifecycle event",.value=ToString(node->lifecycle)});
        VisualGraphLifecycleEvent event;
        return text && TryParseVisualGraphLifecycleEvent(*text,event) && state.document.SetEvent(node->id,event,state.error);
    }
    const bool literal=VisualGraphLiteral::IsSymbol(node->symbol);
    if (!literal && node->kind!=VisualGraphNodeKind::Comment && node->kind!=VisualGraphNodeKind::CustomEvent) { state.error="Replace a binding node from the catalog to change its signature";return false; }
    const auto text=EditorTextEntryDialog::Show(owner,{.title="Gameplay Graph",.label=literal?"Typed literal value":"Node name",.value=literal?VisualGraphLiteral::Text(node->symbol).value_or(""):node->symbol});
    return text && (literal?state.document.SetLiteral(node->id,*text,state.error):state.document.SetSymbol(node->id,*text,state.error));
}
} // namespace

void GameplayGraphEditorPanel::Paint(HDC dc,DockRect content,const EditorTheme& theme,const EditorSceneContext& context) {
    const ScopedFont font{11,FW_NORMAL};const ScopedGdiObject selectedFont(dc,font.handle),pen(dc,GetStockObject(DC_PEN)),brush(dc,GetStockObject(DC_BRUSH));
    const auto color=[](EditorColor c) { return GdiDrawing::ToColorRef(c); };
    const auto text=[&](DockRect r,std::string_view value,COLORREF tint) { auto rect=Rect(r);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,tint);DrawTextUtf8(dc,value.data(),static_cast<int>(value.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX); };
    GdiDrawing::FillRectColor(dc,Rect(content),color(theme.panel));
    const auto* state=context.GameplayGraphEditor();
    if (!state) { text(content,"Open a Visual Graph asset in Project Files.",color(theme.textSecondary));return; }
    const auto work=Work(content);const auto area=Canvas(content);const auto canvas=BuildCanvas(*state,content);const auto& graph=state->document.Snapshot().graph;
    text({content.x+8,content.y,content.width-16,24},graph.name+(state->document.Dirty()?" *":"")+"  |  "+ToString(state->event)+(state->paused?"  Paused":state->playing?"  Running":"  Stopped"),color(theme.textPrimary));
    for (std::size_t i=0;i<buttons.size();++i) { const auto r=Button(work,i);GdiDrawing::DrawSharpFrame(dc,Rect(r),color(theme.toolbarButton),color(theme.borderPanel));text({r.x+3,r.y,r.width-6,r.height},buttons[i],color(theme.textPrimary)); }
    GdiDrawing::DrawSharpFrame(dc,Rect(area),color(theme.background),color(theme.borderPanel));
    const int saved=SaveDC(dc);IntersectClipRect(dc,area.x,area.y,area.x+area.width,area.y+area.height);
    const auto pixel=[&](MaterialGraphCanvasPoint p) { return POINT{static_cast<LONG>(p.x),static_cast<LONG>(p.y)}; };
    for (const auto& link:canvas.Links()) {
        const auto a=canvas.PinCenterWindow(link.fromNode,link.fromPin,true),b=canvas.PinCenterWindow(link.toNode,link.toPin,false);
        const auto tint=state->selectedEdge && std::to_string(*state->selectedEdge)==link.stableId?theme.accent:theme.textSecondary;
        const float dx=std::max(48.0F*state->zoom,std::abs(b.x-a.x)*0.5F);
        const POINT curve[]{pixel(a),pixel({a.x+dx,a.y}),pixel({b.x-dx,b.y}),pixel(b)};
        SetDCPenColor(dc,color(tint));PolyBezier(dc,curve,4);
    }
    for (std::size_t i=0;i<canvas.Nodes().size();++i) {
        const auto& view=canvas.Nodes()[i];const auto id=graph.nodes[i].id;
        const auto world=canvas.NodeBoundsWorld(view);const auto top=canvas.WorldToLocal({world.x,world.y});
        const DockRect r{area.x+static_cast<int>(top.x),area.y+static_cast<int>(top.y),static_cast<int>(world.width*state->zoom),static_cast<int>(world.height*state->zoom)};
        const bool visited=std::ranges::find(state->visited,id)!=state->visited.end();
        GdiDrawing::DrawSharpFrame(dc,Rect(r),color(theme.panel),color(id==state->selected?theme.accent:visited?theme.textPrimary:theme.borderPanel));
        GdiDrawing::FillRectColor(dc,RECT{r.x+1,r.y+1,r.x+r.width-1,r.y+static_cast<int>(30*state->zoom)},color(theme.chrome));
        text({r.x+8,r.y,r.width-16,static_cast<int>(30*state->zoom)},view.title,color(theme.textPrimary));
        if (std::ranges::find(state->breakpoints,id)!=state->breakpoints.end()) { SetDCBrushColor(dc,RGB(235,90,80));Ellipse(dc,r.x+2,r.y+2,r.x+10,r.y+10); }
        for (const bool output:{false,true}) {
            const auto& pins=output?view.outputs:view.inputs;
            for (std::size_t p=0;p<pins.size();++p) {
                const auto point=pixel(canvas.PinCenterWindow(static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(p),output));
                SetDCPenColor(dc,color(theme.textPrimary));SetDCBrushColor(dc,color(theme.accent));Ellipse(dc,point.x-4,point.y-4,point.x+4,point.y+4);
                text({output?point.x-100:point.x+8,point.y-9,92,18},pins[p].label,color(theme.textPrimary));
            }
        }
        if (!view.valueFields.empty()) text({r.x+10,r.y+r.height-26,r.width-20,22},view.valueFields.front().text,color(theme.textSecondary));
    }
    RestoreDC(dc,saved);
    if (work.width<content.width) {
        int y=content.y+114;const int x=content.x+work.width+8,width=content.width-work.width-16;
        text({x,y,width,22},"Selected node / live pin values",color(theme.textPrimary));y+=26;
        if (const auto* node=graph.FindNode(state->selected)) {
            text({x,y,width,22},std::to_string(node->id)+" "+ToString(node->kind),color(theme.textPrimary));y+=24;
            text({x,y,width,22},VisualGraphLiteral::Text(node->symbol).value_or(node->symbol),color(theme.textSecondary));y+=28;
            for (const auto& pin:graph.pins) if (pin.nodeId==node->id && y<content.y+content.height-90) {
                const bool output=pin.direction==VisualGraphPinDirection::Output;
                text({x,y,width,20},std::string(output?"Out ":"In ")+pin.name+" : "+ToString(pin.type),color(theme.textPrimary));y+=20;
                text({x+8,y,width-8,20},state->PinValue(node->id,pin.name,output),color(theme.textSecondary));y+=22;
            }
        }
    }
    std::string status=state->error.empty()?state->wire?"Select an input pin to connect. Click empty canvas to cancel.":"Drag nodes to move; drag empty canvas to pan. Select a wire to disconnect.":state->error;
    if (state->paused && state->PauseLocation().valid) status="Paused before node "+std::to_string(state->PauseLocation().nodeId)+". "+status;
    text({work.x+8,work.y+work.height-72,work.width-16,22},status,color(theme.textSecondary));
    for (std::size_t i=0;i<std::min<std::size_t>(state->diagnostics.size(),2);++i) {
        const auto& d=state->diagnostics[i];text({work.x+8,work.y+work.height-48+static_cast<int>(i)*22,work.width-16,22},"Node "+std::to_string(d.nodeId)+" "+d.pinName+": "+d.message,color(theme.textSecondary));
    }
}

bool GameplayGraphEditorPanel::PointerDown(HWND owner,DockRect content,int x,int y,EditorSceneContext& context) {
    auto* state=context.GameplayGraphEditor();if (!state) return false;
    state->keyboardFocus=true;
    for (std::size_t i=0;i<buttons.size();++i) if (Hit(Button(Work(content),i),x,y)) {
        bool changed=false;
        switch (i) {
        case 0:return state->Save(context.Scene());
        case 1:changed=state->document.Undo();break;
        case 2:changed=state->document.Redo();break;
        case 3:if (const auto entry=PickNode(owner,*state)) { const auto world=BuildCanvas(*state,content).WindowToWorld(static_cast<float>(x),static_cast<float>(y+120));state->selected=state->document.AddNode(*entry,world.x,world.y,state->error);changed=state->selected!=0; }break;
        case 4:changed=state->document.RemoveNode(state->selected,state->error);if (changed) state->selected=0;break;
        case 5:changed=EditValue(owner,*state);break;
        case 6:if (state->selectedEdge) changed=state->document.Disconnect(*state->selectedEdge,state->error);break;
        case 7:return context.AttachScriptToEntity(context.SelectedEntity(),state->assetId);
        case 8:if (!state->PreviewScene()) return state->BuildPreview(context.Scene(),context.Project(),context.SelectedEntity());state->Resume();return true;
        case 9:if (state->paused) state->Resume();else state->paused=true;return true;
        case 10:if (!state->PreviewScene() && !state->BuildPreview(context.Scene(),context.Project(),context.SelectedEntity())) return false;return state->Step();
        case 11:return state->ToggleBreakpoint(state->selected);
        case 12:return state->Validate();
        case 13:if (!state->document.Dirty() || MessageBoxW(owner,L"Discard unsaved graph edits?",L"Gameplay Graph",MB_YESNO|MB_ICONQUESTION)==IDYES) changed=state->document.Load(state->path,state->error);break;
        case 14:Fit(*state,content);return true;
        case 15:state->zoom=std::min(2.0F,state->zoom*1.2F);return true;
        case 16:state->zoom=std::max(0.1F,state->zoom/1.2F);return true;
        case 17: {
            const auto text=EditorTextEntryDialog::Show(owner,{.title="Gameplay Graph",.label="Preview lifecycle event",.value=ToString(state->event)});
            VisualGraphLifecycleEvent event;if (!text || !TryParseVisualGraphLifecycleEvent(*text,event)) return false;
            state->StopPreview();state->event=event;return true;
        }
        }
        if (changed) state->Edited();return changed;
    }
    const auto canvas=BuildCanvas(*state,content);const auto& graph=state->document.Snapshot().graph;
    if (const auto pin=canvas.HitTestPin(static_cast<float>(x),static_cast<float>(y))) {
        const auto id=graph.nodes[pin->node].id;const auto* node=canvas.NodeAt(pin->node);
        const auto name=(pin->output?node->outputs:node->inputs)[pin->pin].label;
        state->selected=id;
        if (pin->output) state->wire=std::pair{id,name};
        else if (state->wire) { const auto wire=*state->wire;state->wire.reset();if (state->document.Connect(wire.first,wire.second,id,name,state->error)) state->Edited(); }
        return true;
    }
    if (const auto link=canvas.HitTestLink(static_cast<float>(x),static_cast<float>(y))) {
        state->selectedEdge=static_cast<std::size_t>(std::stoull(canvas.Links()[*link].stableId));return true;
    }
    if (!Hit(Canvas(content),x,y)) return false;
    state->wire.reset();state->selectedEdge.reset();
    const auto hit=canvas.HitTestNode(static_cast<float>(x),static_cast<float>(y));
    state->selected=hit?graph.nodes[*hit].id:0;state->dragging=true;state->StopPreview();
    if (hit) { const auto world=canvas.WindowToWorld(static_cast<float>(x),static_cast<float>(y));const auto* node=canvas.NodeAt(*hit);state->dragX=world.x-node->x;state->dragY=world.y-node->y;state->document.BeginGroup(); }
    else { state->dragX=static_cast<float>(x)+state->panX*state->zoom;state->dragY=static_cast<float>(y)+state->panY*state->zoom; }
    SetCapture(owner);return true;
}
bool GameplayGraphEditorPanel::PointerMove(DockRect content,int x,int y,EditorSceneContext& context) {
    auto* state=context.GameplayGraphEditor();if (!state || !state->dragging) return false;
    if (state->selected) { const auto world=BuildCanvas(*state,content).WindowToWorld(static_cast<float>(x),static_cast<float>(y));return state->document.MoveNode(state->selected,world.x-state->dragX,world.y-state->dragY,state->error); }
    state->panX=(state->dragX-static_cast<float>(x))/state->zoom;state->panY=(state->dragY-static_cast<float>(y))/state->zoom;return true;
}
void GameplayGraphEditorPanel::PointerUp(HWND owner,EditorSceneContext& context) {
    if (auto* state=context.GameplayGraphEditor()) { state->dragging=false;state->document.EndGroup(); }
    if (GetCapture()==owner) ReleaseCapture();
}
std::optional<POINT> GameplayGraphEditorPanel::NodePoint(DockRect content,const EditorSceneContext& context,std::uint32_t node,std::string_view pin,bool output) {
    const auto* state=context.GameplayGraphEditor();if (!state) return std::nullopt;
    const auto canvas=BuildCanvas(*state,content);const auto& graph=state->document.Snapshot().graph;
    for (std::size_t i=0;i<graph.nodes.size();++i) if (graph.nodes[i].id==node) {
        if (pin.empty()) { const auto* view=canvas.NodeAt(static_cast<std::uint32_t>(i));const auto p=canvas.WorldToLocal({view->x+40,view->y+12});const auto r=Canvas(content);return POINT{r.x+static_cast<LONG>(p.x),r.y+static_cast<LONG>(p.y)}; }
        const auto& pins=output?canvas.Nodes()[i].outputs:canvas.Nodes()[i].inputs;
        for (std::size_t p=0;p<pins.size();++p) if (pins[p].label==pin) {
            const auto point=canvas.PinCenterWindow(static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(p),output);return POINT{static_cast<LONG>(point.x),static_cast<LONG>(point.y)};
        }
    }
    return std::nullopt;
}
std::optional<POINT> GameplayGraphEditorPanel::ButtonPoint(DockRect content,std::string_view label) {
    const auto found=std::ranges::find(buttons,label);if (found==buttons.end()) return std::nullopt;
    const auto rect=Button(Work(content),static_cast<std::size_t>(found-buttons.begin()));return POINT{rect.x+rect.width/2,rect.y+rect.height/2};
}
bool GameplayGraphEditorPanel::ResolveClose(HWND owner,EditorSceneContext& context) {
    auto* state=context.GameplayGraphEditor();if (!state) return true;
    PointerUp(owner,context);
    if (state->document.Dirty()) {
        const auto choice=MessageBoxW(owner,L"Save changes to the gameplay graph?",L"Gameplay Graph",MB_YESNOCANCEL|MB_ICONQUESTION);
        if (choice==IDCANCEL || (choice==IDYES && !state->Save(context.Scene()))) return false;
        if (choice==IDNO && !state->document.Load(state->path,state->error)) return false;
    }
    state->StopPreview();return true;
}
} // namespace kb::editor
#endif
