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
// Device-level input-key disambiguation remains a normalized-device concern. Haptics is
// explicitly routed from this id to a physical gamepad slot by InputHaptics, allowing
// split-screen users to drive independent actuators without changing InputKey.
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
