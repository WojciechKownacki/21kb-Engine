#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace kb::input {

// Identifies one local player's independent input state (mapping-context stack +
// action states) within a single Scene, e.g. split-screen or shared-keyboard
// local co-op. All local users read the SAME physical device state (there is one
// keyboard/mouse on the machine); what differs per user is which mapping
// contexts are active and which action values they resolve to.
//
// Device-level multi-gamepad disambiguation (e.g. distinguishing "gamepad index
// 0" from "gamepad index 1") is a separate concern belonging to the normalized
// device model, not this id - InputKey has no per-device-instance axis yet.
struct LocalUserId {
    std::uint32_t value = 0U;

    [[nodiscard]] friend constexpr bool operator==(LocalUserId lhs, LocalUserId rhs) noexcept = default;
};

// The default/primary local user - exactly today's single-player Scene::Input().
inline constexpr LocalUserId kPrimaryLocalUser{0U};

} // namespace kb::input

template <>
struct std::hash<kb::input::LocalUserId> {
    [[nodiscard]] std::size_t operator()(kb::input::LocalUserId id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};
