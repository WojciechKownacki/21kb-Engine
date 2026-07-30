#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class VisibilityMode : std::uint8_t {
    Inherit = 0U,
    Visible = 1U,
    Hidden = 2U,
};

struct VisibilityComponent {
    static constexpr std::string_view StableId = "kb21.scene.visibility-gate";
    static constexpr std::uint32_t SchemaVersion = 2U;
    static constexpr std::uint32_t AllMask = 0xFFFFFFFFU;

    // `Visible` preserves the authored behaviour of scenes written before
    // schema v2. Children opt into their parent's gate explicitly.
    VisibilityMode mode = VisibilityMode::Visible;
    std::uint32_t mask = AllMask;

    // Source-compatibility shim for authored C++ scene descriptors written
    // before schema v2. New code must set `mode`; a false legacy value still
    // resolves as Hidden so old content remains fail-closed.
    bool visible = true;
};

[[nodiscard]] constexpr bool IsVisibilityModeValid(VisibilityMode mode) noexcept {
    return mode == VisibilityMode::Inherit || mode == VisibilityMode::Visible ||
        mode == VisibilityMode::Hidden;
}

} // namespace kb::scene
