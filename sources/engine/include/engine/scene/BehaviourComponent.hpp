#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::scene {

enum class BehaviourBackend : std::uint8_t {
    Native,
    Lua,
    VisualGraph,
};

enum class BehaviourTickGroup : std::uint8_t {
    Input,
    Gameplay,
    Physics,
    Animation,
    Camera,
    Presentation,
};

struct BehaviourComponent {
    std::uint64_t behaviourAssetId = 0;
    BehaviourBackend backend = BehaviourBackend::VisualGraph;
    bool enabled = true;
    BehaviourTickGroup tickGroup = BehaviourTickGroup::Gameplay;
    std::int32_t executionOrder = 0;
};

// An entity runs up to this many behaviours (scripts), each with a `self` of its own. The first one attached is the
// entity's behaviour component; the others follow it in attach order.
inline constexpr std::size_t kMaxBehavioursPerEntity = 8U;

// Whether two components are the same script with the same settings.
[[nodiscard]] constexpr bool SameBehaviourComponent(const BehaviourComponent& lhs, const BehaviourComponent& rhs) noexcept {
    return lhs.behaviourAssetId == rhs.behaviourAssetId && lhs.backend == rhs.backend && lhs.enabled == rhs.enabled &&
           lhs.tickGroup == rhs.tickGroup && lhs.executionOrder == rhs.executionOrder;
}

// Two lists of behaviours are the same when they hold the same scripts with the same settings in the same order.
[[nodiscard]] inline bool SameBehaviourList(std::span<const BehaviourComponent> lhs, std::span<const BehaviourComponent> rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (!SameBehaviourComponent(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

} // namespace kb::scene
