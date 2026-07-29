#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

using AiNodeId = std::uint64_t;

enum class AiNodeKind : std::uint8_t {
    Sequence,
    Selector,
    Condition,
    Action,
    UtilitySelector,
};

// Child nodes occupy [firstChild, firstChild + childCount) in `nodes`. This
// compact layout is authored once and makes a tick allocation-free.
struct AiBehaviourNode {
    AiNodeId id = 0U;
    AiNodeKind kind = AiNodeKind::Action;
    std::uint32_t firstChild = 0U;
    std::uint32_t childCount = 0U;
};

struct AiStateTransition {
    std::uint32_t targetState = 0U;
    // A callback-owned condition id. Transitions are evaluated in authored
    // order, so ties remain deterministic.
    AiNodeId condition = 0U;
};

struct AiState {
    std::string name;
    std::uint32_t rootNode = 0U;
    std::vector<AiStateTransition> transitions;
};

// One authored AI asset can either run `rootNode` directly, or select a root
// through its finite state machine. It contains no script function names and
// therefore cannot become a second script scheduler.
struct AiBehaviourAsset {
    std::vector<AiBehaviourNode> nodes;
    std::uint32_t rootNode = 0U;
    std::vector<AiState> states;
    std::uint32_t initialState = 0U;
};

struct AiAssetValidationResult {
    bool valid = false;
    std::string error;
};

[[nodiscard]] AiAssetValidationResult ValidateAiBehaviourAsset(const AiBehaviourAsset& asset);

} // namespace kb::scene
