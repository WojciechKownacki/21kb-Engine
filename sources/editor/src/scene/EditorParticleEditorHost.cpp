#include "scene/EditorSceneContext.hpp"
#include "scene/ParticleEditorBakeHostCommand.hpp"

#include "editor/ParticleEditorCommands.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "project/EditorProjectPaths.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> ResolveParticleAssetPath(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetManager& manager) {
    if (const auto mounted = manager.Mounts().Resolve(metadata.virtualPath); mounted.has_value()) {
        return mounted;
    }
    return metadata.physicalPath.empty()
        ? std::nullopt
        : std::optional<std::filesystem::path>{metadata.physicalPath};
}

void LogParticleResult(
    EditorConsoleState& console,
    const kb::particle_editor::ParticleEditorResult& result) {
    if (result.diagnostics.empty()) {
        console.Error("Particles", result.message.empty()
            ? "Particle editor command failed without a diagnostic." : result.message);
        return;
    }
    for (const kb::scene::ParticleEffectDiagnostic& diagnostic : result.diagnostics)
        console.Error("Particles", kb::scene::FormatParticleEffectDiagnostic(diagnostic));
}

} // namespace

bool EditorSceneContext::OpenParticleEditorAsset(kb::assets::AssetId id) {
    if (!id.IsValid()) return false;
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != kb::scene::kParticleEffectAssetType) {
        console_.Error("Particles", "21kb Particle System can only open ParticleEffect assets.");
        return false;
    }
    const auto path = ResolveParticleAssetPath(*metadata, manager);
    if (!path.has_value()) {
        console_.Error("Particles", "Particle effect path could not be resolved: " + metadata->virtualPath.generic_string());
        return false;
    }

    kb::particle_editor::ParticleEditorDocument candidateDocument;
    const auto opened = candidateDocument.Open(particleEditorGateway_, *path);
    if (!opened.Succeeded()) {
        console_.Error("Particles", opened.message);
        return false;
    }
    auto candidatePreview = std::make_unique<kb::particle_editor::ParticlePreviewSession>();
    const auto started = candidatePreview->Start(
        project_, manager.Registry(), id, metadata->virtualPath, candidateDocument.Asset());
    if (!started.Succeeded()) {
        console_.Error("Particles", started.message);
        return false;
    }

    if (particlePreviewSession_ != nullptr) {
        particlePreviewSession_->Release(particlePreviewReleaseHandler_);
    }
    particleEditorDocument_ = std::move(candidateDocument);
    particleEditorWorkspace_ = {};
    particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
    particlePreviewSession_ = std::move(candidatePreview);
    particleEditorAssetId_ = id;
    console_.Info("Particles", "Opened 21kb Particle System document: " + metadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::HasParticleEditorAsset() const noexcept {
    return particleEditorAssetId_.IsValid() && particleEditorDocument_.HasDocument() &&
        particlePreviewSession_ != nullptr && particlePreviewSession_->Active();
}

kb::assets::AssetId EditorSceneContext::ParticleEditorAssetId() const noexcept { return particleEditorAssetId_; }
bool EditorSceneContext::ParticleEditorDirty() const noexcept { return particleEditorDocument_.Dirty(); }
const std::optional<std::filesystem::path>& EditorSceneContext::ParticleEditorSessionPath() const noexcept {
    return particleEditorDocument_.SessionPath();
}
const kb::scene::Scene* EditorSceneContext::ParticleEditorPreviewScene() const noexcept {
    return particlePreviewSession_ != nullptr && particlePreviewSession_->Active()
        ? &particlePreviewSession_->PreviewScene()
        : nullptr;
}
std::uint64_t EditorSceneContext::ParticleEditorPreviewRevision() const noexcept {
    const kb::scene::Scene* preview = ParticleEditorPreviewScene();
    const auto snapshot = preview == nullptr ? nullptr : kb::particles::ParticlePlayback::ReadRenderSnapshot(*preview);
    return snapshot == nullptr ? 0U : snapshot->Revision();
}

bool EditorSceneContext::TickParticleEditorPreview(float deltaSeconds) {
    if (!HasParticleEditorAsset()) return false;
    const auto result = particlePreviewSession_->Tick(deltaSeconds);
    if (!result.Succeeded()) {
        console_.Error("Particles", result.message);
        return false;
    }
    return true;
}

bool EditorSceneContext::SaveParticleEditorAsset() {
    if (!HasParticleEditorAsset()) return false;
    const auto result = particleEditorDocument_.Save(particleEditorGateway_);
    if (!result.Succeeded()) {
        console_.Error("Particles", result.message);
        return false;
    }
    return true;
}

kb::particle_editor::ParticleBakeResult EditorSceneContext::BakeParticleEditorAsset() {
    if (!HasParticleEditorAsset()) {
        kb::particle_editor::ParticleBakeResult result;
        result.diagnostics.push_back({.code = kb::scene::ParticleEffectDiagnosticCode::InvalidReference,
            .propertyPath = "effect", .message = "no particle editor document is open"});
        console_.Error("Particles", kb::scene::FormatParticleEffectDiagnostic(result.diagnostics.front()));
        return result;
    }
    const kb::assets::AssetRegistry& registry = scene_->Assets().Manager().Registry();
    const kb::assets::AssetMetadata* metadata = registry.Find(particleEditorAssetId_);
    if (metadata == nullptr) {
        kb::particle_editor::ParticleBakeResult result;
        result.diagnostics.push_back({.code = kb::scene::ParticleEffectDiagnosticCode::InvalidReference,
            .propertyPath = "effect", .message = "open particle document metadata is no longer registered"});
        console_.Error("Particles", kb::scene::FormatParticleEffectDiagnostic(result.diagnostics.front()));
        return result;
    }
    return ParticleEditorBakeHostCommand::Execute(particleEditorDocument_.Asset(), *metadata, registry,
                                                   EditorProjectPaths::ProjectRoot(), console_);
}

bool EditorSceneContext::RevertParticleEditorAsset() {
    if (!HasParticleEditorAsset() || !particleEditorDocument_.Revert()) return false;
    particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
    const auto published = particlePreviewSession_->PublishWorkingCopy(particleEditorDocument_.Asset());
    if (!published.Succeeded()) {
        console_.Error("Particles", published.message);
        return false;
    }
    return true;
}

bool EditorSceneContext::ApplyParticleEditorWorkingCopy(kb::scene::ParticleEffectAsset asset) {
    if (!HasParticleEditorAsset()) return false;
    const auto applied = particleEditorDocument_.Apply(std::move(asset));
    if (!applied.Succeeded()) {
        console_.Error("Particles", applied.message);
        return false;
    }
    if (applied.status == kb::particle_editor::ParticleEditorStatus::NoChange) return true;
    particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
    const auto published = particlePreviewSession_->PublishWorkingCopy(particleEditorDocument_.Asset());
    if (!published.Succeeded()) {
        static_cast<void>(particleEditorDocument_.Undo());
        console_.Error("Particles", published.message);
        return false;
    }
    return true;
}

const kb::scene::ParticleEffectAsset* EditorSceneContext::ParticleEditorWorkingAsset() const noexcept {
    return HasParticleEditorAsset() ? &particleEditorDocument_.Asset() : nullptr;
}

std::vector<kb::particle_editor::ParticleEmitterListRow> EditorSceneContext::ParticleEditorEmitterRows() const {
    const kb::scene::ParticleEffectAsset* asset = ParticleEditorWorkingAsset();
    return asset == nullptr ? std::vector<kb::particle_editor::ParticleEmitterListRow>{}
                            : kb::particle_editor::ParticleEmitterListModel::Build(
                                  *asset, particleEditorWorkspace_.SelectedEmitterId());
}

const kb::particle_editor::ParticleEditorWorkspaceState& EditorSceneContext::ParticleEditorWorkspace() const noexcept {
    return particleEditorWorkspace_;
}

void EditorSceneContext::SetParticleEditorFocused(bool focused) noexcept {
    particleEditorWorkspace_.SetFocused(focused);
}

bool EditorSceneContext::SelectParticleEditorEmitter(kb::scene::ParticleStableId emitterId) noexcept {
    const kb::scene::ParticleEffectAsset* asset = ParticleEditorWorkingAsset();
    return asset != nullptr && particleEditorWorkspace_.Select(*asset, emitterId);
}

bool EditorSceneContext::FinalizeParticleEditorCommand(kb::particle_editor::ParticleEditorResult result) {
    if (!result.Succeeded()) {
        LogParticleResult(console_, result);
        return false;
    }
    if (result.status == kb::particle_editor::ParticleEditorStatus::NoChange)
        return true;
    const auto published = particlePreviewSession_->PublishWorkingCopy(particleEditorDocument_.Asset());
    if (!published.Succeeded()) {
        static_cast<void>(particleEditorDocument_.Undo());
        particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
        LogParticleResult(console_, published);
        return false;
    }
    return true;
}

bool EditorSceneContext::AddParticleEditorEmitter(kb::assets::AssetId materialId) {
    if (!HasParticleEditorAsset())
        return false;
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(materialId);
    if (metadata == nullptr || !kb::assets::AssetMatchesKind(*metadata, kb::assets::AssetKind::Material)) {
        console_.Error("Particles", "Add Emitter requires a valid material selection.");
        return false;
    }
    return FinalizeParticleEditorCommand(kb::particle_editor::ParticleEditorCommands::AddEmitter(
        particleEditorDocument_, particleEditorWorkspace_,
        {.assetId = materialId.value, .virtualPath = metadata->virtualPath.generic_string()}));
}

bool EditorSceneContext::RenameParticleEditorEmitter(
    kb::scene::ParticleStableId emitterId, std::string name) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::RenameEmitter(
            particleEditorDocument_, particleEditorWorkspace_, emitterId, std::move(name)));
}

bool EditorSceneContext::ToggleParticleEditorEmitter(kb::scene::ParticleStableId emitterId) {
    const kb::scene::ParticleEffectAsset* asset = ParticleEditorWorkingAsset();
    const kb::scene::ParticleEmitterAsset* emitter = asset == nullptr
        ? nullptr : kb::particle_editor::ParticleEmitterListModel::Find(*asset, emitterId);
    return emitter != nullptr && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::SetEmitterEnabled(
            particleEditorDocument_, particleEditorWorkspace_, emitterId, !emitter->enabled));
}

bool EditorSceneContext::MoveParticleEditorEmitter(
    kb::scene::ParticleStableId emitterId, std::uint32_t targetOrder) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::ReorderEmitter(
            particleEditorDocument_, particleEditorWorkspace_, emitterId, targetOrder));
}

bool EditorSceneContext::RemoveParticleEditorEmitter(kb::scene::ParticleStableId emitterId) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::RemoveEmitter(
            particleEditorDocument_, particleEditorWorkspace_, emitterId));
}

bool EditorSceneContext::UndoParticleEditorCommand() {
    if (!HasParticleEditorAsset() || !particleEditorDocument_.Undo())
        return false;
    particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
    const auto published = particlePreviewSession_->PublishWorkingCopy(particleEditorDocument_.Asset());
    if (!published.Succeeded()) {
        static_cast<void>(particleEditorDocument_.Redo());
        particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
        LogParticleResult(console_, published);
        return false;
    }
    return true;
}

bool EditorSceneContext::RedoParticleEditorCommand() {
    if (!HasParticleEditorAsset() || !particleEditorDocument_.Redo())
        return false;
    particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
    const auto published = particlePreviewSession_->PublishWorkingCopy(particleEditorDocument_.Asset());
    if (!published.Succeeded()) {
        static_cast<void>(particleEditorDocument_.Undo());
        particleEditorWorkspace_.Synchronize(particleEditorDocument_.Asset());
        LogParticleResult(console_, published);
        return false;
    }
    return true;
}

bool EditorSceneContext::BeginParticleEditorEmitterRename(kb::scene::ParticleStableId emitterId) {
    const kb::scene::ParticleEffectAsset* asset = ParticleEditorWorkingAsset();
    return asset != nullptr && particleEditorWorkspace_.BeginRename(*asset, emitterId);
}

void EditorSceneContext::AppendParticleEditorRenameText(std::string_view text) {
    particleEditorWorkspace_.AppendRenameText(text);
}

void EditorSceneContext::RemoveParticleEditorRenameCharacter() noexcept {
    particleEditorWorkspace_.RemoveRenameCharacter();
}

void EditorSceneContext::CancelParticleEditorEmitterRename() noexcept {
    particleEditorWorkspace_.CancelRename();
}

bool EditorSceneContext::CommitParticleEditorEmitterRename() {
    if (!particleEditorWorkspace_.RenameActive())
        return false;
    const kb::scene::ParticleStableId emitterId = particleEditorWorkspace_.RenameEmitterId();
    std::string name = particleEditorWorkspace_.RenameText();
    const bool renamed = RenameParticleEditorEmitter(emitterId, std::move(name));
    if (renamed)
        particleEditorWorkspace_.CancelRename();
    return renamed;
}

bool EditorSceneContext::BeginParticleEditorEmitterDrag(kb::scene::ParticleStableId emitterId) noexcept {
    const kb::scene::ParticleEffectAsset* asset = ParticleEditorWorkingAsset();
    return asset != nullptr && particleEditorWorkspace_.BeginEmitterDrag(*asset, emitterId);
}

void EditorSceneContext::UpdateParticleEditorEmitterDrag(std::uint32_t targetOrder) noexcept {
    particleEditorWorkspace_.UpdateEmitterDrag(targetOrder);
}

bool EditorSceneContext::CommitParticleEditorEmitterDrag() {
    if (!particleEditorWorkspace_.EmitterDragActive())
        return false;
    const kb::scene::ParticleStableId emitterId = particleEditorWorkspace_.DraggedEmitterId();
    const std::uint32_t targetOrder = particleEditorWorkspace_.DragTargetOrder();
    particleEditorWorkspace_.EndEmitterDrag();
    return MoveParticleEditorEmitter(emitterId, targetOrder);
}

void EditorSceneContext::CancelParticleEditorEmitterDrag() noexcept {
    particleEditorWorkspace_.EndEmitterDrag();
}

void EditorSceneContext::SetParticleEditorComposerScrollOffset(int offset) noexcept {
    particleEditorWorkspace_.SetComposerScrollOffset(offset);
}

kb::particle_editor::ParticleDocumentCloseResult EditorSceneContext::RequestParticleEditorTransition(
    kb::particle_editor::ParticleDocumentTransition transition) noexcept {
    return particleEditorCloseGuard_.Request(particleEditorDocument_, transition);
}

kb::particle_editor::ParticleDocumentCloseResult EditorSceneContext::ResolveParticleEditorTransition(
    kb::particle_editor::ParticleDocumentCloseDecision decision,
    std::optional<std::filesystem::path> savePath) {
    return particleEditorCloseGuard_.Resolve(
        decision, particleEditorDocument_, particleEditorGateway_, std::move(savePath));
}

void EditorSceneContext::CloseParticleEditorAsset() {
    if (particlePreviewSession_ != nullptr) {
        particlePreviewSession_->Release(particlePreviewReleaseHandler_);
        particlePreviewSession_.reset();
    }
    particleEditorDocument_ = {};
    particleEditorWorkspace_ = {};
    particleEditorCloseGuard_ = {};
    particleEditorAssetId_ = {};
}

void EditorSceneContext::SetParticlePreviewReleaseHandler(
    std::function<void(const kb::scene::Scene&)> handler) {
    if (!handler && particlePreviewSession_ != nullptr) {
        throw std::logic_error{"particle preview release handler cannot be cleared while a session is active"};
    }
    particlePreviewReleaseHandler_ = std::move(handler);
}

} // namespace kb::editor
