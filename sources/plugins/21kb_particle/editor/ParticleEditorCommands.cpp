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
        std::erase_if(emitter.modules, [emitterId](const kb::scene::ParticleModuleAsset& module) {
            const auto* payload = std::get_if<kb::scene::ParticleSubEmitterModule>(&module.payload);
            return payload != nullptr && payload->targetEmitterId == emitterId;
        });
    }
    std::erase_if(candidate.eventBindings, [emitterId](const kb::scene::ParticleEventBindingAsset& binding) {
        return binding.sourceEmitterId == emitterId ||
            (binding.action == kb::scene::ParticleEventAction::EmitTargetEmitter && binding.targetEmitterId == emitterId);
    });
    const auto next = std::min_element(candidate.emitters.begin(), candidate.emitters.end(),
        [](const auto& left, const auto& right) { return left.authoringOrder < right.authoringOrder; });
    return Apply(document, workspace, std::move(candidate), next->emitterId);
}

} // namespace kb::particle_editor
