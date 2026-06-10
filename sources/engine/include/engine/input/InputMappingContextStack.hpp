#pragma once

#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputActionValueType.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputModifiers.hpp"
#include "engine/input/InputTriggerDesc.hpp"
#include "engine/input/InputTriggers.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kb::input {

// A resolved mapping: one key->action binding with its action metadata resolved
// and per-frame state-machine storage for modifiers and triggers.
struct ResolvedMapping {
    std::string actionName;
    InputActionValueType valueType = InputActionValueType::Bool;
    bool consumeInput = true;
    InputKey key = InputKey::None;
    std::vector<InputModifierDesc> modifiers;
    std::vector<InputTriggerDesc> triggers;
    std::vector<std::string> chordActionNames; // parallel to triggers; empty unless Chorded
    ModifierRuntimeState modifierState{};
    std::vector<TriggerRuntimeState> triggerStates;
};

struct ActiveMappingContext {
    std::uint64_t contextId = 0U;
    std::int32_t priority = 0;
    std::vector<ResolvedMapping> mappings;
};

// Owns the prioritized stack of active mapping contexts and turns mapping-context
// assets into ResolvedMappings via the configured resolvers. Mirrors the
// "pushed contexts" half of Unreal's Enhanced Input subsystem.
//
// Single responsibility: which contexts are active, in what priority order, with
// their resolved per-frame mappings.
class InputMappingContextStack {
public:
    using ActionResolver = std::function<std::shared_ptr<const InputActionAsset>(std::uint64_t)>;
    using ContextResolver = std::function<std::shared_ptr<const InputMappingContextAsset>(std::uint64_t)>;

    void SetResolvers(ActionResolver actionResolver, ContextResolver contextResolver);

    // Pushes a context (resolved via the configured resolvers); re-adding updates
    // its priority. Returns false when the context cannot be resolved.
    [[nodiscard]] bool Add(std::uint64_t contextId, std::int32_t priority);
    void Remove(std::uint64_t contextId);
    void Clear() noexcept;
    [[nodiscard]] bool Has(std::uint64_t contextId) const noexcept;

    // Active contexts, sorted by priority (descending). Mutable so the evaluator
    // can advance each mapping's per-frame runtime state.
    [[nodiscard]] std::span<ActiveMappingContext> Active() noexcept {
        return contexts_;
    }

private:
    void SortByPriority();

    ActionResolver actionResolver_;
    ContextResolver contextResolver_;
    std::vector<ActiveMappingContext> contexts_; // sorted by priority, descending
};

} // namespace kb::input
