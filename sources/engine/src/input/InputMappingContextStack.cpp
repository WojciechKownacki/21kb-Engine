#include "engine/input/InputMappingContextStack.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace kb::input {

void InputMappingContextStack::SetResolvers(ActionResolver actionResolver, ContextResolver contextResolver) {
    actionResolver_ = std::move(actionResolver);
    contextResolver_ = std::move(contextResolver);
}

bool InputMappingContextStack::Add(std::uint64_t contextId, std::int32_t priority) {
    if (!actionResolver_ || !contextResolver_) {
        return false;
    }
    const std::shared_ptr<const InputMappingContextAsset> context = contextResolver_(contextId);
    if (context == nullptr) {
        return false;
    }

    ActiveMappingContext active;
    active.contextId = contextId;
    active.priority = priority;
    active.mappings.reserve(context->mappings.size());
    for (const InputKeyMapping& mapping : context->mappings) {
        ResolvedMapping resolved;
        resolved.key = mapping.key;
        resolved.scale = mapping.scale;
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

    Remove(contextId);
    contexts_.push_back(std::move(active));
    SortByPriority();
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

void InputMappingContextStack::SortByPriority() {
    std::ranges::stable_sort(contexts_, [](const ActiveMappingContext& lhs, const ActiveMappingContext& rhs) {
        return lhs.priority > rhs.priority;
    });
}

} // namespace kb::input
