#include "engine/input/InputMappingContextStack.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace kb::input {

void InputMappingContextStack::SetResolvers(ActionResolver actionResolver, ContextResolver contextResolver) {
    actionResolver_ = std::move(actionResolver);
    contextResolver_ = std::move(contextResolver);
}

bool InputMappingContextStack::Add(std::uint64_t contextId, std::int32_t priority) {
    ActiveMappingContext active;
    if (!BuildActive(contextId, priority, RebindProfile(contextId), active)) {
        return false;
    }
    ReplaceActive(std::move(active));
    return true;
}

bool InputMappingContextStack::BuildActive(
    std::uint64_t contextId, std::int32_t priority,
    std::span<const InputRebindOverride> overrides,
    ActiveMappingContext& active) const {
    if (!actionResolver_ || !contextResolver_) {
        return false;
    }
    const std::shared_ptr<const InputMappingContextAsset> baseContext =
        contextResolver_(contextId);
    if (baseContext == nullptr) {
        return false;
    }

    InputMappingContextAsset context = *baseContext;
    ApplyRebindProfile(context, overrides);

    active = ActiveMappingContext{};
    active.contextId = contextId;
    active.priority = priority;
    active.mappings.reserve(context.mappings.size());
    for (const InputKeyMapping& mapping : context.mappings) {
        ResolvedMapping resolved;
        resolved.bindingId = mapping.bindingId;
        resolved.key = mapping.key;
        resolved.scale = mapping.scale;
        resolved.gamepadIndex = mapping.gamepadIndex;
        resolved.modifiers = mapping.modifiers;
        resolved.triggers = mapping.triggers;
        resolved.triggerStates.resize(mapping.triggers.size());
        resolved.chordActionNames.resize(mapping.triggers.size());

        if (const std::shared_ptr<const InputActionAsset> action = actionResolver_(mapping.actionId)) {
            resolved.actionName = action->name;
            resolved.valueType = action->valueType;
            resolved.consumeInput = action->consumeInput;
        }

        for (std::size_t index = 0U; index < mapping.triggers.size(); ++index) {
            const InputTriggerDesc& trigger = mapping.triggers[index];
            if (trigger.type == InputTriggerType::Chorded && trigger.chordActionId != 0U) {
                if (const std::shared_ptr<const InputActionAsset> chord = actionResolver_(trigger.chordActionId)) {
                    resolved.chordActionNames[index] = chord->name;
                }
            }
        }

        if (!resolved.actionName.empty()) {
            active.mappings.push_back(std::move(resolved));
        }
    }

    active.composites.reserve(context.composites.size());
    for (const InputCompositeBinding& composite : context.composites) {
        ResolvedComposite resolved;
        resolved.bindingId = composite.bindingId;
        resolved.slots = composite.slots;
        resolved.modifiers = composite.modifiers;
        resolved.triggers = composite.triggers;
        resolved.triggerStates.resize(composite.triggers.size());
        resolved.chordActionNames.resize(composite.triggers.size());

        if (const std::shared_ptr<const InputActionAsset> action = actionResolver_(composite.actionId)) {
            resolved.actionName = action->name;
            resolved.valueType = action->valueType;
            resolved.consumeInput = action->consumeInput;
        }

        for (std::size_t index = 0U; index < composite.triggers.size(); ++index) {
            const InputTriggerDesc& trigger = composite.triggers[index];
            if (trigger.type == InputTriggerType::Chorded && trigger.chordActionId != 0U) {
                if (const std::shared_ptr<const InputActionAsset> chord = actionResolver_(trigger.chordActionId)) {
                    resolved.chordActionNames[index] = chord->name;
                }
            }
        }

        if (!resolved.actionName.empty()) {
            active.composites.push_back(std::move(resolved));
        }
    }

    return true;
}

void InputMappingContextStack::Remove(std::uint64_t contextId) {
    std::erase_if(contexts_, [contextId](const ActiveMappingContext& context) {
        return context.contextId == contextId;
    });
}

void InputMappingContextStack::Clear() noexcept {
    contexts_.clear();
}

bool InputMappingContextStack::Has(std::uint64_t contextId) const noexcept {
    return std::ranges::any_of(contexts_, [contextId](const ActiveMappingContext& context) {
        return context.contextId == contextId;
    });
}

InputRuntimeRebindResult InputMappingContextStack::Rebind(
    std::uint64_t contextId, std::uint64_t bindingId, InputKey newKey,
    std::uint8_t gamepadIndex, bool allowConflict) {
    InputRuntimeRebindResult result;
    if (!contextResolver_) {
        return result;
    }
    const std::shared_ptr<const InputMappingContextAsset> baseContext =
        contextResolver_(contextId);
    if (baseContext == nullptr) {
        return result;
    }

    InputMappingContextAsset effective = *baseContext;
    ApplyRebindProfile(effective, RebindProfile(contextId));
    result.conflict =
        FindRebindConflict(effective, bindingId, newKey, gamepadIndex);
    if (result.conflict.has_value() && !allowConflict) {
        return result;
    }
    if (!ApplyRebind(
            effective, bindingId, newKey, gamepadIndex,
            /*allowConflict=*/true)) {
        result.conflict.reset();
        return result;
    }

    std::vector<InputRebindOverride> proposed{
        RebindProfile(contextId).begin(), RebindProfile(contextId).end()};
    const auto existing = std::ranges::find_if(
        proposed, [bindingId](const InputRebindOverride& entry) {
            return entry.bindingId == bindingId;
        });
    const auto baseMapping = std::ranges::find_if(
        baseContext->mappings, [bindingId](const InputKeyMapping& mapping) {
            return mapping.bindingId == bindingId;
        });
    const bool restoresDefault =
        baseMapping != baseContext->mappings.end() &&
        baseMapping->key == newKey &&
        baseMapping->gamepadIndex == gamepadIndex;
    if (restoresDefault) {
        if (existing != proposed.end()) {
            proposed.erase(existing);
        }
    } else if (existing != proposed.end()) {
        existing->key = newKey;
        existing->gamepadIndex = gamepadIndex;
    } else {
        proposed.push_back(InputRebindOverride{
            .bindingId = bindingId,
            .key = newKey,
            .gamepadIndex = gamepadIndex,
        });
    }

    ActiveMappingContext rebuilt;
    const std::optional<std::int32_t> priority = ActivePriority(contextId);
    if (priority.has_value() &&
        !BuildActive(contextId, *priority, proposed, rebuilt)) {
        result.conflict.reset();
        return result;
    }

    if (proposed.empty()) {
        rebindProfiles_.erase(contextId);
    } else {
        rebindProfiles_[contextId] = std::move(proposed);
    }
    if (priority.has_value()) {
        ReplaceActive(std::move(rebuilt));
    }
    result.applied = true;
    return result;
}

bool InputMappingContextStack::SetRebindProfile(
    std::uint64_t contextId, std::span<const InputRebindOverride> overrides) {
    if (!IsValidRebindProfile(overrides) || !contextResolver_ ||
        contextResolver_(contextId) == nullptr) {
        return false;
    }

    ActiveMappingContext rebuilt;
    const std::optional<std::int32_t> priority = ActivePriority(contextId);
    if (priority.has_value() &&
        !BuildActive(contextId, *priority, overrides, rebuilt)) {
        return false;
    }

    if (overrides.empty()) {
        rebindProfiles_.erase(contextId);
    } else {
        rebindProfiles_[contextId] =
            std::vector<InputRebindOverride>{overrides.begin(), overrides.end()};
    }
    if (priority.has_value()) {
        ReplaceActive(std::move(rebuilt));
    }
    return true;
}

std::span<const InputRebindOverride> InputMappingContextStack::RebindProfile(
    std::uint64_t contextId) const noexcept {
    const auto found = rebindProfiles_.find(contextId);
    return found != rebindProfiles_.end()
        ? std::span<const InputRebindOverride>{found->second}
        : std::span<const InputRebindOverride>{};
}

std::optional<std::int32_t> InputMappingContextStack::ActivePriority(
    std::uint64_t contextId) const noexcept {
    const auto found = std::ranges::find_if(
        contexts_, [contextId](const ActiveMappingContext& context) {
            return context.contextId == contextId;
        });
    return found != contexts_.end()
        ? std::optional<std::int32_t>{found->priority}
        : std::nullopt;
}

void InputMappingContextStack::ReplaceActive(ActiveMappingContext active) {
    Remove(active.contextId);
    contexts_.push_back(std::move(active));
    SortByPriority();
}

void InputMappingContextStack::SortByPriority() {
    std::ranges::stable_sort(contexts_, [](const ActiveMappingContext& lhs, const ActiveMappingContext& rhs) {
        return lhs.priority > rhs.priority;
    });
}

} // namespace kb::input
