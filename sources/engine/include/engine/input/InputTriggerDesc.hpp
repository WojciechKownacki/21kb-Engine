#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::input {

// Triggers decide when a mapping's (already modified) value counts as "firing",
// mirroring Unreal's UInputTrigger hierarchy. Parameters are stored inline.
//
// A mapping with no explicit triggers behaves as an implicit `Down` trigger.
enum class InputTriggerType : std::uint8_t {
    Down,           // Fires every frame the value is actuated.
    Pressed,        // Fires on the frame actuation begins.
    Released,       // Fires on the frame actuation ends.
    Hold,           // Fires once the value stays actuated for params[1] seconds.
    HoldAndRelease, // Fires on release if held at least params[1] seconds.
    Tap,            // Fires on release if released within params[1] seconds.
    Pulse,          // Fires repeatedly every params[1] seconds while actuated.
    Chorded,        // Fires only while the action referenced by chordActionId is triggered.
};

struct InputTriggerDesc {
    InputTriggerType type = InputTriggerType::Down;
    // params[0]: actuation threshold (default 0.5 when zero is meaningless).
    // Hold/HoldAndRelease -> params[1]: hold time seconds; params[2]: one-shot (0/1).
    // Tap                 -> params[1]: max tap time seconds.
    // Pulse               -> params[1]: interval seconds; params[2]: trigger on start (0/1);
    //                        params[3]: trigger limit (0 = unlimited).
    std::array<float, 4> params{};
    // Only used by Chorded: stable asset id of the action that must be triggered.
    std::uint64_t chordActionId = 0U;
};

[[nodiscard]] constexpr std::string_view ToString(InputTriggerType type) noexcept {
    switch (type) {
        case InputTriggerType::Down:
            return "Down";
        case InputTriggerType::Pressed:
            return "Pressed";
        case InputTriggerType::Released:
            return "Released";
        case InputTriggerType::Hold:
            return "Hold";
        case InputTriggerType::HoldAndRelease:
            return "HoldAndRelease";
        case InputTriggerType::Tap:
            return "Tap";
        case InputTriggerType::Pulse:
            return "Pulse";
        case InputTriggerType::Chorded:
            return "Chorded";
    }
    return "Down";
}

} // namespace kb::input
