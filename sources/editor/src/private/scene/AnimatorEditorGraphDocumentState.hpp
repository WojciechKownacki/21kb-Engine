#pragma once

#include "engine/scene/AnimationAssets.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::editor {

class AnimatorEditorGraphDocumentState {
public:
    void Open(kb::scene::AnimatorController controller) {
        controller_ = std::move(controller);
        selection_.clear();
        clipboard_.clear();
        motionStateId_ = 0U;
        nextId_ = 1U;
        ReserveExistingIds();
        history_.assign(1U, *controller_);
        historyCursor_ = 0U;
    }

    [[nodiscard]] const kb::scene::AnimatorController* Controller() const noexcept {
        return controller_.has_value() ? &*controller_ : nullptr;
    }
    [[nodiscard]] const std::vector<std::uint64_t>& Selection() const noexcept { return selection_; }
    [[nodiscard]] bool Dirty() const noexcept { return historyCursor_ != 0U; }
    [[nodiscard]] bool CanUndo() const noexcept { return historyCursor_ > 0U; }
    [[nodiscard]] bool CanRedo() const noexcept { return historyCursor_ + 1U < history_.size(); }
    [[nodiscard]] bool Undo() noexcept { if (!CanUndo()) return false; controller_ = history_[--historyCursor_]; selection_.clear(); return true; }
    [[nodiscard]] bool Redo() noexcept { if (!CanRedo()) return false; controller_ = history_[++historyCursor_]; selection_.clear(); return true; }
    [[nodiscard]] bool Revert() noexcept {
        if (!Dirty()) return false;
        controller_ = history_.front();
        history_.resize(1U);
        historyCursor_ = 0U;
        selection_.clear();
        nextId_ = 1U;
        ReserveExistingIds();
        return true;
    }
    [[nodiscard]] bool MarkSaved() { if (!controller_.has_value()) return false; history_.assign(1U, *controller_); historyCursor_ = 0U; return true; }
    [[nodiscard]] std::uint64_t MotionState() const noexcept { return motionStateId_; }

    [[nodiscard]] bool OpenMotionDocument(std::uint64_t stateId) {
        if (FindState(stateId) == nullptr || motionStateId_ == stateId) return false;
        motionStateId_ = stateId;
        return true;
    }
    [[nodiscard]] bool CloseMotionDocument() noexcept {
        if (motionStateId_ == 0U) return false;
        motionStateId_ = 0U;
        return true;
    }

    [[nodiscard]] bool SetSelection(std::vector<std::uint64_t> stateIds) {
        if (!controller_.has_value()) return false;
        std::ranges::sort(stateIds);
        stateIds.erase(std::unique(stateIds.begin(), stateIds.end()), stateIds.end());
        if (!std::ranges::all_of(stateIds, [this](std::uint64_t id) { return FindState(id) != nullptr; })) return false;
        if (selection_ == stateIds) return false;
        selection_ = std::move(stateIds);
        return true;
    }

    [[nodiscard]] bool RenameState(std::uint64_t stateId, std::string name) {
        if (name.empty() || !controller_.has_value()) return false;
        StateLocation location{};
        if (!LocateState(stateId, location)) return false;
        kb::scene::AnimatorControllerLayer& layer = controller_->layers[location.layer];
        if (std::ranges::any_of(layer.states, [&name, stateId](const auto& value) { return value.id != stateId && value.name == name; })) return false;
        kb::scene::AnimatorControllerState& state = layer.states[location.state];
        const std::string previous = state.name;
        if (previous == name) return false;
        state.name = std::move(name);
        if (layer.defaultState == previous) layer.defaultState = state.name;
        for (kb::scene::AnimatorControllerTransition& transition : layer.transitions) {
            if (transition.fromState == previous) transition.fromState = state.name;
            if (transition.toState == previous) transition.toState = state.name;
        }
        RecordMutation();
        return true;
    }

    [[nodiscard]] std::uint64_t AddState(std::string_view layerName, std::string name, std::string clipReference, std::int32_t x, std::int32_t y) {
        if (!controller_.has_value() || name.empty() || clipReference.empty()) return 0U;
        const auto layer = std::ranges::find_if(controller_->layers, [layerName](const auto& value) { return value.name == layerName; });
        if (layer == controller_->layers.end() || std::ranges::any_of(layer->states, [&name](const auto& state) { return state.name == name; })) return 0U;
        const std::uint64_t id = AllocateId();
        layer->states.push_back({ .id = id, .name = std::move(name), .clipReference = std::move(clipReference) });
        controller_->graphLayout.push_back({ .stateId = id, .positionX = x, .positionY = y });
        selection_ = { id };
        RecordMutation();
        return id;
    }

    [[nodiscard]] bool DeleteSelectedStates() {
        if (!controller_.has_value() || selection_.empty()) return false;
        std::unordered_set<std::uint64_t> removed{ selection_.begin(), selection_.end() };
        for (const kb::scene::AnimatorControllerLayer& layer : controller_->layers) {
            const std::size_t remaining = std::count_if(layer.states.begin(), layer.states.end(), [&removed](const auto& state) {
                return !removed.contains(state.id);
            });
            if (remaining == 0U) return false;
        }
        for (kb::scene::AnimatorControllerLayer& layer : controller_->layers) {
            std::unordered_set<std::string> removedNames;
            for (const auto& state : layer.states) if (removed.contains(state.id)) removedNames.insert(state.name);
            layer.states.erase(std::remove_if(layer.states.begin(), layer.states.end(), [&removed](const auto& state) {
                return removed.contains(state.id);
            }), layer.states.end());
            layer.transitions.erase(std::remove_if(layer.transitions.begin(), layer.transitions.end(), [&removedNames](const auto& transition) {
                return removedNames.contains(transition.fromState) || removedNames.contains(transition.toState);
            }), layer.transitions.end());
            if (removedNames.contains(layer.defaultState)) layer.defaultState = layer.states.front().name;
        }
        controller_->graphLayout.erase(std::remove_if(controller_->graphLayout.begin(), controller_->graphLayout.end(), [&removed](const auto& node) {
            return removed.contains(node.stateId);
        }), controller_->graphLayout.end());
        for (kb::scene::AnimatorGraphGroup& group : controller_->graphGroups) {
            group.stateIds.erase(std::remove_if(group.stateIds.begin(), group.stateIds.end(), [&removed](std::uint64_t id) {
                return removed.contains(id);
            }), group.stateIds.end());
        }
        selection_.clear();
        RecordMutation();
        return true;
    }

    [[nodiscard]] std::uint64_t AddComment(std::string text, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
        if (!controller_.has_value() || text.empty() || width <= 0 || height <= 0) return 0U;
        const std::uint64_t id = AllocateId();
        controller_->graphComments.push_back({ .id = id, .text = std::move(text), .positionX = x, .positionY = y, .width = width, .height = height });
        RecordMutation();
        return id;
    }

    [[nodiscard]] std::uint64_t AddGroup(std::string name, std::vector<std::uint64_t> stateIds) {
        if (!controller_.has_value() || name.empty() || stateIds.empty() ||
            !std::ranges::all_of(stateIds, [this](std::uint64_t id) { return FindState(id) != nullptr; })) return 0U;
        std::ranges::sort(stateIds);
        stateIds.erase(std::unique(stateIds.begin(), stateIds.end()), stateIds.end());
        const std::uint64_t id = AllocateId();
        controller_->graphGroups.push_back({ .id = id, .name = std::move(name), .stateIds = std::move(stateIds) });
        RecordMutation();
        return id;
    }

    [[nodiscard]] bool CopySelection() {
        if (!controller_.has_value() || selection_.empty()) return false;
        clipboard_.clear();
        for (const kb::scene::AnimatorControllerLayer& layer : controller_->layers) {
            for (const kb::scene::AnimatorControllerState& state : layer.states) {
                if (std::ranges::find(selection_, state.id) != selection_.end()) clipboard_.push_back({ layer.name, state });
            }
        }
        return !clipboard_.empty();
    }

    [[nodiscard]] bool PasteIntoLayer(std::string_view layerName, std::int32_t offsetX, std::int32_t offsetY) {
        if (!controller_.has_value() || clipboard_.empty()) return false;
        const auto layerIt = std::ranges::find_if(controller_->layers, [layerName](const auto& layer) { return layer.name == layerName; });
        if (layerIt == controller_->layers.end()) return false;
        std::vector<std::uint64_t> pasted;
        for (const ClipboardState& copied : clipboard_) {
            if (copied.layerName != layerName) continue;
            kb::scene::AnimatorControllerState state = copied.state;
            state.id = AllocateId();
            const std::string base = state.name;
            std::uint32_t suffix = 1U;
            while (std::ranges::any_of(layerIt->states, [&state](const auto& existing) { return existing.name == state.name; })) {
                state.name = base + " " + std::to_string(suffix++);
            }
            layerIt->states.push_back(state);
            const auto source = std::ranges::find_if(controller_->graphLayout, [&copied](const auto& node) {
                return node.stateId == copied.state.id;
            });
            controller_->graphLayout.push_back({ .stateId = state.id, .positionX = (source == controller_->graphLayout.end() ? 0 : source->positionX) + offsetX,
                .positionY = (source == controller_->graphLayout.end() ? 0 : source->positionY) + offsetY });
            pasted.push_back(state.id);
        }
        if (pasted.empty()) return false;
        RecordMutation();
        return SetSelection(std::move(pasted));
    }

private:
    struct StateLocation { std::size_t layer = 0U; std::size_t state = 0U; };
    struct ClipboardState { std::string layerName; kb::scene::AnimatorControllerState state; };

    [[nodiscard]] const kb::scene::AnimatorControllerState* FindState(std::uint64_t id) const noexcept {
        if (!controller_.has_value()) return nullptr;
        for (const auto& layer : controller_->layers) for (const auto& state : layer.states) if (state.id == id) return &state;
        return nullptr;
    }
    [[nodiscard]] bool LocateState(std::uint64_t id, StateLocation& location) const noexcept {
        if (!controller_.has_value()) return false;
        for (std::size_t layer = 0U; layer < controller_->layers.size(); ++layer) for (std::size_t state = 0U; state < controller_->layers[layer].states.size(); ++state)
            if (controller_->layers[layer].states[state].id == id) { location = { layer, state }; return true; }
        return false;
    }
    void ReserveExistingIds() noexcept {
        if (!controller_.has_value()) return;
        for (const auto& layer : controller_->layers) { for (const auto& state : layer.states) nextId_ = std::max(nextId_, state.id + 1U); for (const auto& edge : layer.transitions) nextId_ = std::max(nextId_, edge.id + 1U); }
        for (const auto& comment : controller_->graphComments) nextId_ = std::max(nextId_, comment.id + 1U);
        for (const auto& group : controller_->graphGroups) nextId_ = std::max(nextId_, group.id + 1U);
    }
    [[nodiscard]] std::uint64_t AllocateId() noexcept { return nextId_++; }
    void RecordMutation() {
        if (!controller_.has_value()) return;
        history_.resize(historyCursor_ + 1U);
        history_.push_back(*controller_);
        ++historyCursor_;
    }

    std::optional<kb::scene::AnimatorController> controller_;
    std::vector<std::uint64_t> selection_;
    std::vector<ClipboardState> clipboard_;
    std::vector<kb::scene::AnimatorController> history_;
    std::size_t historyCursor_ = 0U;
    std::uint64_t nextId_ = 1U;
    std::uint64_t motionStateId_ = 0U;
};

} // namespace kb::editor
