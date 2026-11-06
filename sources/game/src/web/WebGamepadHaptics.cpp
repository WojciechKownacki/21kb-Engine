#include "WebGamepadHaptics.hpp"
#include <emscripten/emscripten.h>
#include <cmath>

namespace kb::game {
input::InputHapticsCapability WebGamepadHaptics::Capability(std::uint32_t slot) {
    const int flags = slot < 4U ? EM_ASM_INT({ return Module['kbGamepadHaptics'].capability($0); }, slot) : 0;
    return { .supported = (flags & 2) != 0, .connected = (flags & 1) != 0,
        .dualMotor = (flags & 2) != 0, .maxGamepads = 4U,
        .disabledReason = (flags & 2) ? "" : !(flags & 1) ? "browser controller is not connected" :
            "browser dual-rumble is unavailable, inactive or rejected" };
}
bool WebGamepadHaptics::SetVibration(std::uint32_t slot, float low, float high) {
    if (slot >= 4U || !std::isfinite(low) || !std::isfinite(high)) return false;
    return EM_ASM_INT({ return Module['kbGamepadHaptics'].set($0, $1, $2) ? 1 : 0; }, slot, low, high) != 0;
}
void WebGamepadHaptics::StopAll() noexcept { EM_ASM({ Module['kbGamepadHaptics'].stopAll(); }); }
void WebGamepadHaptics::Update() noexcept { EM_ASM({ Module['kbGamepadHaptics'].update(); }); }
} // namespace kb::game
