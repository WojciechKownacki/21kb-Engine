#include "engine/scene/AiBehaviourRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::uint32_t kMaxAiTreeDepth = 128U;

[[nodiscard]] bool IsComposite(AiNodeKind kind) noexcept {
    return kind == AiNodeKind::Sequence || kind == AiNodeKind::Selector || kind == AiNodeKind::UtilitySelector;
}

[[nodiscard]] bool ValidateSubtree(
    const AiBehaviourAsset& asset,
    std::uint32_t index,
    std::vector<std::uint8_t>& marks,
    std::uint32_t depth,
    std::string& error) {
    if (depth > kMaxAiTreeDepth) {
        error = "AI behaviour tree exceeds the supported nesting depth.";
        return false;
    }
    if (marks[index] == 1U) {
        error = "AI behaviour tree contains a cycle.";
        return false;
    }
    if (marks[index] == 2U) return true;
    marks[index] = 1U;
    const AiBehaviourNode& node = asset.nodes[index];
    for (std::uint32_t child = node.firstChild; child < node.firstChild + node.childCount; ++child) {
        if (!ValidateSubtree(asset, child, marks, depth + 1U, error)) return false;
    }
    marks[index] = 2U;
    return true;
}

void ResetSubtree(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state, std::uint32_t index) noexcept {
    state.childCursors[index] = 0U;
    const AiBehaviourNode& node = asset.nodes[index];
    for (std::uint32_t child = node.firstChild; child < node.firstChild + node.childCount; ++child) {
        ResetSubtree(asset, state, child);
    }
}

[[nodiscard]] AiExecutionStatus EvaluateNode(
    const AiBehaviourAsset& asset,
    AiBehaviourRuntimeState& state,
    const AiBehaviourCallbacks& callbacks,
    std::uint32_t index) noexcept {
    const AiBehaviourNode& node = asset.nodes[index];
    switch (node.kind) {
    case AiNodeKind::Condition:
        return callbacks.condition != nullptr && callbacks.condition(callbacks.context, node.id) ? AiExecutionStatus::Success :
            (callbacks.condition != nullptr ? AiExecutionStatus::Failure : AiExecutionStatus::Invalid);
    case AiNodeKind::Action:
        if (callbacks.action == nullptr) return AiExecutionStatus::Invalid;
        return callbacks.action(callbacks.context, node.id);
    case AiNodeKind::Sequence:
    case AiNodeKind::Selector: {
        std::uint32_t& cursor = state.childCursors[index];
        while (cursor < node.childCount) {
            const std::uint32_t child = node.firstChild + cursor;
            const AiExecutionStatus childStatus = EvaluateNode(asset, state, callbacks, child);
            if (childStatus == AiExecutionStatus::Running || childStatus == AiExecutionStatus::Invalid) return childStatus;
            const bool stop = (node.kind == AiNodeKind::Sequence && childStatus == AiExecutionStatus::Failure) ||
                (node.kind == AiNodeKind::Selector && childStatus == AiExecutionStatus::Success);
            if (stop) {
                ResetSubtree(asset, state, index);
                return childStatus;
            }
            ++cursor;
        }
        const AiExecutionStatus completed = node.kind == AiNodeKind::Sequence ? AiExecutionStatus::Success : AiExecutionStatus::Failure;
        ResetSubtree(asset, state, index);
        return completed;
    }
    case AiNodeKind::UtilitySelector: {
        if (callbacks.utility == nullptr) return AiExecutionStatus::Invalid;
        std::uint32_t selected = node.firstChild;
        float selectedScore = -std::numeric_limits<float>::infinity();
        for (std::uint32_t child = node.firstChild; child < node.firstChild + node.childCount; ++child) {
            const float score = callbacks.utility(callbacks.context, asset.nodes[child].id);
            if (std::isfinite(score) && score > selectedScore) {
                selected = child;
                selectedScore = score;
            }
        }
        if (!std::isfinite(selectedScore)) return AiExecutionStatus::Failure;
        // Store the relative child plus one (zero means no running choice).
        // If scoring changes while a child is Running, cancel its retained
        // composite cursor before evaluating the newly selected branch.
        std::uint32_t& runningSelection = state.childCursors[index];
        const std::uint32_t selectedMarker = selected - node.firstChild + 1U;
        if (runningSelection != 0U && runningSelection != selectedMarker) {
            ResetSubtree(asset, state, node.firstChild + runningSelection - 1U);
        }
        runningSelection = selectedMarker;
        const AiExecutionStatus status = EvaluateNode(asset, state, callbacks, selected);
        if (status != AiExecutionStatus::Running) ResetSubtree(asset, state, index);
        return status;
    }
    }
    return AiExecutionStatus::Invalid;
}

} // namespace

AiAssetValidationResult ValidateAiBehaviourAsset(const AiBehaviourAsset& asset) {
    if (asset.nodes.empty() || asset.rootNode >= asset.nodes.size()) {
        return { .valid = false, .error = "AI behaviour asset has no valid root node." };
    }
    std::vector<AiNodeId> ids;
    ids.reserve(asset.nodes.size());
    for (const AiBehaviourNode& node : asset.nodes) {
        if (node.id == 0U || (IsComposite(node.kind) ? node.childCount == 0U : node.childCount != 0U) ||
            node.firstChild > asset.nodes.size() || node.childCount > asset.nodes.size() - node.firstChild) {
            return { .valid = false, .error = "AI behaviour node has an invalid id or child range." };
        }
        ids.push_back(node.id);
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
        return { .valid = false, .error = "AI behaviour node ids must be unique." };
    }
    std::vector<std::uint8_t> marks(asset.nodes.size(), 0U);
    std::string error;
    if (!ValidateSubtree(asset, asset.rootNode, marks, 0U, error)) return { .valid = false, .error = std::move(error) };
    if (!asset.states.empty()) {
        if (asset.initialState >= asset.states.size()) return { .valid = false, .error = "AI state machine initial state is invalid." };
        for (const AiState& state : asset.states) {
            if (state.name.empty() || state.rootNode >= asset.nodes.size()) return { .valid = false, .error = "AI state has an invalid name or root node." };
            if (!ValidateSubtree(asset, state.rootNode, marks, 0U, error)) return { .valid = false, .error = std::move(error) };
            for (const AiStateTransition& transition : state.transitions) {
                if (transition.condition == 0U || transition.targetState >= asset.states.size()) {
                    return { .valid = false, .error = "AI state transition is invalid." };
                }
            }
        }
    }
    if (std::find(marks.begin(), marks.end(), 0U) != marks.end()) {
        return { .valid = false, .error = "AI behaviour asset contains an unreachable node." };
    }
    return { .valid = true, .error = {} };
}

bool AiBehaviourRuntime::Initialize(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state) {
    if (!ValidateAiBehaviourAsset(asset).valid) {
        state = {};
        return false;
    }
    try {
        state.childCursors.assign(asset.nodes.size(), 0U);
    } catch (const std::bad_alloc&) {
        state = {};
        return false;
    }
    state.activeState = asset.states.empty() ? 0U : asset.initialState;
    state.initialized = true;
    return true;
}

AiExecutionStatus AiBehaviourRuntime::Tick(
    const AiBehaviourAsset& asset,
    AiBehaviourRuntimeState& state,
    const AiBehaviourCallbacks& callbacks) noexcept {
    if (!state.initialized || state.childCursors.size() != asset.nodes.size() || asset.nodes.empty()) return AiExecutionStatus::Invalid;
    std::uint32_t root = asset.rootNode;
    if (!asset.states.empty()) {
        if (state.activeState >= asset.states.size()) return AiExecutionStatus::Invalid;
        const AiState& activeState = asset.states[state.activeState];
        for (const AiStateTransition& transition : activeState.transitions) {
            if (callbacks.condition == nullptr) return AiExecutionStatus::Invalid;
            if (callbacks.condition(callbacks.context, transition.condition)) {
                state.activeState = transition.targetState;
                std::fill(state.childCursors.begin(), state.childCursors.end(), 0U);
                break;
            }
        }
        root = asset.states[state.activeState].rootNode;
    }
    return EvaluateNode(asset, state, callbacks, root);
}

void AiBehaviourRuntime::Reset(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state) noexcept {
    if (!state.initialized || state.childCursors.size() != asset.nodes.size()) return;
    std::fill(state.childCursors.begin(), state.childCursors.end(), 0U);
    state.activeState = asset.states.empty() ? 0U : asset.initialState;
}

} // namespace kb::scene
