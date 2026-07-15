#pragma once

#include <cstdint>

namespace kb::input {

// Named priority bands for InputSubsystem::AddMappingContext's priority
// parameter (see InputMappingContextStack - higher priority wins ties and
// consumes keys away from lower-priority contexts, LIB-113/115/116). These are
// RESERVED VALUES, not enforced ranges: nothing stops a caller from pushing an
// arbitrary int, but every engine-provided context - gameplay entities via
// InputComponent (LIB-115) today, and the UI tree/console/debug overlay
// systems that will exist once LIB-173/180/224-226 are built - should use
// these constants so relative ordering is consistent and discoverable instead
// of each future system inventing its own ad-hoc number.
//
// Order (highest wins first): DebugOverlay > Console > UI > Gameplay. A debug
// overlay must be able to intercept input even over an open console; a
// console must intercept over game UI; game UI must intercept over raw
// gameplay actions (e.g. WASD movement shouldn't fire while a menu is open).
struct InputContextPriority {
    InputContextPriority() = delete;

    // Matches InputComponent::priority's existing default (0) - every
    // gameplay-authored mapping context already lives at this band today.
    static constexpr std::int32_t Gameplay = 0;
    static constexpr std::int32_t UI = 1000;
    static constexpr std::int32_t Console = 2000;
    static constexpr std::int32_t DebugOverlay = 3000;
};

} // namespace kb::input
