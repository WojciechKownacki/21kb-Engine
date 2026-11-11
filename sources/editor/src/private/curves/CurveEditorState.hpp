#pragma once

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace kb::editor {

enum class CurveEditorHandle : std::uint8_t {
    None,
    Key,
    InTangent,
    OutTangent,
};

struct CurveEditorHit {
    CurveEditorHandle handle = CurveEditorHandle::None;
    std::size_t key = 0U;
};

// Pixel rectangle the view is drawn into.
struct CurveEditorViewport {
    float left = 0.0F;
    float top = 0.0F;
    float right = 1.0F;
    float bottom = 1.0F;
};

// One curve being edited: the working keys, the visible time/value window and the selected key.
// Every edit keeps keys sorted by time and re-derives Auto, Flat and Linear tangents.
class CurveEditorState {
public:
    static constexpr float kHandleLengthPixels = 40.0F;
    static constexpr float kPickRadiusPixels = 6.0F;

    void SetCurve(const kb::math::Curve& curve) {
        const bool first = curve_.keyframes.empty();
        curve_ = curve;
        if (selected_.has_value() && *selected_ >= curve_.keyframes.size()) selected_.reset();
        if (first) FrameAll();
    }
    [[nodiscard]] const kb::math::Curve& Curve() const noexcept { return curve_; }

    // Keys stay inside [minTime, maxTime]; particle curves use 0..1, clip curves 0..duration.
    void SetTimeLimits(float minTime, float maxTime) noexcept {
        minTime_ = minTime;
        maxTime_ = std::max(minTime, maxTime);
    }
    [[nodiscard]] float MinTime() const noexcept { return minTime_; }
    [[nodiscard]] float MaxTime() const noexcept { return maxTime_; }

    void SetViewport(const CurveEditorViewport& viewport) noexcept { viewport_ = viewport; }
    [[nodiscard]] const CurveEditorViewport& Viewport() const noexcept { return viewport_; }
    [[nodiscard]] float TimeStart() const noexcept { return timeStart_; }
    [[nodiscard]] float TimeEnd() const noexcept { return timeEnd_; }
    [[nodiscard]] float ValueStart() const noexcept { return valueStart_; }
    [[nodiscard]] float ValueEnd() const noexcept { return valueEnd_; }

    [[nodiscard]] float XAt(float time) const noexcept {
        return viewport_.left + (time - timeStart_) / (timeEnd_ - timeStart_) * Width();
    }
    [[nodiscard]] float YAt(float value) const noexcept {
        return viewport_.bottom - (value - valueStart_) / (valueEnd_ - valueStart_) * Height();
    }
    [[nodiscard]] float TimeAt(float x) const noexcept {
        return timeStart_ + (x - viewport_.left) / Width() * (timeEnd_ - timeStart_);
    }
    [[nodiscard]] float ValueAt(float y) const noexcept {
        return valueStart_ + (viewport_.bottom - y) / Height() * (valueEnd_ - valueStart_);
    }

    // Fits the time limits and every key value, with a margin.
    void FrameAll() noexcept {
        timeStart_ = minTime_;
        timeEnd_ = maxTime_ > minTime_ ? maxTime_ : minTime_ + 1.0F;
        float low = 0.0F;
        float high = 1.0F;
        if (!curve_.keyframes.empty()) {
            low = high = curve_.keyframes.front().value;
            for (const kb::math::CurveKeyframe& key : curve_.keyframes) {
                low = std::min(low, key.value);
                high = std::max(high, key.value);
            }
        }
        if (high - low < 1.0e-3F) {
            low -= 0.5F;
            high += 0.5F;
        }
        const float margin = (high - low) * 0.1F;
        valueStart_ = low - margin;
        valueEnd_ = high + margin;
        const float timeMargin = (timeEnd_ - timeStart_) * 0.02F;
        timeStart_ -= timeMargin;
        timeEnd_ += timeMargin;
    }

    // Zooms one axis around a pixel anchor; factor > 1 zooms in.
    [[nodiscard]] bool Zoom(float factor, float anchorX, float anchorY, bool valueAxis) noexcept {
        if (!std::isfinite(factor) || factor <= 0.0F) return false;
        if (valueAxis) {
            const float anchor = ValueAt(anchorY);
            const float span = (valueEnd_ - valueStart_) / factor;
            if (span < 1.0e-4F || span > 1.0e6F) return false;
            const float share = (anchor - valueStart_) / (valueEnd_ - valueStart_);
            valueStart_ = anchor - share * span;
            valueEnd_ = valueStart_ + span;
        } else {
            const float anchor = TimeAt(anchorX);
            const float span = (timeEnd_ - timeStart_) / factor;
            if (span < 1.0e-4F || span > 1.0e6F) return false;
            const float share = (anchor - timeStart_) / (timeEnd_ - timeStart_);
            timeStart_ = anchor - share * span;
            timeEnd_ = timeStart_ + span;
        }
        return true;
    }

    // Moves the visible window by a pixel delta (content follows the pointer).
    [[nodiscard]] bool Pan(float deltaX, float deltaY) noexcept {
        if (deltaX == 0.0F && deltaY == 0.0F) return false;
        const float dt = deltaX / Width() * (timeEnd_ - timeStart_);
        const float dv = deltaY / Height() * (valueEnd_ - valueStart_);
        timeStart_ -= dt;
        timeEnd_ -= dt;
        valueStart_ += dv;
        valueEnd_ += dv;
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> Selected() const noexcept { return selected_; }
    [[nodiscard]] bool Select(std::optional<std::size_t> index) noexcept {
        if (index.has_value() && *index >= curve_.keyframes.size()) return false;
        if (selected_ == index) return false;
        selected_ = index;
        return true;
    }

    // Pixel position of a tangent handle of a key.
    [[nodiscard]] std::pair<float, float> HandlePosition(std::size_t index, bool outSide) const noexcept {
        const kb::math::CurveKeyframe& key = curve_.keyframes[index];
        const float slope = outSide ? key.outTangent : key.inTangent;
        // Direction in pixels of one time unit along the tangent.
        const float dx = Width() / (timeEnd_ - timeStart_);
        const float dy = -slope * Height() / (valueEnd_ - valueStart_);
        const float length = std::sqrt(dx * dx + dy * dy);
        const float sign = outSide ? 1.0F : -1.0F;
        const float scale = length > 0.0F ? kHandleLengthPixels / length : 0.0F;
        return { XAt(key.time) + sign * dx * scale, YAt(key.value) + sign * dy * scale };
    }

    [[nodiscard]] static bool HasHandles(const kb::math::CurveKeyframe& key) noexcept {
        return key.tangentMode != kb::math::CurveTangentMode::Easing && key.tangentMode != kb::math::CurveTangentMode::Constant;
    }

    // Tangent handles of the selected key win over keys.
    [[nodiscard]] CurveEditorHit HitTest(float x, float y) const noexcept {
        const auto pickable = [x, y](float px, float py) {
            return std::abs(px - x) <= kPickRadiusPixels && std::abs(py - y) <= kPickRadiusPixels;
        };
        if (selected_.has_value() && HasHandles(curve_.keyframes[*selected_])) {
            const auto out = HandlePosition(*selected_, true);
            if (pickable(out.first, out.second)) return { CurveEditorHandle::OutTangent, *selected_ };
            const auto in = HandlePosition(*selected_, false);
            if (pickable(in.first, in.second)) return { CurveEditorHandle::InTangent, *selected_ };
        }
        for (std::size_t index = 0U; index < curve_.keyframes.size(); ++index) {
            if (pickable(XAt(curve_.keyframes[index].time), YAt(curve_.keyframes[index].value))) return { CurveEditorHandle::Key, index };
        }
        return {};
    }

    // Adds a smooth key on the curve at a time and selects it.
    [[nodiscard]] std::optional<std::size_t> AddKey(float time) {
        if (!std::isfinite(time)) return std::nullopt;
        time = std::clamp(time, minTime_, maxTime_);
        for (const kb::math::CurveKeyframe& key : curve_.keyframes) {
            if (std::abs(key.time - time) < MinimumGap()) return std::nullopt;
        }
        kb::math::CurveKeyframe key{};
        key.time = time;
        key.value = kb::math::Evaluate(curve_, time);
        key.tangentMode = kb::math::CurveTangentMode::Auto;
        const auto position = std::upper_bound(curve_.keyframes.begin(), curve_.keyframes.end(), time,
            [](float value, const kb::math::CurveKeyframe& existing) { return value < existing.time; });
        const std::size_t index = static_cast<std::size_t>(position - curve_.keyframes.begin());
        curve_.keyframes.insert(position, key);
        Recompute();
        selected_ = index;
        return index;
    }

    // Moves a key; it stays between its neighbours so the selection never jumps.
    [[nodiscard]] bool MoveKey(std::size_t index, float time, float value) {
        if (index >= curve_.keyframes.size() || !std::isfinite(time) || !std::isfinite(value)) return false;
        const float gap = MinimumGap();
        const float low = index > 0U ? curve_.keyframes[index - 1U].time + gap : minTime_;
        const float high = index + 1U < curve_.keyframes.size() ? curve_.keyframes[index + 1U].time - gap : maxTime_;
        kb::math::CurveKeyframe& key = curve_.keyframes[index];
        const float clamped = low <= high ? std::clamp(time, low, high) : key.time;
        if (key.time == clamped && key.value == value) return false;
        key.time = clamped;
        key.value = value;
        Recompute();
        return true;
    }

    // Removes a key; a curve always keeps one key.
    [[nodiscard]] bool DeleteKey(std::size_t index) {
        if (index >= curve_.keyframes.size() || curve_.keyframes.size() <= 1U) return false;
        curve_.keyframes.erase(curve_.keyframes.begin() + static_cast<std::ptrdiff_t>(index));
        Recompute();
        selected_.reset();
        return true;
    }

    [[nodiscard]] bool SetTangentMode(std::size_t index, kb::math::CurveTangentMode mode) {
        if (index >= curve_.keyframes.size()) return false;
        kb::math::CurveKeyframe& key = curve_.keyframes[index];
        if (key.tangentMode == mode) return false;
        // Free tangents start from the smooth ones when the key had no tangent data.
        if (key.tangentMode == kb::math::CurveTangentMode::Easing && mode == kb::math::CurveTangentMode::Free) {
            key.tangentMode = kb::math::CurveTangentMode::Auto;
            Recompute();
        }
        key.tangentMode = mode;
        Recompute();
        return true;
    }

    // Sets one tangent from a handle position; the key becomes Free. A key that was not
    // Free keeps both sides aligned, a Free key moves only the dragged side.
    [[nodiscard]] bool DragTangent(std::size_t index, bool outSide, float x, float y) {
        if (index >= curve_.keyframes.size()) return false;
        kb::math::CurveKeyframe& key = curve_.keyframes[index];
        const float dt = (TimeAt(x) - key.time) * (outSide ? 1.0F : -1.0F);
        if (dt <= 1.0e-6F) return false;
        const float slope = (ValueAt(y) - key.value) * (outSide ? 1.0F : -1.0F) / dt;
        if (!std::isfinite(slope)) return false;
        const bool aligned = key.tangentMode != kb::math::CurveTangentMode::Free;
        key.tangentMode = kb::math::CurveTangentMode::Free;
        if (outSide || aligned) key.outTangent = slope;
        if (!outSide || aligned) key.inTangent = slope;
        Recompute();
        return true;
    }

private:
    [[nodiscard]] float Width() const noexcept { return std::max(1.0F, viewport_.right - viewport_.left); }
    [[nodiscard]] float Height() const noexcept { return std::max(1.0F, viewport_.bottom - viewport_.top); }
    [[nodiscard]] float MinimumGap() const noexcept { return std::max(1.0e-4F, (maxTime_ - minTime_) * 1.0e-3F); }
    void Recompute() noexcept { kb::math::RecomputeTangents(std::span<kb::math::CurveKeyframe>{ curve_.keyframes }); }

    kb::math::Curve curve_{};
    CurveEditorViewport viewport_{};
    float minTime_ = 0.0F;
    float maxTime_ = 1.0F;
    float timeStart_ = 0.0F;
    float timeEnd_ = 1.0F;
    float valueStart_ = 0.0F;
    float valueEnd_ = 1.0F;
    std::optional<std::size_t> selected_;
};

} // namespace kb::editor
