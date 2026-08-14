#include "ParticleEditorCommands.hpp"

#include "ParticleEmitterListModel.hpp"

#include <algorithm>

namespace kb::particle_editor {
namespace {

[[nodiscard]] ParticleEditorResult CommandError(
    ParticleEditorStatus status,
    kb::scene::ParticleEffectDiagnosticCode code,
    std::string path,
    std::string message,
    kb::scene::ParticleStableId emitterId = 0U) {
    return {.status = status,
            .message = message,
            .diagnostics = {{.code = code, .propertyPath = std::move(path),
                             .emitterId = emitterId, .message = std::move(message)}}};
}

[[nodiscard]] auto FindMutable(kb::scene::ParticleEffectAsset& asset,
                               kb::scene::ParticleStableId emitterId) {
    return std::find_if(asset.emitters.begin(), asset.emitters.end(),
        [emitterId](const auto& emitter) { return emitter.emitterId == emitterId; });
}

[[nodiscard]] ParticleEditorResult Apply(
    ParticleEditorDocument& document,
    ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleEffectAsset candidate,
    kb::scene::ParticleStableId selectedEmitterId) {
    ParticleEditorResult result = document.Apply(std::move(candidate));
    if (!result.Succeeded())
        return result;
    workspace.Synchronize(document.Asset());
    if (selectedEmitterId != 0U)
        static_cast<void>(workspace.Select(document.Asset(), selectedEmitterId));
    return result;
}

[[nodiscard]] auto FindModuleMutable(kb::scene::ParticleEmitterAsset& emitter,
                                     kb::scene::ParticleStableId moduleId) {
    return std::find_if(emitter.modules.begin(), emitter.modules.end(),
        [moduleId](const auto& module) { return module.moduleId == moduleId; });
}

} // namespace

ParticleEditorResult ParticleEditorCommands::AddEmitter(
    ParticleEditorDocument& document,
    ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleAssetReference material) {
    const kb::scene::ParticleEffectAsset& current = document.Asset();
    if (current.emitters.size() >= kb::scene::kParticleEffectMaxEmitters) {
        return CommandError(ParticleEditorStatus::LimitExceeded,
            kb::scene::ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitterCount",
            "particle effect already contains the maximum number of emitters");
    }
    if (material.Empty()) {
        return CommandError(ParticleEditorStatus::InvalidAsset,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference,
            "effect.emitter[" + std::to_string(current.emitters.size()) + "].output.material",
            "a valid material selection is required before adding an emitter");
    }
    kb::scene::ParticleStableId nextId = 1U;
    for (const auto& emitter : current.emitters) {
        if (emitter.emitterId != nextId)
            break;
        ++nextId;
    }
    kb::scene::ParticleEffectAsset candidate = current;
    kb::scene::ParticleEmitterAsset emitter;
    emitter.emitterId = nextId;
    emitter.authoringOrder = static_cast<std::uint32_t>(candidate.emitters.size());
    emitter.name = "Emitter " + std::to_string(candidate.emitters.size() + 1U);
    emitter.output.material = std::move(material);
    candidate.emitters.insert(std::lower_bound(candidate.emitters.begin(), candidate.emitters.end(), nextId,
        [](const kb::scene::ParticleEmitterAsset& candidateEmitter, kb::scene::ParticleStableId id) {
            return candidateEmitter.emitterId < id;
        }), std::move(emitter));
    return Apply(document, workspace, std::move(candidate), nextId);
}

ParticleEditorResult ParticleEditorCommands::RenameEmitter(
    ParticleEditorDocument& document,
    ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId,
    std::string name) {
    kb::scene::ParticleEffectAsset candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter", "emitter does not exist", emitterId);
    emitter->name = std::move(name);
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::SetEmitterEnabled(
    ParticleEditorDocument& document,
    ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId,
    bool enabled) {
    kb::scene::ParticleEffectAsset candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter", "emitter does not exist", emitterId);
    emitter->enabled = enabled;
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::ReorderEmitter(
    ParticleEditorDocument& document,
    ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId,
    std::uint32_t targetOrder) {
    kb::scene::ParticleEffectAsset candidate = document.Asset();
    const auto moved = FindMutable(candidate, emitterId);
    if (moved == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter", "emitter does not exist", emitterId);
    if (targetOrder >= static_cast<std::uint32_t>(candidate.emitters.size()))
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidValue, "effect.emitter.authoringOrder",
            "emitter reorder target is outside the authored list", emitterId);
    const std::uint32_t sourceOrder = moved->authoringOrder;
    if (sourceOrder == targetOrder)
        return {.status = ParticleEditorStatus::NoChange};
    for (auto& emitter : candidate.emitters) {
        if (emitter.emitterId == emitterId)
            continue;
        if (sourceOrder < targetOrder && emitter.authoringOrder > sourceOrder && emitter.authoringOrder <= targetOrder)
            --emitter.authoringOrder;
        else if (targetOrder < sourceOrder && emitter.authoringOrder >= targetOrder && emitter.authoringOrder < sourceOrder)
            ++emitter.authoringOrder;
    }
    moved->authoringOrder = targetOrder;
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::RemoveEmitter(
    ParticleEditorDocument& document,
    ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId) {
    kb::scene::ParticleEffectAsset candidate = document.Asset();
    const auto removed = FindMutable(candidate, emitterId);
    if (removed == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter", "emitter does not exist", emitterId);
    if (candidate.emitters.size() == 1U)
        return CommandError(ParticleEditorStatus::LimitExceeded,
            kb::scene::ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitterCount",
            "a particle effect must retain at least one emitter", emitterId);
    const std::uint32_t removedOrder = removed->authoringOrder;
    candidate.emitters.erase(removed);
    for (auto& emitter : candidate.emitters) {
        if (emitter.authoringOrder > removedOrder)
            --emitter.authoringOrder;
        std::vector<std::uint32_t> removedModuleOrders;
        for (const auto& module : emitter.modules) {
            const auto* payload = std::get_if<kb::scene::ParticleSubEmitterModule>(&module.payload);
            if (payload != nullptr && payload->targetEmitterId == emitterId)
                removedModuleOrders.push_back(module.authoringOrder);
        }
        std::erase_if(emitter.modules, [emitterId](const kb::scene::ParticleModuleAsset& module) {
            const auto* payload = std::get_if<kb::scene::ParticleSubEmitterModule>(&module.payload);
            return payload != nullptr && payload->targetEmitterId == emitterId;
        });
        for (auto& module : emitter.modules)
            module.authoringOrder -= static_cast<std::uint32_t>(std::count_if(
                removedModuleOrders.begin(), removedModuleOrders.end(),
                [&](std::uint32_t order) { return order < module.authoringOrder; }));
    }
    std::erase_if(candidate.eventBindings, [emitterId](const kb::scene::ParticleEventBindingAsset& binding) {
        return binding.sourceEmitterId == emitterId ||
            (binding.action == kb::scene::ParticleEventAction::EmitTargetEmitter && binding.targetEmitterId == emitterId);
    });
    const auto next = std::min_element(candidate.emitters.begin(), candidate.emitters.end(),
        [](const auto& left, const auto& right) { return left.authoringOrder < right.authoringOrder; });
    return Apply(document, workspace, std::move(candidate), next->emitterId);
}

ParticleEditorResult ParticleEditorCommands::SetEmitterSpawn(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleSpawnAsset spawn) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter",
            "emitter does not exist", emitterId);
    emitter->spawn = std::move(spawn);
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::SetEmitterOutput(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleOutputAsset output) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter",
            "emitter does not exist", emitterId);
    emitter->output = std::move(output);
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::AddModule(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleModuleType type) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter",
            "emitter does not exist", emitterId);
    if (emitter->modules.size() >= kb::scene::kParticleEffectMaxModulesPerEmitter)
        return CommandError(ParticleEditorStatus::LimitExceeded,
            kb::scene::ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitter.moduleCount",
            "emitter already contains the maximum number of modules", emitterId);
    if (!kb::scene::IsRepeatableParticleModule(type) &&
        std::any_of(emitter->modules.begin(), emitter->modules.end(),
            [type](const auto& module) { return module.type == type; }))
        return CommandError(ParticleEditorStatus::InvalidAsset,
            kb::scene::ParticleEffectDiagnosticCode::DuplicateModule, "effect.emitter.module.type",
            "this module type may occur only once", emitterId);
    kb::scene::ParticleStableId nextId = 1U;
    for (const auto& module : emitter->modules) {
        if (module.moduleId != nextId) break;
        ++nextId;
    }
    kb::scene::ParticleModuleAsset module{.moduleId = nextId,
        .authoringOrder = static_cast<std::uint32_t>(emitter->modules.size()), .type = type,
        .payload = kb::scene::DefaultParticleModulePayload(type)};
    emitter->modules.insert(std::lower_bound(emitter->modules.begin(), emitter->modules.end(), nextId,
        [](const auto& value, kb::scene::ParticleStableId id) { return value.moduleId < id; }), std::move(module));
    auto result = Apply(document, workspace, std::move(candidate), emitterId);
    if (result.Succeeded()) static_cast<void>(workspace.SelectModule(document.Asset(), emitterId, nextId));
    return result;
}

ParticleEditorResult ParticleEditorCommands::SetModuleEnabled(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId, bool enabled) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter",
            "emitter does not exist", emitterId);
    const auto module = FindModuleMutable(*emitter, moduleId);
    if (module == emitter->modules.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter.module",
            "module does not exist", emitterId);
    module->enabled = enabled;
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::SetModulePayload(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId,
    kb::scene::ParticleModulePayload payload) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter",
            "emitter does not exist", emitterId);
    const auto module = FindModuleMutable(*emitter, moduleId);
    if (module == emitter->modules.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter.module",
            "module does not exist", emitterId);
    module->payload = std::move(payload);
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::ReorderModule(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId,
    std::uint32_t targetOrder) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end() || targetOrder >= emitter->modules.size())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidValue, "effect.emitter.module.authoringOrder",
            "module reorder target is outside the authored list", emitterId);
    const auto moved = FindModuleMutable(*emitter, moduleId);
    if (moved == emitter->modules.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter.module",
            "module does not exist", emitterId);
    const std::uint32_t sourceOrder = moved->authoringOrder;
    if (sourceOrder == targetOrder) return {.status = ParticleEditorStatus::NoChange};
    for (auto& module : emitter->modules) {
        if (module.moduleId == moduleId) continue;
        if (sourceOrder < targetOrder && module.authoringOrder > sourceOrder && module.authoringOrder <= targetOrder)
            --module.authoringOrder;
        else if (targetOrder < sourceOrder && module.authoringOrder >= targetOrder && module.authoringOrder < sourceOrder)
            ++module.authoringOrder;
    }
    moved->authoringOrder = targetOrder;
    return Apply(document, workspace, std::move(candidate), emitterId);
}

ParticleEditorResult ParticleEditorCommands::RemoveModule(
    ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
    kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId) {
    auto candidate = document.Asset();
    const auto emitter = FindMutable(candidate, emitterId);
    if (emitter == candidate.emitters.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter",
            "emitter does not exist", emitterId);
    const auto removed = FindModuleMutable(*emitter, moduleId);
    if (removed == emitter->modules.end())
        return CommandError(ParticleEditorStatus::InvalidSelection,
            kb::scene::ParticleEffectDiagnosticCode::InvalidReference, "effect.emitter.module",
            "module does not exist", emitterId);
    const std::uint32_t order = removed->authoringOrder;
    emitter->modules.erase(removed);
    for (auto& module : emitter->modules)
        if (module.authoringOrder > order) --module.authoringOrder;
    std::erase_if(candidate.eventBindings, [emitterId, moduleId](const auto& binding) {
        return binding.sourceEmitterId == emitterId && binding.sourceModuleId == moduleId;
    });
    auto result = Apply(document, workspace, std::move(candidate), emitterId);
    if (result.Succeeded()) workspace.ClearSelectedModule();
    return result;
}

} // namespace kb::particle_editor
