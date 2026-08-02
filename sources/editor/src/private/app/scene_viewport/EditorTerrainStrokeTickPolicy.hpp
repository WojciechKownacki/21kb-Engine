#pragma once

#include <algorithm>
#include <cmath>

namespace kb::editor {

class EditorTerrainStrokeTickPolicy final {
public:
    EditorTerrainStrokeTickPolicy() = delete;

    static constexpr float TickIntervalSeconds = 1.0F / 30.0F;
    static constexpr float StampPressure = 0.2F;

    [[nodiscard]] static bool Advance(float deltaSeconds, float& elapsedSeconds) noexcept {
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
            return false;
        }

        // Never replay an unbounded backlog after a stalled frame. One edit per
        // editor tick keeps the sculpt hot path responsive under load.
        elapsedSeconds = std::min(
            elapsedSeconds + deltaSeconds,
            TickIntervalSeconds * 2.0F);
        if (elapsedSeconds < TickIntervalSeconds) {
            return false;
        }
        elapsedSeconds = std::fmod(elapsedSeconds, TickIntervalSeconds);
        return true;
    }
};

} // namespace kb::editor
