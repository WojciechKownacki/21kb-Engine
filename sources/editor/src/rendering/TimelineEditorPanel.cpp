#include "rendering/TimelineEditorPanel.hpp"
#if defined(_WIN32)
#include "scene/EditorSceneContext.hpp"
#include "scene/TimelineEditorModel.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/ui/layout/UICanvasGroup.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace kb::editor {
namespace {
constexpr int kToolbar = 70, kRuler = 30, kRow = 24;
constexpr std::array<const char*,14> kButtons{"Save", "Undo", "Redo", "Use selected", "Play", "Pause", "Reverse", "Loop", "Add", "Add key", "Delete", "Previous", "Next", "Revert"};
RECT Rect(DockRect r) { return {r.x,r.y,r.x+r.width,r.y+r.height}; }
DockRect Table(DockRect content) { return {content.x,content.y,content.width >= 760 ? content.width * 3 / 5 : content.width,content.height}; }
DockRect Button(DockRect table, std::size_t index) {
    const int width = std::max(1, (table.width - 12) / 7);
    return {table.x+6+static_cast<int>(index%7)*width,table.y+6+static_cast<int>(index/7)*30,width-3,26};
}
std::size_t Capacity(DockRect table) { return static_cast<std::size_t>(std::max(1,(table.height-kToolbar-kRuler-30)/kRow)); }
std::optional<std::string> Prompt(HWND owner, const std::string& label, const std::string& value, const std::string& hint = {}) {
    return EditorTextEntryDialog::Show(owner,{.title="Timeline",.label=label,.value=value,.hint=hint});
}
std::optional<std::string> PickProperty(HWND owner, const EditorSceneContext& context, const std::string& binding) {
    const auto* scene = context.TimelineEditorPreview().Scene();
    const auto* asset = context.TimelineEditorDocument().WorkingCopy();
    if (!scene || !asset) return std::nullopt;
    auto target = context.TimelineEditorPreview().Owner();
    const auto definition = std::find_if(asset->bindings.begin(),asset->bindings.end(),[&](const auto& item) { return item.name==binding; });
    if (definition==asset->bindings.end()) return std::nullopt;
    std::istringstream path(definition->defaultPath); std::string segment;
    while (std::getline(path,segment,'/')) {
        if (segment==".") continue;
        kb::scene::SceneEntity next{};
        for (const auto child : scene->Hierarchy().ChildEntities(target)) if (scene->Entities().Name(child)==segment) {
            if (next.IsValid()) return std::nullopt;
            next=child;
        }
        if (!next.IsValid()) return std::nullopt;
        target=next;
    }
    const auto& world=scene->Runtime().EcsWorld();
    std::vector<std::string> references;
    const auto menu=CreatePopupMenu();
    for (const auto& component : world.InspectEntity(target).components) {
        const auto* reflection=world.Reflection(component.id); if (!reflection) continue;
        std::string label{reflection->Name()};
        if (label==kb::scene::LightComponent::StableId) label="Light";
        if (label==kb::scene::UICanvasGroup::StableId) label="Canvas Group";
        for (const auto& field : reflection->Fields()) {
            if (field.type==kb::ecs::ComponentFieldType::Bytes) continue;
            references.push_back(std::string{reflection->Name()}+'.'+field.name);
            const auto text=label+" / "+field.name;
            const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
            std::wstring wide(static_cast<std::size_t>(count),L' ');
            MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),count);
            AppendMenuW(menu,MF_STRING,references.size(),wide.c_str());
        }
    }
    POINT point{}; GetCursorPos(&point);
    const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,owner,nullptr);
    DestroyMenu(menu);
    return selected>0 && selected<=references.size() ? std::optional<std::string>{references[selected-1]} : std::nullopt;
}
void Add(HWND owner, EditorSceneContext& context) {
    const auto* working = context.TimelineEditorDocument().WorkingCopy(); if (!working) return;
    const auto menu = CreatePopupMenu();
    const std::array<const wchar_t*,8> labels{L"Binding",L"Transform",L"Property",L"Audio",L"Event",L"Sequence",L"Animation",L"Marker"};
    for (std::size_t i=0;i<labels.size();++i) AppendMenuW(menu,MF_STRING,i+1,labels[i]);
    POINT point{}; GetCursorPos(&point);
    const auto chosen = TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,owner,nullptr);
    DestroyMenu(menu); if (!chosen) return;
    const auto kind = static_cast<TimelineEditorTrackKind>(chosen-1);
    std::string binding, reference;
    if (kind != TimelineEditorTrackKind::Marker) {
        auto result = Prompt(owner,kind == TimelineEditorTrackKind::Binding ? "New binding name" : "Target binding",
            kind == TimelineEditorTrackKind::Binding ? "Target" : (working->bindings.empty() ? "Target" : working->bindings.front().name));
        if (!result) return; binding=*result;
    }
    if (kind == TimelineEditorTrackKind::Property) {
        const auto selected=PickProperty(owner,context,binding);
        if (!selected) return;
        reference=*selected;
    }
    if (kind == TimelineEditorTrackKind::Binding || kind == TimelineEditorTrackKind::Audio ||
        kind == TimelineEditorTrackKind::Animation || kind == TimelineEditorTrackKind::Sequence) {
        const auto result = Prompt(owner,kind == TimelineEditorTrackKind::Binding ? "Owner-relative path" :
            "Asset reference",kind == TimelineEditorTrackKind::Binding ? "." : "");
        if (!result) return; reference=*result;
    }
    std::shared_ptr<const kb::scene::TimelineAsset> child;
    if (kind == TimelineEditorTrackKind::Sequence) {
        auto& manager=context.Scene().Assets().Manager();
        const auto* meta=manager.Registry().FindByPath(reference);
        if (meta) { const auto handle=manager.Load<kb::scene::TimelineAsset>(meta->id); if (handle.IsLoaded()) child=std::make_shared<kb::scene::TimelineAsset>(*handle); }
    }
    static_cast<void>(context.EditTimelineAsset([&](auto& asset) {
        return TimelineEditorModel::Add(asset,kind,binding,reference,context.Scene().Runtime().EcsWorld(),child.get());
    }));
}
}

void TimelineEditorPanel::Paint(HDC dc, HWND host, DockRect content, const DockPanel& panel,
    const EditorTheme& theme, const EditorSceneContext& context, EditorSceneBgfxViewport* viewport) {
    const ScopedFont font{11,FW_NORMAL}; const ScopedGdiObject selected(dc,font.handle);
    const auto text=[&](DockRect r,std::string_view value,COLORREF tint) {
        auto rect=Rect(r); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,tint);
        DrawTextUtf8(dc,value.data(),static_cast<int>(value.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    };
    const auto color=[](EditorColor c) { return GdiDrawing::ToColorRef(c); };
    GdiDrawing::FillRectColor(dc,Rect(content),color(theme.panel));
    const auto* working=context.TimelineEditorDocument().WorkingCopy();
    if (!working) { text(content,"Open a Timeline asset in Project Files.",color(theme.textSecondary)); return; }
    const auto table=Table(content);
    for (std::size_t i=0;i<kButtons.size();++i) {
        const auto bounds=Button(table,i);
        GdiDrawing::DrawSharpFrame(dc,Rect(bounds),color(theme.toolbarButton),color(theme.borderPanel));
        std::string label=kButtons[i]; if (i==7 && context.TimelineEditorLoop()) label+=" *";
        text({bounds.x+4,bounds.y,bounds.width-8,bounds.height},label,color(theme.textPrimary));
    }
    const auto& preview=context.TimelineEditorPreview();
    const DockRect ruler{table.x+8,table.y+kToolbar,table.width-16,kRuler};
    GdiDrawing::FillRectColor(dc,Rect(ruler),color(theme.chrome));
    const float ratio=working->durationSeconds>0 ? std::clamp(preview.Time()/working->durationSeconds,0.0F,1.0F) : 0;
    const int cursor=ruler.x+static_cast<int>(ratio*ruler.width);
    GdiDrawing::FillRectColor(dc,RECT{cursor,ruler.y,cursor+2,ruler.y+ruler.height},color(theme.accent));
    char clock[100]{}; std::snprintf(clock,sizeof(clock),"%.3f / %.3f s  |  %s%s",preview.Time(),working->durationSeconds,
        preview.Playing()?"Playing":"Paused",context.TimelineEditorDocument().Dirty()?"  * unsaved":"");
    text(ruler,clock,color(theme.textPrimary));
    const auto rows=TimelineEditorModel::Rows(*working);
    const auto first=std::min(context.TimelineEditorFirstRow(),rows.size());
    for (std::size_t i=first;i<std::min(rows.size(),first+Capacity(table));++i) {
        DockRect bounds{table.x+6,table.y+kToolbar+kRuler+static_cast<int>(i-first)*kRow,table.width-12,kRow};
        if (i==context.TimelineEditorSelectedRow()) GdiDrawing::FillRectColor(dc,Rect(bounds),color(theme.toolbarButton));
        text({bounds.x+4,bounds.y,bounds.width/2,bounds.height},rows[i].label,color(theme.textPrimary));
        text({bounds.x+bounds.width/2,bounds.y,bounds.width/2,bounds.height},rows[i].value,color(theme.textSecondary));
    }
    text({table.x+8,table.y+table.height-28,table.width-16,24},"Click value to edit. Select a track heading to add a key or delete.",color(theme.textSecondary));
    if (table.width<content.width && preview.Scene() && viewport) {
        EditorSceneBgfxViewport::PresentSettings settings{}; settings.viewportKey=panel.id;
        settings.sceneFullSyncRequired=true; settings.selectionMaskEnabled=false; settings.selectionOutlineEnabled=false;
        viewport->Present(dc,host,RECT{content.x+table.width+4,content.y,content.x+content.width,content.y+content.height},*preview.Scene(),theme,settings);
    }
}

bool TimelineEditorPanel::Pointer(HWND owner, DockRect content, int x, int y, EditorSceneContext& context) {
    const auto* working=context.TimelineEditorDocument().WorkingCopy(); if (!working) return false;
    const auto table=Table(content); const auto rows=TimelineEditorModel::Rows(*working);
    const auto selected=context.TimelineEditorSelectedRow();
    for (std::size_t i=0;i<kButtons.size();++i) if (Button(table,i).Contains(x,y)) {
        switch(i) {
        case 0: return context.SaveTimelineEditorAsset();
        case 1: return context.UndoTimelineEditorEdit();
        case 2: return context.RedoTimelineEditorEdit();
        case 3: return context.SetTimelineEditorPreviewOwner(context.SelectedEntity());
        case 4: return context.PlayTimelineEditorPreview(1,context.TimelineEditorLoop());
        case 5: context.PauseTimelineEditorPreview(); return true;
        case 6: return context.PlayTimelineEditorPreview(-1,context.TimelineEditorLoop());
        case 7: context.SetTimelineEditorLoop(!context.TimelineEditorLoop()); return true;
        case 8: Add(owner,context); return true;
        case 9: return selected<rows.size() && rows[selected].addKey && context.EditTimelineAsset([&](auto& asset) { return rows[selected].addKey(asset,context.TimelineEditorPreview().Time()); });
        case 10: return selected<rows.size() && rows[selected].remove && context.EditTimelineAsset(rows[selected].remove);
        case 11: context.SetTimelineEditorFirstRow(context.TimelineEditorFirstRow()>Capacity(table)?context.TimelineEditorFirstRow()-Capacity(table):0); return true;
        case 12: context.SetTimelineEditorFirstRow(std::min(context.TimelineEditorFirstRow()+Capacity(table),rows.empty()?0:rows.size()-1)); return true;
        case 13: return context.TimelineEditorDocument().Dirty() ? ResolveClose(owner,context) : context.RevertTimelineEditorAsset();
        }
    }
    if (DockRect{table.x+8,table.y+kToolbar,table.width-16,kRuler}.Contains(x,y))
        return context.SeekTimelineEditorPreview(std::clamp(static_cast<float>(x-table.x-8)/std::max(1,table.width-16),0.0F,1.0F)*working->durationSeconds);
    if (x<table.x || x>=table.x+table.width || y<table.y+kToolbar+kRuler || y>=table.y+table.height-30) return false;
    const auto index=context.TimelineEditorFirstRow()+static_cast<std::size_t>((y-table.y-kToolbar-kRuler)/kRow);
    if (index>=rows.size()) return false;
    context.SetTimelineEditorSelectedRow(index);
    if (rows[index].edit) {
        const auto result=Prompt(owner,rows[index].label,rows[index].value);
        if (result) return context.EditTimelineAsset([&](auto& asset) { return rows[index].edit(asset,*result); });
    }
    return true;
}

bool TimelineEditorPanel::ResolveClose(HWND owner, EditorSceneContext& context) {
    context.PauseTimelineEditorPreview();
    if (!context.TimelineEditorDocument().Dirty()) return true;
    const auto result=EditorChoiceDialog::Show(owner,{.title="Unsaved Timeline",.message="Save changes to the Timeline?",
        .supportingText="The Timeline asset is shared by runtime instances.",.primaryLabel="Save",.secondaryLabel="Discard",.cancelLabel="Cancel"});
    if (result==EditorChoiceDialogResult::Primary) return context.SaveTimelineEditorAsset();
    if (result==EditorChoiceDialogResult::Secondary) return context.RevertTimelineEditorAsset();
    return false;
}
} // namespace kb::editor
#endif
