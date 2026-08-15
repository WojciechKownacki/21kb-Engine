#include "editor/ParticleAssetGateway.hpp"
#include "editor/ParticleDocumentCloseGuard.hpp"
#include "editor/ParticleEditorDocument.hpp"
#include "editor/ParticleEditorCommands.hpp"
#include "editor/ParticleEditorWorkspaceState.hpp"
#include "editor/ParticleEmitterListModel.hpp"
#include "editor/ParticleEmitterInspectorModel.hpp"
#include "editor/ParticlePreviewSession.hpp"
#include "editor/ParticleBakeService.hpp"
#include "ParticleEffectCompiler.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef KB_21KB_PARTICLE_PLUGIN_PATH
#define KB_21KB_PARTICLE_PLUGIN_PATH ""
#endif

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeEffect(float rate = 60.0F) {
    kb::scene::ParticleEffectAsset effect;
    effect.effectId = 7001U;
    effect.displayName = "Editor Preview Effect";
    effect.recipeCategory = "Simple";
    effect.determinismSeed = 0xA1B2C3D4E5F60718ULL;
    effect.durationSeconds = 5.0F;
    effect.looping = true;
    kb::scene::ParticleEmitterAsset emitter;
    emitter.emitterId = 11U;
    emitter.authoringOrder = 0U;
    emitter.name = "Preview Emitter";
    emitter.maxParticles = 512U;
    emitter.spawn.rateOverTime.keyframes = {{.time = 0.0F, .value = rate}};
    emitter.spawn.lifetimeMin = 2.0F;
    emitter.spawn.lifetimeMax = 2.0F;
    emitter.spawn.direction = {1.0F, 0.0F, 0.0F};
    emitter.spawn.speedMin = 1.0F;
    emitter.spawn.speedMax = 1.0F;
    emitter.output.material.virtualPath = "/Game/Materials/PreviewParticle.21kb";
    effect.emitters.push_back(std::move(emitter));
    return effect;
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_particle_editor_tests";
}

[[nodiscard]] std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void TestDocumentHistorySavePointAndAtomicFailure() {
    const std::filesystem::path root = TestRoot() / "document";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "document fixture directory creation failed");

    kb::particle_editor::ParticleAssetGateway gateway;
    kb::particle_editor::ParticleEditorDocument document;
    Require(document.Create(MakeEffect()).Succeeded(), "new particle document creation failed");
    Require(document.Dirty() && !document.SessionPath().has_value(),
        "new unsaved particle document did not start dirty and pathless");
    Require(std::filesystem::is_empty(root), "creating an unsaved document touched disk");

    auto changed = document.Asset();
    changed.displayName = "Changed Preview Effect";
    Require(document.Apply(changed).Succeeded() && document.CanUndo(),
        "particle document edit was not recorded");
    Require(document.Undo() && document.Asset().displayName == "Editor Preview Effect",
        "particle document undo did not restore the prior asset");
    Require(document.Redo() && document.Asset().displayName == "Changed Preview Effect",
        "particle document redo did not restore the edit");
    Require(document.Apply(document.Asset()).status == kb::particle_editor::ParticleEditorStatus::NoChange,
        "canonical no-op edit polluted particle history");

    const std::filesystem::path failedPath = root / "CannotSave.kbvfx";
    const std::string oldBytes = "existing bytes remain";
    {
        std::ofstream output{failedPath, std::ios::binary};
        output << oldBytes;
    }
    std::filesystem::create_directory(failedPath.string() + ".tmp", error);
    Require(!error, "atomic failure fixture could not reserve the temporary path");
    const auto failed = document.Save(gateway, failedPath);
    Require(!failed.Succeeded() && failed.status == kb::particle_editor::ParticleEditorStatus::IoFailure,
        "particle document atomic save failure was not explicit");
    Require(!document.SessionPath().has_value() && document.Dirty() && ReadBytes(failedPath) == oldBytes,
        "failed first save adopted a path, cleared dirty state, or modified destination bytes");

    const std::filesystem::path savedPath = root / "Saved.kbvfx";
    Require(document.Save(gateway, savedPath).Succeeded(), "particle document first save failed");
    Require(document.SessionPath() == savedPath && !document.Dirty(),
        "successful first save did not atomically establish path and save point");
    changed = document.Asset();
    changed.durationSeconds = 7.0F;
    Require(document.Apply(changed).Succeeded() && document.Dirty(), "post-save edit was not dirty");
    Require(document.Revert() && !document.Dirty() && document.Asset().durationSeconds == 5.0F,
        "revert did not restore the exact saved particle asset");

    kb::particle_editor::ParticleEditorDocument opened;
    Require(opened.Open(gateway, savedPath).Succeeded() && !opened.Dirty(),
        "saved particle document did not reopen cleanly");
    const std::filesystem::path malformed = root / "Malformed.kbvfx";
    {
        std::ofstream output{malformed, std::ios::binary};
        output << "not a particle effect\n";
    }
    Require(!opened.Open(gateway, malformed).Succeeded() && opened.SessionPath() == savedPath &&
            opened.Asset().displayName == "Changed Preview Effect",
        "failed open damaged the active document session");
}

void TestEmitterListCommandsAndAuthoredCompileOrder() {
    using namespace kb::particle_editor;
    ParticleEditorDocument limited;
    ParticleEditorWorkspaceState limitedWorkspace;
    Require(limited.Create(MakeEffect()).Succeeded(), "emitter command fixture creation failed");
    limitedWorkspace.Synchronize(limited.Asset());
    const ParticleEditorResult cancelled = ParticleEditorCommands::AddEmitter(
        limited, limitedWorkspace, {});
    Require(cancelled.status == ParticleEditorStatus::InvalidAsset && !limited.CanUndo() &&
            limited.Asset().emitters.size() == 1U,
        "cancelled material selection mutated emitter history");
    for (std::uint32_t index = 1U; index < kb::scene::kParticleEffectMaxEmitters; ++index) {
        Require(ParticleEditorCommands::AddEmitter(limited, limitedWorkspace,
                    {.virtualPath = "/Game/Materials/Emitter" + std::to_string(index) + ".kbmat"}).Succeeded(),
            "valid material selection did not add an emitter");
    }
    const ParticleEditorResult overflow = ParticleEditorCommands::AddEmitter(
        limited, limitedWorkspace, {.virtualPath = "/Game/Materials/Overflow.kbmat"});
    Require(overflow.status == ParticleEditorStatus::LimitExceeded &&
            limited.Asset().emitters.size() == kb::scene::kParticleEffectMaxEmitters,
        "emitter limit boundary plus one was accepted");
    std::array<bool, kb::scene::kParticleEffectMaxEmitters> observedOrders{};
    for (std::size_t index = 0U; index < limited.Asset().emitters.size(); ++index) {
        const auto& emitter = limited.Asset().emitters[index];
        Require(emitter.authoringOrder < observedOrders.size() &&
                !observedOrders[emitter.authoringOrder] &&
                (index == 0U || limited.Asset().emitters[index - 1U].emitterId < emitter.emitterId),
            "add did not preserve stable-id storage and contiguous authoring order");
        observedOrders[emitter.authoringOrder] = true;
    }
    Require(std::all_of(observedOrders.begin(), observedOrders.end(), [](bool value) { return value; }),
        "add did not produce a complete authored-order permutation");

    auto ordered = MakeEffect();
    ordered.emitters[0].name = "First";
    for (std::uint32_t index = 1U; index < 3U; ++index) {
        auto emitter = ordered.emitters[0];
        emitter.emitterId = 11U + index;
        emitter.authoringOrder = index;
        emitter.name = index == 1U ? "Second" : "Third";
        ordered.emitters.push_back(std::move(emitter));
    }
    ParticleEditorDocument document;
    ParticleEditorWorkspaceState workspace;
    Require(document.Create(ordered).Succeeded(), "ordered emitter fixture creation failed");
    workspace.Synchronize(document.Asset());
    workspace.SetFocused(true);
    Require(workspace.Select(document.Asset(), 12U) && workspace.Focused() &&
            workspace.SelectedEmitterId() == 12U,
        "emitter selection did not establish stable-id keyboard focus");
    Require(ParticleEditorCommands::RenameEmitter(document, workspace, 12U, "Renamed").Succeeded() &&
            ParticleEditorCommands::SetEmitterEnabled(document, workspace, 12U, false).Succeeded(),
        "rename or enable command failed");
    Require(!document.Asset().emitters[1].enabled && document.Asset().emitters[1].name == "Renamed",
        "rename or enable command did not update the selected stable emitter");

    ParticleEditorDocument reorderDocument;
    ParticleEditorWorkspaceState reorderWorkspace;
    Require(reorderDocument.Create(ordered).Succeeded(), "reorder fixture creation failed");
    reorderWorkspace.Synchronize(reorderDocument.Asset());
    Require(ParticleEditorCommands::ReorderEmitter(reorderDocument, reorderWorkspace, 13U, 0U).Succeeded() &&
            reorderDocument.CanUndo() && reorderDocument.Asset().emitters[2].authoringOrder == 0U,
        "drag reorder did not record the authored order");
    Require(reorderDocument.Undo() && !reorderDocument.CanUndo() &&
            reorderDocument.Asset().emitters[2].authoringOrder == 2U,
        "one drag reorder did not produce exactly one history command");
    Require(reorderDocument.Redo(), "reorder redo failed");

    kb::assets::AssetRegistry registry;
    Require(registry.Upsert({.id = kb::assets::AssetId{80U}, .type = "RenderMaterial",
                .virtualPath = "/Game/Materials/PreviewParticle.21kb", .contentHash = 1U}),
        "compiler order material registration failed");
    auto compilable = reorderDocument.Asset();
    for (auto& emitter : compilable.emitters)
        emitter.output.material = {.assetId = 80U,
                                   .virtualPath = "/Game/Materials/PreviewParticle.21kb"};
    const kb::assets::AssetMetadata owner{.id = kb::assets::AssetId{79U},
        .type = kb::scene::kParticleEffectAssetType,
        .virtualPath = "/Game/Effects/AuthoredOrder.kbvfx", .contentHash = 1U};
    const kb::particle_plugin::ParticleCompileResult compiled =
        kb::particle_plugin::ParticleEffectCompiler::Compile(compilable, owner, registry);
    Require(compiled.Succeeded() && compiled.effect->emitterCount == 3U &&
            compiled.effect->emitters[0].emitterId == 13U &&
            compiled.effect->emitters[1].emitterId == 11U &&
            compiled.effect->emitters[2].emitterId == 12U,
        "shared compiler ignored persisted authoring order");

    Require(ParticleEditorCommands::RemoveEmitter(reorderDocument, reorderWorkspace, 11U).Succeeded() &&
            reorderDocument.Asset().emitters.size() == 2U &&
            reorderDocument.Asset().emitters[0].emitterId == 12U &&
            reorderDocument.Asset().emitters[1].emitterId == 13U,
        "remove changed stable-id storage order or left the emitter present");
    const auto rows = ParticleEmitterListModel::Build(
        reorderDocument.Asset(), reorderWorkspace.SelectedEmitterId());
    Require(rows.size() == 2U && rows[0].authoringOrder == 0U && rows[1].authoringOrder == 1U,
        "emitter list model did not expose contiguous authored order");
}

void TestModuleStackCommandsCapabilitiesAndAuthoredOrder() {
    using namespace kb::particle_editor;
    ParticleEditorDocument singleEmitterDocument;
    ParticleEditorWorkspaceState singleEmitterWorkspace;
    Require(singleEmitterDocument.Create(MakeEffect()).Succeeded(),
        "single-emitter module fixture creation failed");
    singleEmitterWorkspace.Synchronize(singleEmitterDocument.Asset());
    const auto unavailableSingleEmitterSub = ParticleEditorCommands::AddModule(
        singleEmitterDocument, singleEmitterWorkspace, 11U,
        kb::scene::ParticleModuleType::SubEmitter, 0U);
    Require(unavailableSingleEmitterSub.status == ParticleEditorStatus::InvalidSelection &&
            !singleEmitterDocument.CanUndo() && singleEmitterDocument.Asset().emitters[0].modules.empty(),
        "single-emitter effect exposed an invalid Sub Emitter add or mutated history");

    auto allTypesEffect = MakeEffect();
    auto target = allTypesEffect.emitters[0];
    target.emitterId = 12U;
    target.authoringOrder = 1U;
    target.name = "Sub Emitter Target";
    allTypesEffect.emitters.push_back(std::move(target));
    ParticleEditorDocument allTypesDocument;
    ParticleEditorWorkspaceState allTypesWorkspace;
    Require(allTypesDocument.Create(allTypesEffect).Succeeded(), "all-module-types fixture creation failed");
    allTypesWorkspace.Synchronize(allTypesDocument.Asset());
    const auto cancelledSubEmitter = ParticleEditorCommands::AddModule(allTypesDocument, allTypesWorkspace, 11U,
        kb::scene::ParticleModuleType::SubEmitter, 0U);
    Require(cancelledSubEmitter.status == ParticleEditorStatus::InvalidSelection &&
            !allTypesDocument.CanUndo() && allTypesDocument.Asset().emitters[0].modules.empty(),
        "cancelled Sub Emitter target selection mutated document history");
    for (std::uint8_t raw = 0U; raw <= static_cast<std::uint8_t>(kb::scene::ParticleModuleType::SubEmitter); ++raw) {
        const auto type = static_cast<kb::scene::ParticleModuleType>(raw);
        const auto added = ParticleEditorCommands::AddModule(allTypesDocument, allTypesWorkspace, 11U, type,
            type == kb::scene::ParticleModuleType::SubEmitter ? 12U : 0U);
        Require(added.Succeeded(), "one of the nine executable module types could not be added validly");
    }
    const auto& allModules = allTypesDocument.Asset().emitters[0].modules;
    const auto color = std::find_if(allModules.begin(), allModules.end(), [](const auto& module) {
        return module.type == kb::scene::ParticleModuleType::ColorOverLife;
    });
    const auto subEmitter = std::find_if(allModules.begin(), allModules.end(), [](const auto& module) {
        return module.type == kb::scene::ParticleModuleType::SubEmitter;
    });
    Require(allModules.size() == 9U && color != allModules.end() &&
            std::get<kb::scene::ParticleColorOverLifeModule>(color->payload).gradient.stops.size() == 2U &&
            subEmitter != allModules.end() &&
            std::get<kb::scene::ParticleSubEmitterModule>(subEmitter->payload).targetEmitterId == 12U &&
            kb::scene::ParticleEffectAssetValidator::ValidateStructure(allTypesDocument.Asset()).Succeeded(),
        "all-nine module add did not produce bounded valid defaults and an explicit Sub Emitter target");
    const auto sizeModule = std::find_if(allModules.begin(), allModules.end(), [](const auto& module) {
        return module.type == kb::scene::ParticleModuleType::SizeOverLife;
    });
    Require(sizeModule != allModules.end(), "size over life module was not added by the all-nine-module loop");
    const kb::scene::ParticleStableId colorModuleId = color->moduleId;
    const kb::scene::ParticleStableId sizeModuleId = sizeModule->moduleId;

    const auto colorInspector =
        ParticleEmitterInspectorModel::Build(allTypesDocument.Asset(), 11U, colorModuleId, nullptr, nullptr);
    const auto gradientRow = std::find_if(colorInspector.properties.begin(), colorInspector.properties.end(),
        [](const auto& row) { return row.label == "Gradient"; });
    Require(gradientRow != colorInspector.properties.end() && gradientRow->editable &&
            gradientRow->value == "0,1,1,1,1;1,1,1,1,1",
        "ColorOverLife gradient property row was not editable or did not encode the default gradient");
    const auto sizeInspector =
        ParticleEmitterInspectorModel::Build(allTypesDocument.Asset(), 11U, sizeModuleId, nullptr, nullptr);
    const auto curveRow = std::find_if(sizeInspector.properties.begin(), sizeInspector.properties.end(),
        [](const auto& row) { return row.label == "Curve"; });
    Require(curveRow != sizeInspector.properties.end() && curveRow->editable && curveRow->value == "0,1,0",
        "SizeOverLife curve property row was not editable or did not encode the default curve");
    const auto rateInspector = ParticleEmitterInspectorModel::Build(allTypesDocument.Asset(), 11U, 0U, nullptr, nullptr);
    const auto rateRow = std::find_if(rateInspector.properties.begin(), rateInspector.properties.end(),
        [](const auto& row) { return row.label == "Rate curve"; });
    Require(rateRow != rateInspector.properties.end() && rateRow->editable && rateRow->value == "0,60,0",
        "spawn rate curve property row was not editable or did not encode the authored curve");

    auto editedGradient = std::get<kb::scene::ParticleColorOverLifeModule>(color->payload);
    editedGradient.gradient.stops.insert(editedGradient.gradient.stops.begin() + 1,
        kb::math::GradientStop{.time = 0.5F, .color = {1.0F, 0.5F, 0.0F, 1.0F}});
    Require(ParticleEditorCommands::SetModulePayload(
                allTypesDocument, allTypesWorkspace, 11U, colorModuleId, editedGradient)
                .Succeeded(),
        "gradient stop insertion via SetModulePayload was rejected");
    const auto& colorAfterEdit = allTypesDocument.Asset().emitters[0].modules;
    const auto colorEdited = std::find_if(colorAfterEdit.begin(), colorAfterEdit.end(),
        [colorModuleId](const auto& module) { return module.moduleId == colorModuleId; });
    Require(colorEdited != colorAfterEdit.end() &&
            std::get<kb::scene::ParticleColorOverLifeModule>(colorEdited->payload).gradient.stops.size() == 3U,
        "gradient stop insertion did not persist a genuine three-stop edit");

    auto editedCurve = std::get<kb::scene::ParticleSizeOverLifeModule>(sizeModule->payload);
    editedCurve.curve.keyframes.push_back(kb::math::CurveKeyframe{
        .time = 1.0F, .value = 2.0F, .easing = kb::math::Easing::OutQuad});
    Require(ParticleEditorCommands::SetModulePayload(
                allTypesDocument, allTypesWorkspace, 11U, sizeModuleId, editedCurve)
                .Succeeded(),
        "curve keyframe insertion via SetModulePayload was rejected");
    const auto& sizeAfterEdit = allTypesDocument.Asset().emitters[0].modules;
    const auto sizeEdited = std::find_if(sizeAfterEdit.begin(), sizeAfterEdit.end(),
        [sizeModuleId](const auto& module) { return module.moduleId == sizeModuleId; });
    Require(sizeEdited != sizeAfterEdit.end() &&
            std::get<kb::scene::ParticleSizeOverLifeModule>(sizeEdited->payload).curve.keyframes.size() == 2U,
        "curve keyframe insertion did not persist a genuine two-key edit");

    auto editedSpawn = allTypesDocument.Asset().emitters[0].spawn;
    editedSpawn.rateOverTime.keyframes.push_back(
        kb::math::CurveKeyframe{.time = 1.0F, .value = 120.0F, .easing = kb::math::Easing::Linear});
    Require(ParticleEditorCommands::SetEmitterSpawn(allTypesDocument, allTypesWorkspace, 11U, editedSpawn).Succeeded() &&
            allTypesDocument.Asset().emitters[0].spawn.rateOverTime.keyframes.size() == 2U,
        "spawn rate curve keyframe insertion via SetEmitterSpawn did not persist a genuine two-key edit");

    ParticleEditorDocument document;
    ParticleEditorWorkspaceState workspace;
    Require(document.Create(MakeEffect()).Succeeded(), "module command fixture creation failed");
    workspace.Synchronize(document.Asset());
    Require(ParticleEditorCommands::AddModule(document, workspace, 11U,
                kb::scene::ParticleModuleType::Gravity).Succeeded() &&
            ParticleEditorCommands::AddModule(document, workspace, 11U,
                kb::scene::ParticleModuleType::Wind).Succeeded() &&
            ParticleEditorCommands::AddModule(document, workspace, 11U,
                kb::scene::ParticleModuleType::Drag).Succeeded(),
        "typed module add command failed");
    Require(document.Asset().emitters[0].modules.size() == 3U &&
            document.Asset().emitters[0].modules[0].moduleId == 1U &&
            document.Asset().emitters[0].modules[2].authoringOrder == 2U,
        "module add did not preserve stable-id storage and contiguous authoring order");
    const auto duplicate = ParticleEditorCommands::AddModule(document, workspace, 11U,
        kb::scene::ParticleModuleType::Gravity);
    Require(duplicate.status == ParticleEditorStatus::InvalidAsset,
        "singleton module duplicate was accepted");
    Require(ParticleEditorCommands::ReorderModule(document, workspace, 11U, 3U, 0U).Succeeded() &&
            document.CanUndo() && document.Asset().emitters[0].modules[2].authoringOrder == 0U,
        "module drag reorder did not create one authored-order command");
    Require(document.Undo() && document.Asset().emitters[0].modules[2].authoringOrder == 2U &&
            document.Redo(), "module reorder did not undo and redo as one command");
    Require(workspace.SelectModule(document.Asset(), 11U, 2U), "module stable-id selection failed");

    kb::assets::AssetRegistry registry;
    Require(registry.Upsert({.id = kb::assets::AssetId{72U}, .type = "RenderMaterial",
                .virtualPath = "/Game/Materials/PreviewParticle.21kb", .contentHash = 1U}),
        "module compiler material registration failed");
    auto compilable = document.Asset();
    compilable.emitters[0].output.material = {.assetId = 72U,
        .virtualPath = "/Game/Materials/PreviewParticle.21kb"};
    const kb::assets::AssetMetadata owner{.id = kb::assets::AssetId{71U},
        .type = kb::scene::kParticleEffectAssetType, .virtualPath = "/Game/Effects/Modules.kbvfx"};
    const auto compiled = kb::particle_plugin::ParticleEffectCompiler::Compile(compilable, owner, registry);
    Require(compiled.Succeeded() && compiled.effect->emitters[0].modules[0].moduleId == 3U &&
            compiled.effect->emitters[0].modules[1].moduleId == 1U &&
            compiled.effect->emitters[0].modules[2].moduleId == 2U,
        "compiler did not execute modules in persisted authoring order");

    auto unsupported = compilable;
    unsupported.emitters[0].output.type = kb::scene::ParticleOutputType::Mesh;
    unsupported.emitters[0].output.payload = kb::scene::ParticleMeshOutput{};
    unsupported.emitters[0].output.mesh = {.assetId = 100U};
    unsupported.backendPolicy = kb::scene::ParticleBackendPolicy::GpuVisualRequired;
    unsupported.eventBindings.push_back({.sourceEmitterId = 11U,
        .action = kb::scene::ParticleEventAction::EmitEffectAsset,
        .targetEffect = {.assetId = 200U}});
    const auto capabilityDiagnostics =
        kb::particle_plugin::ParticleEffectCompiler::ValidateCapabilities(unsupported);
    Require(capabilityDiagnostics.size() == 3U &&
            std::all_of(capabilityDiagnostics.begin(), capabilityDiagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability;
            }), "public capability validator did not aggregate GPU policy, external event, and output failures");
    const auto inspector = ParticleEmitterInspectorModel::Build(unsupported, 11U, 2U, nullptr, nullptr);
    Require(inspector.outputChoices.size() == 8U && !inspector.outputChoices[3].enabled &&
            inspector.outputChoices[3].diagnostics.size() == 1U &&
            inspector.modules.size() == 3U && inspector.modules[2].selected &&
            unsupported.emitters[0].output.type == kb::scene::ParticleOutputType::Mesh,
        "inspector did not expose disabled capability diagnostics or preserve authored unsupported data");
    Require(ParticleEditorCommands::RemoveModule(document, workspace, 11U, 2U).Succeeded() &&
            document.Asset().emitters[0].modules.size() == 2U &&
            document.Asset().emitters[0].modules[0].authoringOrder == 1U &&
            document.Asset().emitters[0].modules[1].authoringOrder == 0U,
        "module removal did not preserve stable storage and close authored-order permutation");
}

void TestCloseGuardAllDirtyTransitions() {
    kb::particle_editor::ParticleAssetGateway gateway;
    kb::particle_editor::ParticleEditorDocument document;
    Require(document.Create(MakeEffect()).Succeeded(), "close-guard document creation failed");
    kb::particle_editor::ParticleDocumentCloseGuard guard;
    for (const auto transition : {
             kb::particle_editor::ParticleDocumentTransition::Open,
             kb::particle_editor::ParticleDocumentTransition::Revert,
             kb::particle_editor::ParticleDocumentTransition::CloseTab,
             kb::particle_editor::ParticleDocumentTransition::CloseWindow,
             kb::particle_editor::ParticleDocumentTransition::CloseProject,
             kb::particle_editor::ParticleDocumentTransition::ExitApplication}) {
        Require(guard.Request(document, transition).state ==
                kb::particle_editor::ParticleDocumentCloseState::DecisionRequired,
            "dirty transition bypassed the document close guard");
        Require(guard.Resolve(kb::particle_editor::ParticleDocumentCloseDecision::Cancel,
                    document, gateway).state == kb::particle_editor::ParticleDocumentCloseState::Cancelled,
            "cancel did not block a dirty particle transition");
    }
    Require(guard.Request(document, kb::particle_editor::ParticleDocumentTransition::CloseTab).state ==
            kb::particle_editor::ParticleDocumentCloseState::DecisionRequired,
        "dirty close did not request a decision");
    const auto blocked = guard.Resolve(kb::particle_editor::ParticleDocumentCloseDecision::Save,
        document, gateway);
    Require(blocked.state == kb::particle_editor::ParticleDocumentCloseState::Blocked &&
            blocked.saveResult.status == kb::particle_editor::ParticleEditorStatus::PathRequired &&
            guard.PendingTransition().has_value(),
        "failed close-time save did not preserve the pending transition");
    Require(guard.Resolve(kb::particle_editor::ParticleDocumentCloseDecision::Discard,
                document, gateway).state == kb::particle_editor::ParticleDocumentCloseState::Proceed,
        "explicit discard did not release the pending transition");
}

void TestProductionBakeCacheAndCapabilityGates() {
    using namespace kb::particle_editor;
    const std::filesystem::path root = TestRoot() / "bake";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "bake fixture directory creation failed");

    kb::assets::AssetRegistry registry;
    Require(registry.Upsert({.id = kb::assets::AssetId{72U}, .type = "RenderMaterial",
                .virtualPath = "/Game/Materials/PreviewParticle.21kb", .physicalPath = root / "Material.21kb",
                .contentHash = 10U}) &&
            registry.Upsert({.id = kb::assets::AssetId{73U}, .type = "RenderMesh",
                .virtualPath = "/Game/Meshes/Particle.kbmesh", .physicalPath = root / "Mesh.kbmesh",
                .contentHash = 20U}),
        "bake dependency registration failed");
    const std::filesystem::path sourcePath = root / "Working.kbvfx";
    {
        std::ofstream source{sourcePath, std::ios::binary};
        source << "source bytes must not be changed by Bake";
    }
    const std::string originalSource = ReadBytes(sourcePath);
    const kb::assets::AssetMetadata owner{.id = kb::assets::AssetId{71U},
        .type = kb::scene::kParticleEffectAssetType, .virtualPath = "/Game/Effects/Working.kbvfx",
        .physicalPath = sourcePath, .contentHash = 30U};
    auto effect = MakeEffect();
    effect.emitters[0].spawn.bursts.push_back({.timeSeconds = 0.25F, .count = 3U});
    effect.emitters[0].modules.push_back({.moduleId = 1U, .authoringOrder = 0U, .type = kb::scene::ParticleModuleType::Gravity,
        .payload = kb::scene::ParticleGravityModule{.acceleration = {0.0F, -2.0F, 0.0F}}});
    kb::math::Gradient colorGradient;
    colorGradient.stops = {{.time = 0.0F, .color = {1.0F, 0.0F, 0.0F, 1.0F}},
                           {.time = 1.0F, .color = {0.0F, 0.0F, 1.0F, 0.5F}}};
    effect.emitters[0].modules.push_back({.moduleId = 2U, .authoringOrder = 1U, .type = kb::scene::ParticleModuleType::ColorOverLife,
        .payload = kb::scene::ParticleColorOverLifeModule{.gradient = std::move(colorGradient)}});
    const std::filesystem::path cacheRoot = root / "Saved" / "21kbParticleCache";

    const auto bake = [&](const kb::scene::ParticleEffectAsset& working) {
        return ParticleBakeService::Bake({.workingAsset = working, .owner = owner,
            .registry = registry, .cacheRoot = cacheRoot});
    };
    const ParticleBakeResult first = bake(effect);
    Require(first.Succeeded() && first.status == ParticleBakeStatus::Baked && first.effect != nullptr &&
            std::filesystem::is_regular_file(first.cachePath) && ReadBytes(sourcePath) == originalSource,
        "first production Bake failed or modified the source asset");
    const ParticleBakeResult hit = bake(effect);
    Require(hit.status == ParticleBakeStatus::UpToDate && hit.key == first.key &&
            hit.effect->emitters[0].materialAssetId == 72U && hit.effect->emitters[0].burstCount == 1U &&
            hit.effect->emitters[0].moduleCount == 2U && hit.effect->emitters[0].colorOverLife.stopCount == 2U &&
            std::get<kb::scene::ParticleGravityModule>(hit.effect->emitters[0].modules[0].payload).acceleration.y == -2.0F,
        "canonical Bake cache did not return a validated immutable artifact hit");
    const ParticleBakeResult alternatePlatform = ParticleBakeService::Bake({.workingAsset = effect, .owner = owner,
        .registry = registry, .cacheRoot = cacheRoot,
        .compile = {.platform = kb::particles::ParticleCompilePlatform::WindowsVulkan}});
    Require(alternatePlatform.status == ParticleBakeStatus::Baked &&
            alternatePlatform.key.platform != first.key.platform && alternatePlatform.cachePath != first.cachePath,
        "typed compiler platform was omitted from the Bake cache key");
    kb::particle_plugin::ParticleCompilerCapabilities reducedCapabilities;
    reducedCapabilities.pointSprite = false;
    const ParticleBakeResult alternateCapabilities = ParticleBakeService::Bake({.workingAsset = effect, .owner = owner,
        .registry = registry, .cacheRoot = cacheRoot,
        .compile = {.capabilities = reducedCapabilities}});
    Require(alternateCapabilities.status == ParticleBakeStatus::Baked &&
            alternateCapabilities.key.capabilityKey != first.key.capabilityKey &&
            alternateCapabilities.cachePath != first.cachePath,
        "typed compiler capability set was omitted from the Bake cache key");

    auto edited = effect;
    edited.durationSeconds = 7.0F;
    const ParticleBakeResult sourceChanged = bake(edited);
    Require(sourceChanged.status == ParticleBakeStatus::Baked && sourceChanged.key.sourceHash != first.key.sourceHash &&
            sourceChanged.cachePath != first.cachePath,
        "unsaved canonical source change did not invalidate the Bake cache key");
    Require(registry.Upsert({.id = kb::assets::AssetId{999U}, .type = "RenderTexture",
                .virtualPath = "/Game/Unrelated.kbtex", .contentHash = 1U}),
        "unrelated bake registry mutation failed");
    Require(bake(effect).status == ParticleBakeStatus::UpToDate,
        "unrelated registry metadata invalidated the particle Bake cache");
    Require(registry.Upsert({.id = kb::assets::AssetId{72U}, .type = "RenderMaterial",
                .virtualPath = "/Game/Materials/PreviewParticle.21kb", .physicalPath = root / "Material.21kb",
                .contentHash = 11U}),
        "dependency content mutation failed");
    const ParticleBakeResult dependencyChanged = bake(effect);
    Require(dependencyChanged.status == ParticleBakeStatus::Baked &&
            dependencyChanged.key.dependencyHash != first.key.dependencyHash,
        "transitive dependency content change did not invalidate the Bake cache key");

    {
        std::ofstream corrupt{dependencyChanged.cachePath, std::ios::binary | std::ios::trunc};
        corrupt << "corrupt";
    }
    const std::string corruptBytes = ReadBytes(dependencyChanged.cachePath);
    std::filesystem::create_directory(dependencyChanged.cachePath.string() + ".tmp", error);
    Require(!error, "atomic Bake failure fixture could not reserve temporary path");
    const ParticleBakeResult blockedRebuild = bake(effect);
    Require(blockedRebuild.status == ParticleBakeStatus::CacheWriteFailed &&
            ReadBytes(dependencyChanged.cachePath) == corruptBytes &&
            std::filesystem::is_directory(dependencyChanged.cachePath.string() + ".tmp"),
        "failed atomic compiled-cache rebuild changed its existing destination or prepared conflict");
    std::filesystem::remove(dependencyChanged.cachePath.string() + ".tmp", error);
    Require(!error, "atomic Bake failure fixture cleanup failed");
    const ParticleBakeResult rebuilt = bake(effect);
    Require(rebuilt.status == ParticleBakeStatus::Baked && rebuilt.effect != nullptr &&
            kb::particles::ParticleCompiledEffectCache::Load(rebuilt.cachePath, rebuilt.key).Succeeded(),
        "corrupt compiled cache was accepted or not atomically rebuilt");
    {
        std::ofstream oversized{rebuilt.cachePath, std::ios::binary | std::ios::trunc};
        oversized.seekp(static_cast<std::streamoff>(kb::particles::kParticleCompiledEffectCacheMaxBytes));
        oversized.put('x');
    }
    Require(bake(effect).status == ParticleBakeStatus::Baked,
        "oversized compiled cache was read unboundedly or not rebuilt");
    {
        std::fstream future{rebuilt.cachePath, std::ios::binary | std::ios::in | std::ios::out};
        const std::array<char, 8U> versionTwo{2, 0, 0, 0, 0, 0, 0, 0};
        future.seekp(8, std::ios::beg);
        future.write(versionTwo.data(), static_cast<std::streamsize>(versionTwo.size()));
    }
    Require(bake(effect).status == ParticleBakeStatus::Baked,
        "future compiled cache format was accepted instead of atomically rebuilt");

    for (const kb::scene::ParticleOutputType unsupported : {kb::scene::ParticleOutputType::Mesh,
             kb::scene::ParticleOutputType::Trail, kb::scene::ParticleOutputType::Ribbon,
             kb::scene::ParticleOutputType::Beam, kb::scene::ParticleOutputType::Volumetric}) {
        auto candidate = effect;
        candidate.emitters[0].output.type = unsupported;
        candidate.emitters[0].output.payload = kb::scene::DefaultParticleOutputPayload(unsupported);
        candidate.emitters[0].output.mesh = unsupported == kb::scene::ParticleOutputType::Mesh
            ? kb::scene::ParticleAssetReference{.assetId = 73U} : kb::scene::ParticleAssetReference{};
        const ParticleBakeResult rejected = bake(candidate);
        Require(rejected.status == ParticleBakeStatus::UnsupportedCapability && !rejected.diagnostics.empty() &&
                rejected.diagnostics.front().code == kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability,
            "unsupported output was silently downgraded by Bake");
    }
    auto gpuRequired = effect;
    gpuRequired.backendPolicy = kb::scene::ParticleBackendPolicy::GpuVisualRequired;
    Require(bake(gpuRequired).status == ParticleBakeStatus::UnsupportedCapability,
        "GPU-required policy was silently downgraded by the current compiler");

    auto child = effect;
    child.effectId = 74U;
    const std::filesystem::path childPath = root / "Child.kbvfx";
    Require(kb::scene::ParticleEffectAssetIO::Save(childPath, child) &&
            registry.Upsert({.id = kb::assets::AssetId{74U}, .type = kb::scene::kParticleEffectAssetType,
                .virtualPath = "/Game/Effects/Child.kbvfx", .physicalPath = childPath, .contentHash = 1U}),
        "external effect gate fixture setup failed");
    auto external = effect;
    external.eventBindings.push_back({.sourceEmitterId = 11U,
        .trigger = kb::scene::ParticleEventTrigger::Death,
        .action = kb::scene::ParticleEventAction::EmitEffectAsset,
        .targetEffect = {.assetId = 74U}, .count = 1U, .maxDepth = 1U, .perStepBudget = 1U});
    Require(bake(external).status == ParticleBakeStatus::UnsupportedCapability,
        "external particle effect event was silently accepted by Bake");
    Require(ReadBytes(sourcePath) == originalSource, "Bake changed source bytes while exercising failure paths");
}

class HeadlessSurface final : public kb::render::RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 32U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 32U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

void TestIsolatedRuntimePreviewAndGpuRelease() {
    const std::filesystem::path pluginPath = KB_21KB_PARTICLE_PLUGIN_PATH;
    Require(!pluginPath.empty() && std::filesystem::is_regular_file(pluginPath),
        "focused editor preview test requires the produced provider module");
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({
        .name = "Rendering.21kbParticle",
        .binaryPath = pluginPath.string(),
        .enabled = true,
    });

    kb::assets::AssetRegistry sourceRegistry;
    Require(sourceRegistry.Upsert({
                .id = kb::assets::AssetId{72U},
                .type = "RenderMaterial",
                .name = "Preview Particle Material",
                .virtualPath = "/Game/Materials/PreviewParticle.21kb",
                .physicalPath = "PreviewParticle.21kb",
                .contentHash = 1U,
            }),
        "preview dependency metadata registration failed");

    kb::particle_editor::ParticlePreviewSession preview;
    const auto effect = MakeEffect();
    Require(preview.Start(project, sourceRegistry, kb::assets::AssetId{71U},
                "/Game/Effects/UnsavedPreview.kbvfx", effect).Succeeded(),
        "isolated particle preview session did not start through the real provider");
    Require(preview.Active() && preview.PreviewScene().Mode() == kb::scene::SceneMode::Runtime &&
            preview.PreviewScene().IsModuleActive("Rendering.21kbParticle") &&
            kb::particles::ParticlePlayback::HasBackend(preview.PreviewScene()) &&
            preview.PreviewScene().Components().ParticleEffects().Has(preview.EffectEntity()) &&
            preview.PreviewScene().Components().Cameras().Has(preview.CameraEntity()),
        "preview did not own the required runtime scene, provider, component, and camera");

    for (int frame = 0; frame < 4; ++frame) {
        Require(preview.Tick(1.0F / 60.0F).Succeeded(), "preview SceneRuntime update failed");
    }
    const auto instances = kb::particles::ParticlePlayback::LiveInstanceIds(preview.PreviewScene());
    Require(instances.size() == 1U &&
            kb::particles::ParticlePlayback::Query(preview.PreviewScene(), instances.front()).liveParticleCount > 0U,
        "preview did not use the accepted runtime simulation backend");
    auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(preview.PreviewScene());
    Require(snapshot != nullptr && !snapshot->IsTombstone() && !snapshot->Particles().empty(),
        "preview runtime did not publish a GPU-consumable particle snapshot");
    std::weak_ptr<const kb::particles::ParticleRenderSnapshot> snapshotLifetime = snapshot;
    snapshot.reset();

    auto workingCopy = effect;
    workingCopy.displayName = "Unsaved Runtime Working Copy";
    workingCopy.emitters.front().spawn.rateOverTime.keyframes.front().value = 120.0F;
    Require(preview.PublishWorkingCopy(workingCopy).Succeeded(),
        "unsaved working copy was not published through AssetManager");
    Require(preview.Tick(1.0F / 60.0F).Succeeded(),
        "preview did not reconcile the unsaved runtime publication");
    Require(!std::filesystem::exists(TestRoot() / "UnsavedPreview.kbvfx"),
        "publishing an unsaved preview working copy touched disk");

    HeadlessSurface surface;
    kb::render::DisplayConfig display{};
    display.allowHeadlessNoop = true;
    display.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    kb::render::Renderer renderer;
    Require(renderer.Initialize(surface, &display),
        "accepted GPU renderer did not initialize for particle preview");
    Require(renderer.BeginFrame() && preview.Submit(renderer),
        "particle preview did not submit through the accepted GPU renderer");
    renderer.EndFrame();
    Require(renderer.RuntimeResourceStats().renderSceneCount == 1U,
        "GPU renderer did not retain exactly one isolated preview scene");
    preview.Release(renderer);
    Require(!preview.Active() && renderer.RuntimeResourceStats().renderSceneCount == 0U &&
            snapshotLifetime.expired(),
        "preview release leaked its runtime scene, retained snapshot, or renderer-owned scene cache");
    renderer.Shutdown();
}

} // namespace

int main() {
    try {
        TestDocumentHistorySavePointAndAtomicFailure();
        TestEmitterListCommandsAndAuthoredCompileOrder();
        TestModuleStackCommandsCapabilitiesAndAuthoredOrder();
        TestCloseGuardAllDirtyTransitions();
        TestProductionBakeCacheAndCapabilityGates();
        TestIsolatedRuntimePreviewAndGpuRelease();
        std::cout << "21kb Particle System editor core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
