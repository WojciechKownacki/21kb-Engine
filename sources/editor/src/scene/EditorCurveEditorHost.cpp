#include "scene/EditorSceneContext.hpp"

#include "editor/ParticleEmitterInspectorModel.hpp"

#include <algorithm>

namespace kb::editor {

// The clip Curve track selected in the timeline, or null.
[[nodiscard]] static const kb::scene::AnimationCurveTrack* SelectedClipCurve(
    const AnimationClipTimelineState& timeline, const kb::scene::AnimationClip* clip) noexcept {
    const AnimationClipTimelineTrack* track = timeline.SelectedTrackData();
    if (clip == nullptr || track == nullptr || track->kind != AnimationClipTimelineTrackKind::Curve) return nullptr;
    const auto found = std::find_if(clip->curves.begin(), clip->curves.end(),
        [track](const kb::scene::AnimationCurveTrack& curve) { return curve.name == track->curveName; });
    return found == clip->curves.end() ? nullptr : &*found;
}

bool EditorSceneContext::BindAnimationClipCurveEditor() {
    const kb::scene::AnimationClip* clip = animationClipEditorDocument_.WorkingCopy();
    const kb::scene::AnimationCurveTrack* curve = SelectedClipCurve(animationClipEditorTimeline_, clip);
    if (curve == nullptr) return false;
    if (curveEditorParticleProperty_.has_value() || curveEditorClipCurve_ != curve->name) {
        curveEditor_ = CurveEditorState{};
        curveEditorInput_ = CurveEditorInteraction{};
    }
    curveEditorParticleProperty_.reset();
    curveEditorClipCurve_ = curve->name;
    curveEditor_.SetTimeLimits(0.0F, clip->durationSeconds);
    if (!curveEditorInput_.Dragging()) curveEditor_.SetCurve(kb::math::Curve{ curve->keyframes });
    return true;
}

bool EditorSceneContext::BindParticleCurveEditor(std::size_t propertyIndex) {
    const auto inspector = ParticleEditorInspector();
    if (propertyIndex >= inspector.properties.size() || !inspector.properties[propertyIndex].editable ||
        inspector.properties[propertyIndex].widget != kb::particle_editor::ParticleEditorPropertyWidget::Curve) {
        return false;
    }
    if (curveEditorParticleProperty_ != propertyIndex) {
        curveEditor_ = CurveEditorState{};
        curveEditorInput_ = CurveEditorInteraction{};
    }
    curveEditorClipCurve_.clear();
    curveEditorParticleProperty_ = propertyIndex;
    curveEditor_.SetTimeLimits(0.0F, 1.0F);
    if (!curveEditorInput_.Dragging()) curveEditor_.SetCurve(inspector.properties[propertyIndex].curve);
    return true;
}

std::optional<CurveEditorState> EditorSceneContext::AnimationClipCurveEditorView(const CurveEditorViewport& viewport) const {
    const kb::scene::AnimationClip* clip = animationClipEditorDocument_.WorkingCopy();
    const kb::scene::AnimationCurveTrack* curve = SelectedClipCurve(animationClipEditorTimeline_, clip);
    if (curve == nullptr) return std::nullopt;
    const bool bound = !curveEditorParticleProperty_.has_value() && curveEditorClipCurve_ == curve->name;
    CurveEditorState view = bound ? curveEditor_ : CurveEditorState{};
    view.SetTimeLimits(0.0F, clip->durationSeconds);
    view.SetViewport(viewport);
    if (!bound || !curveEditorInput_.Dragging()) view.SetCurve(kb::math::Curve{ curve->keyframes });
    return view;
}

CurveEditorState EditorSceneContext::ParticleCurveEditorView(
    std::size_t propertyIndex, const kb::math::Curve& curve, const CurveEditorViewport& viewport) const {
    const bool bound = curveEditorParticleProperty_ == propertyIndex;
    CurveEditorState view = bound ? curveEditor_ : CurveEditorState{};
    view.SetViewport(viewport);
    if (!bound || !curveEditorInput_.Dragging()) view.SetCurve(curve);
    return view;
}

std::optional<std::size_t> EditorSceneContext::ParticleCurveEditorProperty() const noexcept {
    return curveEditorParticleProperty_;
}

bool EditorSceneContext::ApplyCurveEditorResult(const CurveEditorResult& result) {
    bool applied = false;
    if (curveEditorParticleProperty_.has_value()) {
        // Moves after the first one of a drag replace the latest undo step.
        if (result.editBegin) curveEditorCoalesce_ = false;
        if (result.edited) {
            applied = EditParticleEditorProperty(*curveEditorParticleProperty_,
                kb::particle_editor::ParticleEmitterInspectorModel::FormatCurve(curveEditor_.Curve()), curveEditorCoalesce_);
            curveEditorCoalesce_ = !result.editEnd;
        }
        if (result.editEnd) curveEditorCoalesce_ = false;
        return applied;
    }
    if (curveEditorClipCurve_.empty()) return false;
    if (result.editBegin) static_cast<void>(BeginAnimationClipEditorEditGroup());
    if (result.edited) {
        applied = animationClipEditorDocument_.SetCurveKeyframes(curveEditorClipCurve_, curveEditor_.Curve().keyframes) &&
            PublishAnimationClipEditorWorkingCopy();
    }
    if (result.editEnd) EndAnimationClipEditorEditGroup();
    return applied;
}

} // namespace kb::editor
