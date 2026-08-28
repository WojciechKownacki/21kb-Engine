#pragma once

#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputActionValueType.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputModifiers.hpp"
#include "engine/input/InputRebinding.hpp"
#include "engine/input/InputTriggerDesc.hpp"
#include "engine/input/InputTriggers.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::input {

// A resolved mapping: one key->action binding with its action metadata resolved
// and per-frame state-machine storage for modifiers and triggers.
struct ResolvedMapping {
    std::uint64_t bindingId = 0U;
    std::string actionName;
    InputActionValueType valueType = InputActionValueType::Bool;
    bool consumeInput = true;
    InputKey key = InputKey::None;
    float scale = 1.0F;
    std::uint8_t gamepadIndex = 0U;
    std::vector<InputModifierDesc> modifiers;
    std::vector<InputTriggerDesc> triggers;
    std::vector<std::string> chordActionNames; // parallel to triggers; empty unless Chorded
    ModifierRuntimeState modifierState{};
    std::vector<TriggerRuntimeState> triggerStates;
};

// A resolved composite: several keys combined into one action's value, with the
// action metadata resolved and per-frame state-machine storage for the combined
// modifier/trigger stack. See InputCompositeBinding for why this is not just a
// set of ResolvedMappings summed independently.
struct ResolvedComposite {
    std::uint64_t bindingId = 0U;
    std::string actionName;
    InputActionValueType valueType = InputActionValueType::Bool;
    bool consumeInput = true;
    std::vector<InputCompositeSlot> slots;
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
    std::vector<ResolvedComposite> composites;
};

struct InputRuntimeRebindResult {
    bool applied = false;
    std::optional<InputRebindConflict> conflict;
};

// Owns the prioritized stack of active mapping contexts and turns mapping-context
// assets into ResolvedMappings via the configured resolvers. This is the
// "pushed contexts" half of the input stack; InputSubsystem owns evaluation.
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

    // Stores a user override above the immutable mapping-context asset and
    // immediately rebuilds the live resolved context without changing its
    // priority. A rejected conflict leaves both the profile and live mappings
    // untouched.
    [[nodiscard]] InputRuntimeRebindResult Rebind(
        std::uint64_t contextId, std::uint64_t bindingId, InputKey newKey,
        std::uint8_t gamepadIndex, bool allowConflict = false);

    // Replaces the complete persisted profile for one context. Stale binding
    // ids are retained in the profile (so a temporarily removed binding can
    // return in a later content version) and ignored while resolving.
    [[nodiscard]] bool SetRebindProfile(
        std::uint64_t contextId, std::span<const InputRebindOverride> overrides);
    [[nodiscard]] std::span<const InputRebindOverride> RebindProfile(
        std::uint64_t contextId) const noexcept;

    // Active contexts, sorted by priority (descending). Mutable so the evaluator
    // can advance each mapping's per-frame runtime state.
    [[nodiscard]] std::span<ActiveMappingContext> Active() noexcept {
        return contexts_;
    }

private:
    [[nodiscard]] bool BuildActive(
        std::uint64_t contextId, std::int32_t priority,
        std::span<const InputRebindOverride> overrides,
        ActiveMappingContext& active) const;
    [[nodiscard]] std::optional<std::int32_t> ActivePriority(
        std::uint64_t contextId) const noexcept;
    void ReplaceActive(ActiveMappingContext active);
    void SortByPriority();

    ActionResolver actionResolver_;
    ContextResolver contextResolver_;
    std::vector<ActiveMappingContext> contexts_; // sorted by priority, descending
    std::unordered_map<std::uint64_t, std::vector<InputRebindOverride>> rebindProfiles_;
};

} // namespace kb::input
