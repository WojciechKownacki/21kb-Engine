#pragma once

#include "curves/CurveEditorState.hpp"

#include <cmath>
#include <cstdint>

namespace kb::editor {

// What a host does after an input event: redraw, and for edits open an undo group
// (editBegin), store CurveEditorState::Curve() into its document (edited), close the group (editEnd).
struct CurveEditorResult {
    bool redraw = false;
    bool editBegin = false;
    bool edited = false;
    bool editEnd = false;

    [[nodiscard]] static CurveEditorResult SingleEdit() noexcept { return { true, true, true, true }; }
};

enum class CurveEditorKey : std::uint8_t {
    FrameAll,
    Delete,
};

// Pointer, wheel and key handling of the curve view, independent of the window system.
class CurveEditorInteraction {
public:
    [[nodiscard]] bool Dragging() const noexcept { return drag_ != CurveEditorHandle::None; }
    [[nodiscard]] bool Panning() const noexcept { return panning_; }

    [[nodiscard]] CurveEditorResult PointerDown(CurveEditorState& state, float x, float y, bool shift, bool ctrl) {
        static_cast<void>(shift);
        if (ctrl) return DoubleClick(state, x, y);
        const CurveEditorHit hit = state.HitTest(x, y);
        CurveEditorResult result{};
        if (hit.handle == CurveEditorHandle::None) {
            result.redraw = state.Select(std::nullopt);
            return result;
        }
        static_cast<void>(state.Select(hit.key));
        result.redraw = true;
        drag_ = hit.handle;
        dragKey_ = hit.key;
        startX_ = x;
        startY_ = y;
        const kb::math::CurveKeyframe& key = state.Curve().keyframes[hit.key];
        startTime_ = key.time;
        startValue_ = key.value;
        editOpen_ = false;
        return result;
    }

    [[nodiscard]] CurveEditorResult PointerMove(CurveEditorState& state, float x, float y, bool shift) {
        CurveEditorResult result{};
        if (drag_ == CurveEditorHandle::None) return result;
        bool changed = false;
        if (drag_ == CurveEditorHandle::Key) {
            float time = startTime_ + (state.TimeAt(x) - state.TimeAt(startX_));
            float value = startValue_ + (state.ValueAt(y) - state.ValueAt(startY_));
            // Shift keeps the axis the pointer moved along most.
            if (shift) {
                if (std::abs(x - startX_) >= std::abs(y - startY_)) value = startValue_;
                else time = startTime_;
            }
            changed = state.MoveKey(dragKey_, time, value);
        } else {
            changed = state.DragTangent(dragKey_, drag_ == CurveEditorHandle::OutTangent, x, y);
        }
        if (!changed) return result;
        result.redraw = true;
        result.edited = true;
        result.editBegin = !editOpen_;
        editOpen_ = true;
        return result;
    }

    [[nodiscard]] CurveEditorResult PointerUp() noexcept {
        CurveEditorResult result{};
        result.editEnd = editOpen_;
        result.redraw = drag_ != CurveEditorHandle::None;
        drag_ = CurveEditorHandle::None;
        editOpen_ = false;
        return result;
    }

    // Adds a key on the curve under the pointer.
    [[nodiscard]] CurveEditorResult DoubleClick(CurveEditorState& state, float x, float y) {
        static_cast<void>(y);
        return state.AddKey(state.TimeAt(x)).has_value() ? CurveEditorResult::SingleEdit() : CurveEditorResult{};
    }

    // Wheel zooms time around the pointer, Ctrl zooms the value axis.
    [[nodiscard]] static CurveEditorResult Wheel(CurveEditorState& state, float x, float y, int delta, bool ctrl) {
        const float factor = std::pow(1.2F, static_cast<float>(delta) / 120.0F);
        return { state.Zoom(factor, x, y, ctrl) };
    }

    void MiddleDown(float x, float y) noexcept {
        panning_ = true;
        panX_ = x;
        panY_ = y;
    }
    [[nodiscard]] CurveEditorResult MiddleMove(CurveEditorState& state, float x, float y) noexcept {
        if (!panning_) return {};
        const bool moved = state.Pan(x - panX_, y - panY_);
        panX_ = x;
        panY_ = y;
        return { moved };
    }
    void MiddleUp() noexcept { panning_ = false; }

    [[nodiscard]] static CurveEditorResult Key(CurveEditorState& state, CurveEditorKey key) {
        if (key == CurveEditorKey::FrameAll) {
            state.FrameAll();
            return { true };
        }
        const std::optional<std::size_t> selected = state.Selected();
        return selected.has_value() && state.DeleteKey(*selected) ? CurveEditorResult::SingleEdit() : CurveEditorResult{};
    }

    [[nodiscard]] static CurveEditorResult SetTangentMode(CurveEditorState& state, kb::math::CurveTangentMode mode) {
        const std::optional<std::size_t> selected = state.Selected();
        return selected.has_value() && state.SetTangentMode(*selected, mode) ? CurveEditorResult::SingleEdit() : CurveEditorResult{};
    }

private:
    CurveEditorHandle drag_ = CurveEditorHandle::None;
    std::size_t dragKey_ = 0U;
    float startX_ = 0.0F;
    float startY_ = 0.0F;
    float startTime_ = 0.0F;
    float startValue_ = 0.0F;
    bool editOpen_ = false;
    bool panning_ = false;
    float panX_ = 0.0F;
    float panY_ = 0.0F;
};

} // namespace kb::editor
