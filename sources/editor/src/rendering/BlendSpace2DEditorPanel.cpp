#include "rendering/BlendSpace2DEditorPanel.hpp"
#if defined(_WIN32)
#include "scene/EditorSceneContext.hpp"
#include "scene/BlendSpace2DEditorState.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <tuple>
#include <vector>

namespace kb::editor {
namespace {
constexpr int rowHeight=22;
constexpr std::array<const char*,12> buttons{"Save","Undo","Redo","Use selected","Play / Pause","Add sample","Edit sample","Remove","Fit","Previous","Next","Revert"};
RECT Rect(DockRect r) { return {r.x,r.y,r.x+r.width,r.y+r.height}; }
bool Hit(DockRect r,int x,int y) { return x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height; }
DockRect Left(DockRect r) { if (r.width>=900) r.width=r.width*3/5;return r; }
DockRect Button(DockRect r,std::size_t index) {
    const int width=std::max(1,(r.width-12)/6);
    return {r.x+6+static_cast<int>(index%6)*width,r.y+26+static_cast<int>(index/6)*30,width-3,26};
}
DockRect Ruler(DockRect r) { return {r.x+10,r.y+89,std::max(1,r.width-20),18}; }
DockRect Canvas(DockRect r) { return {r.x+10,r.y+114,std::max(1,r.width-20),std::clamp(r.height/2-30,40,300)}; }
int RowsY(DockRect r) { const auto canvas=Canvas(r);return canvas.y+canvas.height+26; }
std::size_t Capacity(DockRect r) { return static_cast<std::size_t>(std::max(1,(r.y+r.height-RowsY(r)-26)/rowHeight)); }
POINT ToPoint(DockRect canvas,const std::array<double,4>& bounds,double x,double y) {
    const double u=std::clamp((x-bounds[0])/(bounds[1]-bounds[0]),-4.0,5.0);
    const double v=std::clamp((y-bounds[2])/(bounds[3]-bounds[2]),-4.0,5.0);
    return {canvas.x+static_cast<LONG>(u*canvas.width),canvas.y+canvas.height-static_cast<LONG>(v*canvas.height)};
}
std::array<float,2> FromPoint(DockRect canvas,const std::array<double,4>& bounds,int x,int y) {
    const auto bounded=[](double value) { return static_cast<float>(std::clamp(value,-static_cast<double>(std::numeric_limits<float>::max()),static_cast<double>(std::numeric_limits<float>::max()))); };
    return {bounded(bounds[0]+static_cast<double>(x-canvas.x)/canvas.width*(bounds[1]-bounds[0])),
        bounded(bounds[2]+static_cast<double>(canvas.y+canvas.height-y)/canvas.height*(bounds[3]-bounds[2]))};
}
std::optional<std::string> PickClip(HWND owner,const EditorSceneContext& context) {
    std::vector<kb::assets::AssetMetadata> clips;
    for (const auto& metadata:context.Scene().Assets().Manager().Registry().All()) if (metadata.type==kb::scene::kAnimationClipAssetType) clips.push_back(metadata);
    std::ranges::sort(clips,[](const auto& a,const auto& b) { return std::tie(a.name,a.id.value)<std::tie(b.name,b.id.value); });
    if (clips.empty()) { MessageBoxW(owner,L"Import an Animation Clip before adding a sample.",L"BlendSpace2D",MB_OK|MB_ICONINFORMATION);return std::nullopt; }
    const auto menu=CreatePopupMenu();
    for (std::size_t i=0;i<clips.size();++i) {
        const auto& text=clips[i].name;
        const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
        std::wstring wide(static_cast<std::size_t>(count),L' ');
        MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),count);
        AppendMenuW(menu,MF_STRING,i+1,wide.c_str());
    }
    POINT cursor{};GetCursorPos(&cursor);
    const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,cursor.x,cursor.y,0,owner,nullptr);
    DestroyMenu(menu);
    return selected>0 && selected<=clips.size()?std::optional<std::string>{kb::assets::ToString(clips[selected-1].id)}:std::nullopt;
}
std::optional<std::array<float,2>> ParsePoint(std::string_view text) {
    const auto comma=text.find(',');if (comma==std::string_view::npos || comma!=text.rfind(',')) return std::nullopt;
    std::array words{text.substr(0,comma),text.substr(comma+1)};std::array<float,2> values{};
    for (std::size_t i=0;i<2;++i) {
        const auto first=words[i].find_first_not_of(" \t"),last=words[i].find_last_not_of(" \t");
        if (first==std::string_view::npos) return std::nullopt;
        const auto word=words[i].substr(first,last-first+1);
        const auto parsed=std::from_chars(word.data(),word.data()+word.size(),values[i]);
        if (parsed.ec!=std::errc{} || parsed.ptr!=word.data()+word.size() || !std::isfinite(values[i])) return std::nullopt;
    }
    return values;
}
bool EditSample(HWND owner,EditorSceneContext& context) {
    const auto* state=context.BlendSpace2DEditor();if (!state || !state->document.WorkingCopy()) return false;
    const auto& samples=state->document.WorkingCopy()->samples;const auto index=state->selected;
    if (index>=samples.size()) return false;
    const auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Coordinates");AppendMenuW(menu,MF_STRING,2,L"Animation Clip");
    POINT cursor{};GetCursorPos(&cursor);const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,cursor.x,cursor.y,0,owner,nullptr);DestroyMenu(menu);
    if (selected==2) {
        const auto clip=PickClip(owner,context);if (!clip) return false;
        return context.EditBlendSpace2DAsset([&](auto& asset) { asset.samples[index].clipReference=*clip;return true; });
    }
    if (selected!=1) return false;
    char initial[96]{};std::snprintf(initial,sizeof(initial),"%.9g,%.9g",samples[index].x,samples[index].y);
    const auto text=EditorTextEntryDialog::Show(owner,{.title="BlendSpace2D",.label="Sample coordinates x,y",.value=initial,.hint="Two finite numbers separated by a comma"});
    if (!text) return false;
    const auto point=ParsePoint(*text);
    if (!point) { MessageBoxW(owner,L"Enter two finite coordinates separated by a comma.",L"BlendSpace2D",MB_OK|MB_ICONERROR);return false; }
    return context.EditBlendSpace2DAsset([&](auto& asset) { asset.samples[index].x=(*point)[0];asset.samples[index].y=(*point)[1];return true; });
}
} // namespace

void BlendSpace2DEditorPanel::Paint(HDC dc,HWND host,DockRect content,const DockPanel& panel,
    const EditorTheme& theme,const EditorSceneContext& context,EditorSceneBgfxViewport* viewport) {
    const ScopedFont font{11,FW_NORMAL};const ScopedGdiObject selectedFont(dc,font.handle);
    const ScopedGdiObject selectedPen(dc,GetStockObject(DC_PEN)),selectedBrush(dc,GetStockObject(DC_BRUSH));
    const auto color=[](EditorColor value) { return GdiDrawing::ToColorRef(value); };
    const auto text=[&](DockRect rect,std::string_view value,COLORREF tint) {
        auto r=Rect(rect);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,tint);
        DrawTextUtf8(dc,value.data(),static_cast<int>(value.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    };
    const auto line=[&](POINT a,POINT b,COLORREF tint) { SetDCPenColor(dc,tint);MoveToEx(dc,a.x,a.y,nullptr);LineTo(dc,b.x,b.y); };
    GdiDrawing::FillRectColor(dc,Rect(content),color(theme.panel));
    const auto* state=context.BlendSpace2DEditor();
    if (!state || !state->document.WorkingCopy()) { text(content,"Open a BlendSpace2D asset in Project Files.",color(theme.textSecondary));return; }
    const auto* asset=state->document.WorkingCopy();const auto left=Left(content);const auto canvas=Canvas(left);
    const auto* metadata=context.Scene().Assets().Manager().Registry().Find(state->document.AssetId());
    text({left.x+8,left.y,left.width-16,24},(metadata?metadata->name:std::string{"BlendSpace2D"})+(state->document.Dirty()?" *":""),color(theme.textPrimary));
    for (std::size_t i=0;i<buttons.size();++i) {
        const auto button=Button(left,i);GdiDrawing::DrawSharpFrame(dc,Rect(button),color(theme.toolbarButton),color(theme.borderPanel));
        text({button.x+4,button.y,button.width-8,button.height},buttons[i],color(theme.textPrimary));
    }
    const auto ruler=Ruler(left);GdiDrawing::FillRectColor(dc,Rect(ruler),color(theme.chrome));
    const int timeX=ruler.x+static_cast<int>(std::clamp(state->phase,0.0F,1.0F)*ruler.width);
    GdiDrawing::FillRectColor(dc,RECT{timeX,ruler.y,timeX+2,ruler.y+ruler.height},color(theme.accent));
    char value[160]{};std::snprintf(value,sizeof(value),"X %.4g   Y %.4g   Phase %.3f  |  %s",state->x,state->y,state->phase,state->preview.Playing()?"Playing":"Paused");
    text(ruler,value,color(theme.textPrimary));
    GdiDrawing::DrawSharpFrame(dc,Rect(canvas),color(theme.background),color(theme.borderPanel));
    const int saved=SaveDC(dc);IntersectClipRect(dc,canvas.x,canvas.y,canvas.x+canvas.width,canvas.y+canvas.height);
    for (int i=1;i<10;++i) {
        line({canvas.x+i*canvas.width/10,canvas.y},{canvas.x+i*canvas.width/10,canvas.y+canvas.height},color(theme.borderPanel));
        line({canvas.x,canvas.y+i*canvas.height/10},{canvas.x+canvas.width,canvas.y+i*canvas.height/10},color(theme.borderPanel));
    }
    for (const auto& triangle:state->document.Plan()->Triangles()) for (std::size_t i=0;i<3;++i) {
        const auto& a=asset->samples[triangle[i]],&b=asset->samples[triangle[(i+1)%3]];
        line(ToPoint(canvas,state->bounds,a.x,a.y),ToPoint(canvas,state->bounds,b.x,b.y),color(theme.textSecondary));
    }
    std::string error;const auto weights=state->document.Plan()->Sample(state->x,state->y,error);
    const auto weightFor=[&](std::size_t index) { if (weights) for (std::size_t i=0;i<weights->count;++i) if (weights->values[i].sample==index) return weights->values[i].weight;return 0.0F; };
    const auto query=ToPoint(canvas,state->bounds,state->x,state->y);
    for (std::size_t i=0;i<asset->samples.size();++i) {
        const auto point=ToPoint(canvas,state->bounds,asset->samples[i].x,asset->samples[i].y);const float weight=weightFor(i);
        if (weight>0) line(query,point,color(theme.accent));
        SetDCPenColor(dc,color(theme.textPrimary));SetDCBrushColor(dc,color(i==state->selected?theme.accent:theme.toolbarButton));
        const int radius=5+static_cast<int>(weight*6);Ellipse(dc,point.x-radius,point.y-radius,point.x+radius,point.y+radius);
        text({point.x+radius+3,point.y-9,44,18},std::to_string(i),color(theme.textPrimary));
    }
    line({query.x-6,query.y},{query.x+7,query.y},color(theme.accent));line({query.x,query.y-6},{query.x,query.y+7},color(theme.accent));
    RestoreDC(dc,saved);
    text({left.x+10,RowsY(left)-22,left.width-20,20},"Index   X / Y       Weight       Animation Clip",color(theme.textSecondary));
    const auto first=std::min(state->firstRow,asset->samples.size());
    for (std::size_t i=first;i<std::min(asset->samples.size(),first+Capacity(left));++i) {
        const DockRect row{left.x+8,RowsY(left)+static_cast<int>(i-first)*rowHeight,left.width-16,rowHeight};
        if (i==state->selected) GdiDrawing::FillRectColor(dc,Rect(row),color(theme.toolbarButton));
        const auto& sample=asset->samples[i];kb::assets::AssetId id;const auto* clip=kb::assets::TryParseAssetId(sample.clipReference,id)?context.Scene().Assets().Manager().Registry().Find(id):nullptr;
        std::snprintf(value,sizeof(value),"%zu   %.4g / %.4g   %.3f",i,sample.x,sample.y,weightFor(i));
        text({row.x,row.y,row.width/2,row.height},value,color(theme.textPrimary));
        text({row.x+row.width/2,row.y,row.width/2,row.height},clip?clip->name:sample.clipReference,color(theme.textSecondary));
    }
    text({left.x+8,left.y+left.height-24,left.width-16,22},!state->error.empty()?state->error:state->dragging?"Release to preview. One Undo restores the whole move.":"Drag a sample to move it. Click empty space to preview weights.",color(theme.textSecondary));
    if (left.width<content.width && state->preview.Scene() && viewport) {
        EditorSceneBgfxViewport::PresentSettings settings{};settings.viewportKey=panel.id;settings.sceneRevision=state->revision;
        settings.sceneFullSyncRequired=true;settings.selectionMaskEnabled=false;settings.selectionOutlineEnabled=false;
        viewport->Present(dc,host,RECT{content.x+left.width+4,content.y,content.x+content.width,content.y+content.height},*state->preview.Scene(),theme,settings);
    }
}

bool BlendSpace2DEditorPanel::PointerDown(HWND owner,DockRect content,int x,int y,EditorSceneContext& context) {
    const auto* state=context.BlendSpace2DEditor();if (!state || !state->document.WorkingCopy()) return false;
    const auto left=Left(content);const auto* working=state->document.WorkingCopy();
    for (std::size_t i=0;i<buttons.size();++i) if (Hit(Button(left,i),x,y)) switch(i) {
        case 0:return context.SaveBlendSpace2DEditorAsset();case 1:return context.UndoBlendSpace2DEditorEdit();case 2:return context.RedoBlendSpace2DEditorEdit();
        case 3:return context.SetBlendSpace2DPreviewOwner(context.SelectedEntity());case 4:return context.PlayBlendSpace2DPreview(!state->preview.Playing());
        case 5: {
            const auto clip=PickClip(owner,context);if (!clip) return false;
            float px=state->x,py=state->y;
            const float step=static_cast<float>((state->bounds[1]-state->bounds[0])*0.05);
            for (unsigned attempt=0;attempt<257;++attempt) {
                if (!std::ranges::any_of(working->samples,[&](const auto& sample) { return sample.x==px && sample.y==py; })) break;
                px+=step;
            }
            const auto index=working->samples.size();
            if (!context.EditBlendSpace2DAsset([&](auto& asset) { asset.samples.push_back({px,py,*clip});return true; })) return false;
            context.SelectBlendSpace2DSample(index);context.FitBlendSpace2DEditorView();return true;
        }
        case 6:return EditSample(owner,context);
        case 7: { const auto index=state->selected;if (index>=working->samples.size()) return false;
            return context.EditBlendSpace2DAsset([&](auto& asset) { asset.samples.erase(asset.samples.begin()+static_cast<std::ptrdiff_t>(index));return true; }); }
        case 8:context.FitBlendSpace2DEditorView();return true;
        case 9:context.SetBlendSpace2DEditorFirstRow(state->firstRow>Capacity(left)?state->firstRow-Capacity(left):0);return true;
        case 10:context.SetBlendSpace2DEditorFirstRow(std::min(working->samples.size(),state->firstRow+Capacity(left)));return true;
        case 11:return context.RevertBlendSpace2DEditorAsset();default:break;
    }
    const auto ruler=Ruler(left);if (Hit(ruler,x,y)) return context.SeekBlendSpace2DPreview(std::clamp(static_cast<float>(x-ruler.x)/ruler.width,0.0F,1.0F));
    const auto canvas=Canvas(left);
    if (Hit(canvas,x,y)) {
        for (std::size_t i=0;i<working->samples.size();++i) {
            const auto point=ToPoint(canvas,state->bounds,working->samples[i].x,working->samples[i].y);
            const double dx=static_cast<double>(x)-point.x,dy=static_cast<double>(y)-point.y;
            if (dx*dx+dy*dy<=144) { if (!context.BeginBlendSpace2DSampleDrag(i)) return false;if (owner) SetCapture(owner);return true; }
        }
        const auto point=FromPoint(canvas,state->bounds,x,y);return context.SetBlendSpace2DPreviewCoordinates(point[0],point[1]);
    }
    if (y>=RowsY(left) && y<left.y+left.height-26 && x>=left.x && x<left.x+left.width) {
        const auto index=state->firstRow+static_cast<std::size_t>((y-RowsY(left))/rowHeight);
        if (index>=working->samples.size()) return false;
        context.SelectBlendSpace2DSample(index);return true;
    }
    return false;
}
bool BlendSpace2DEditorPanel::PointerMove(DockRect content,int x,int y,EditorSceneContext& context) {
    const auto* state=context.BlendSpace2DEditor();if (!state || !state->dragging) return false;
    const auto point=FromPoint(Canvas(Left(content)),state->bounds,x,y);return context.MoveBlendSpace2DSample(point[0],point[1]);
}
bool BlendSpace2DEditorPanel::PointerUp(HWND owner,EditorSceneContext& context) {
    const auto* state=context.BlendSpace2DEditor();if (!state || !state->dragging) return false;
    const bool result=context.EndBlendSpace2DSampleDrag();if (owner && GetCapture()==owner) ReleaseCapture();return result;
}
bool BlendSpace2DEditorPanel::ResolveClose(HWND owner,EditorSceneContext& context) {
    const auto* state=context.BlendSpace2DEditor();if (!state) return true;
    if (state->dragging && !PointerUp(owner,context)) return false;
    if (state->document.Dirty()) {
        const int choice=MessageBoxW(owner,L"Save changes to this BlendSpace2D asset?",L"BlendSpace2D",MB_YESNOCANCEL|MB_ICONQUESTION);
        if (choice==IDCANCEL) return false;
        if (choice==IDYES) { if (!context.SaveBlendSpace2DEditorAsset()) return false; }
        else if (!context.RevertBlendSpace2DEditorAsset()) return false;
    }
    static_cast<void>(context.PlayBlendSpace2DPreview(false));return true;
}
} // namespace kb::editor
#endif
