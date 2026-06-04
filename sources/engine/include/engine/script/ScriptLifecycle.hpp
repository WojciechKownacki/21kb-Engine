#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>

namespace kb::script {

enum class ScriptLifecycleEvent : std::uint8_t {
    Created,
    Activated,
    Ready,
    FixedTick,
    Tick,
    LateTick,
    BeforeRender,
    AfterRender,
    Deactivated,
    Destroyed,
};

[[nodiscard]] const char* ToString(ScriptLifecycleEvent event) noexcept;
[[nodiscard]] kb::visual::VisualGraphLifecycleEvent ToVisualGraphLifecycleEvent(ScriptLifecycleEvent event) noexcept;

} // namespace kb::script
