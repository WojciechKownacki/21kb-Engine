#include "scene/EditorSceneContext.hpp"
#include "scene/BlendSpace2DEditorState.hpp"
#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::editor {

bool EditorSceneContext::CreateBlendSpace2DAsset(const std::filesystem::path& virtualFolder) {
    auto& manager=scene_->Assets().Manager();
    const auto folderText=kb::assets::NormalizeAssetPath(virtualFolder);
    if (folderText!="/Game" && !folderText.starts_with("/Game/")) return false;
    if (blendSpaceEditor_ && blendSpaceEditor_->document.Dirty()) {
        console_.Error("BlendSpace2D","Save the current asset before creating another BlendSpace2D");return false;
    }
    const auto probe=manager.Mounts().Resolve(virtualFolder/"probe");
    if (!probe) return false;
    std::filesystem::path path=probe->parent_path()/"NewBlendSpace2D.kbblend2d";
    for (std::size_t suffix=1;std::filesystem::exists(path);++suffix)
        path=probe->parent_path()/("NewBlendSpace2D"+std::to_string(suffix)+".kbblend2d");
    std::string error;
    if (!kb::scene::BlendSpace2DAssetIO::Save(path,{},error)) { console_.Error("BlendSpace2D",error);return false; }
    static_cast<void>(scene_->Assets().Discover());
    const auto* metadata=manager.Registry().FindByPath(virtualFolder/path.filename());
    if (metadata && OpenBlendSpace2DEditorAsset(metadata->id) && assetBrowser_.SelectAsset(metadata->id,manager)) return true;
    std::error_code ignored;std::filesystem::remove(path,ignored);
    static_cast<void>(scene_->Assets().Discover());return false;
}

bool EditorSceneContext::OpenBlendSpace2DEditorAsset(kb::assets::AssetId id) {
    if (blendSpaceEditor_ && blendSpaceEditor_->document.AssetId()==id) { blendSpaceEditor_->openRequested=true;return true; }
    if (blendSpaceEditor_ && blendSpaceEditor_->document.Dirty()) {
        console_.Error("BlendSpace2D","Save the current asset before opening another BlendSpace2D");return false;
    }
    const auto* metadata=scene_->Assets().Manager().Registry().Find(id);
    if (!metadata || metadata->type!=kb::scene::kBlendSpace2DAssetType) return false;
    std::string error;
    const auto asset=kb::scene::BlendSpace2DAssetIO::Load(metadata->physicalPath,error);
    auto candidate=std::make_unique<BlendSpace2DEditorState>();
    if (!asset || !candidate->document.Open(id,*asset,error)) { console_.Error("BlendSpace2D",error);return false; }
    candidate->preview.SetReleaseHandler(renderSceneReleaseHandler_);
    blendSpaceEditor_=std::move(candidate);
    FitBlendSpace2DEditorView();
    if (scene_->Entities().IsAlive(SelectedEntity())) static_cast<void>(SetBlendSpace2DPreviewOwner(SelectedEntity()));
    return true;
}

bool EditorSceneContext::ConsumeBlendSpace2DEditorOpenRequest() noexcept {
    if (!blendSpaceEditor_ || !blendSpaceEditor_->openRequested) return false;
    blendSpaceEditor_->openRequested=false;return true;
}

bool EditorSceneContext::EditBlendSpace2DAsset(const std::function<bool(kb::scene::BlendSpace2DAsset&)>& edit) {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;
    auto candidate=state.document;
    if (!candidate.Apply(edit,state.error)) { if (!state.error.empty()) console_.Error("BlendSpace2D",state.error); return false; }
    if (!scene_->Entities().IsAlive(state.owner)) ClearBlendSpace2DPreview();
    const auto* working=candidate.WorkingCopy();
    if (working->samples.empty()) state.preview.Clear();
    else if (scene_->Entities().IsAlive(state.owner)) {
        const bool playing=state.preview.Playing();
        const float phase=state.preview.Scene()?state.preview.Phase():state.phase;
        if (!state.preview.Open(*scene_,state.owner,project_,candidate.AssetId(),*working,state.x,state.y,phase,state.error)) {
            console_.Error("BlendSpace2D",state.error);return false;
        }
        state.preview.SetPlaying(playing);
    }
    const auto sampleCount=working->samples.size();
    state.document=std::move(candidate);
    if (state.selected>=sampleCount) state.selected=std::numeric_limits<std::size_t>::max();
    ++state.revision;
    return true;
}

bool EditorSceneContext::UndoBlendSpace2DEditorEdit() {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;state.dragging=false;
    auto previous=state.document;
    if (!state.document.Undo()) return false;
    if (scene_->Entities().IsAlive(state.owner) && !SetBlendSpace2DPreviewOwner(state.owner)) { state.document=std::move(previous);return false; }
    ++state.revision;return true;
}
bool EditorSceneContext::RedoBlendSpace2DEditorEdit() {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;state.dragging=false;
    auto previous=state.document;
    if (!state.document.Redo()) return false;
    if (scene_->Entities().IsAlive(state.owner) && !SetBlendSpace2DPreviewOwner(state.owner)) { state.document=std::move(previous);return false; }
    ++state.revision;return true;
}
bool EditorSceneContext::SaveBlendSpace2DEditorAsset() {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;
    const auto* metadata=scene_->Assets().Manager().Registry().Find(state.document.AssetId());
    const auto* working=state.document.WorkingCopy();
    if (!metadata || !working || !kb::scene::BlendSpace2DAssetIO::Save(metadata->physicalPath,*working,state.error)) {
        console_.Error("BlendSpace2D",state.error.empty()?"BlendSpace2D save failed":state.error);return false;
    }
    if (!scene_->Assets().Manager().RefreshAsset(metadata->id)) { state.error="Asset saved, but its runtime refresh failed";console_.Error("BlendSpace2D",state.error);return false; }
    state.document.MarkSaved();++state.revision;return true;
}
bool EditorSceneContext::RevertBlendSpace2DEditorAsset() {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;
    const auto* metadata=scene_->Assets().Manager().Registry().Find(state.document.AssetId());
    if (!metadata) return false;
    const auto asset=kb::scene::BlendSpace2DAssetIO::Load(metadata->physicalPath,state.error);
    auto candidate=state.document;
    if (!asset || !candidate.Open(state.document.AssetId(),*asset,state.error)) return false;
    if (asset->samples.empty()) state.preview.Clear();
    else if (scene_->Entities().IsAlive(state.owner) && !state.preview.Open(*scene_,state.owner,project_,candidate.AssetId(),*asset,state.x,state.y,state.phase,state.error))
        return false;
    state.document=std::move(candidate);state.dragging=false;++state.revision;
    FitBlendSpace2DEditorView();return true;
}

bool EditorSceneContext::SetBlendSpace2DPreviewOwner(kb::scene::SceneEntity owner) {
    if (!blendSpaceEditor_ || !scene_->Entities().IsAlive(owner)) return false;
    auto& state=*blendSpaceEditor_;
    const auto* working=state.document.WorkingCopy();
    if (!working) return false;
    if (working->samples.empty()) { state.preview.Clear();state.owner=owner;return true; }
    if (!state.preview.Open(*scene_,owner,project_,state.document.AssetId(),*working,state.x,state.y,state.phase,state.error)) {
        console_.Error("BlendSpace2D",state.error);return false;
    }
    state.owner=owner;++state.revision;return true;
}
bool EditorSceneContext::SetBlendSpace2DPreviewCoordinates(float x,float y) {
    if (!blendSpaceEditor_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    auto& state=*blendSpaceEditor_;
    if (state.preview.Scene() && !state.preview.SetCoordinates(x,y,state.error)) return false;
    state.x=x;state.y=y;++state.revision;return true;
}
bool EditorSceneContext::SeekBlendSpace2DPreview(float phase) {
    if (!blendSpaceEditor_ || !std::isfinite(phase) || phase<0 || phase>1) return false;
    auto& state=*blendSpaceEditor_;state.preview.SetPlaying(false);
    if (state.preview.Scene() && !state.preview.Seek(phase,state.error)) return false;
    state.phase=phase;++state.revision;return true;
}
bool EditorSceneContext::PlayBlendSpace2DPreview(bool playing) {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;
    if (playing && !state.preview.Scene()) { state.error="Select a preview owner first";return false; }
    state.preview.SetPlaying(playing);++state.revision;return true;
}
bool EditorSceneContext::TickBlendSpace2DPreview(float delta) {
    if (!blendSpaceEditor_) return false;
    auto& state=*blendSpaceEditor_;
    if (!state.preview.Playing()) return false;
    const bool changed=state.preview.Tick(delta,state.error);
    if (changed) { state.phase=state.preview.Phase();++state.revision; }
    if (!state.error.empty()) console_.Error("BlendSpace2D",state.error);
    return changed;
}
void EditorSceneContext::ClearBlendSpace2DPreview() noexcept {
    if (!blendSpaceEditor_) return;
    blendSpaceEditor_->preview.Clear();blendSpaceEditor_->owner={};blendSpaceEditor_->dragging=false;
}
void EditorSceneContext::SynchronizeBlendSpace2DReleaseHandler() {
    if (blendSpaceEditor_) blendSpaceEditor_->preview.SetReleaseHandler(renderSceneReleaseHandler_);
}

void EditorSceneContext::FitBlendSpace2DEditorView() {
    if (!blendSpaceEditor_) return;
    auto& state=*blendSpaceEditor_;
    const auto* asset=state.document.WorkingCopy();
    if (!asset || asset->samples.empty()) { state.bounds={-1,1,-1,1};return; }
    double minX=asset->samples[0].x,maxX=minX,minY=asset->samples[0].y,maxY=minY;
    for (const auto& sample:asset->samples) {
        minX=std::min(minX,static_cast<double>(sample.x));maxX=std::max(maxX,static_cast<double>(sample.x));
        minY=std::min(minY,static_cast<double>(sample.y));maxY=std::max(maxY,static_cast<double>(sample.y));
    }
    const double magnitude=std::max({std::abs(minX),std::abs(maxX),std::abs(minY),std::abs(maxY)});
    const double extent=std::max({maxX-minX,maxY-minY,0.001,magnitude*0.0001});
    state.bounds={minX-extent*0.2,maxX+extent*0.2,minY-extent*0.2,maxY+extent*0.2};++state.revision;
}
void EditorSceneContext::SelectBlendSpace2DSample(std::size_t index) {
    if (!blendSpaceEditor_) return;
    blendSpaceEditor_->selected=index;
}
void EditorSceneContext::SetBlendSpace2DEditorFirstRow(std::size_t row) {
    if (blendSpaceEditor_) blendSpaceEditor_->firstRow=row;
}
bool EditorSceneContext::BeginBlendSpace2DSampleDrag(std::size_t index) {
    if (!blendSpaceEditor_ || !blendSpaceEditor_->document.WorkingCopy() || index>=blendSpaceEditor_->document.WorkingCopy()->samples.size()) return false;
    auto& state=*blendSpaceEditor_;state.selected=index;state.dragging=true;state.dragEdited=false;
    state.document.BeginGroup();state.preview.SetPlaying(false);return true;
}
bool EditorSceneContext::MoveBlendSpace2DSample(float x,float y) {
    if (!blendSpaceEditor_ || !blendSpaceEditor_->dragging) return false;
    auto& state=*blendSpaceEditor_;
    if (!state.document.Move(state.selected,x,y,state.error)) return false;
    state.dragEdited=true;++state.revision;return true;
}
bool EditorSceneContext::EndBlendSpace2DSampleDrag() {
    if (!blendSpaceEditor_ || !blendSpaceEditor_->dragging) return false;
    auto& state=*blendSpaceEditor_;state.dragging=false;state.document.EndGroup();
    if (state.dragEdited && scene_->Entities().IsAlive(state.owner) && !SetBlendSpace2DPreviewOwner(state.owner)) {
        static_cast<void>(state.document.Undo());return false;
    }
    ++state.revision;return true;
}
} // namespace kb::editor
