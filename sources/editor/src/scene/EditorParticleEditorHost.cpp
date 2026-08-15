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
#include <charconv>
#include <cmath>
#include <sstream>
#include <locale>
#include <type_traits>
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

template <typename T>
[[nodiscard]] bool ParseNumber(std::string_view text, T& value) noexcept {
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end) return false;
    if constexpr (std::is_floating_point_v<T>) {
        return std::isfinite(value);
    } else {
        return true;
    }
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& value) noexcept {
    if (text == "true") { value = true; return true; }
    if (text == "false") { value = false; return true; }
    return false;
}

[[nodiscard]] bool ParseVec3(std::string_view text, kb::math::Vec3& value) {
    std::istringstream input{std::string{text}};
    input.imbue(std::locale::classic());
    input >> value.x >> value.y >> value.z;
    input >> std::ws;
    return input && input.eof() && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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

kb::particle_editor::ParticleEmitterInspectorView EditorSceneContext::ParticleEditorInspector() const {
    const auto* asset = ParticleEditorWorkingAsset();
    if (asset == nullptr) return {};
    const auto& registry = scene_->Assets().Manager().Registry();
    const auto* owner = registry.Find(particleEditorAssetId_);
    return kb::particle_editor::ParticleEmitterInspectorModel::Build(
        *asset, particleEditorWorkspace_.SelectedEmitterId(), particleEditorWorkspace_.SelectedModuleId(),
        owner, &registry);
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

bool EditorSceneContext::SelectParticleEditorModule(kb::scene::ParticleStableId emitterId,
                                                    kb::scene::ParticleStableId moduleId) noexcept {
    const auto* asset = ParticleEditorWorkingAsset();
    return asset != nullptr && particleEditorWorkspace_.SelectModule(*asset, emitterId, moduleId);
}

bool EditorSceneContext::AddParticleEditorModule(kb::scene::ParticleModuleType type,
                                                 kb::scene::ParticleStableId targetEmitterId) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::AddModule(particleEditorDocument_, particleEditorWorkspace_,
            particleEditorWorkspace_.SelectedEmitterId(), type, targetEmitterId));
}

bool EditorSceneContext::ToggleParticleEditorModule(kb::scene::ParticleStableId moduleId) {
    const auto* asset = ParticleEditorWorkingAsset();
    if (asset == nullptr) return false;
    const auto* emitter = kb::particle_editor::ParticleEmitterListModel::Find(
        *asset, particleEditorWorkspace_.SelectedEmitterId());
    if (emitter == nullptr) return false;
    const auto module = std::find_if(emitter->modules.begin(), emitter->modules.end(),
        [moduleId](const auto& value) { return value.moduleId == moduleId; });
    return module != emitter->modules.end() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::SetModuleEnabled(particleEditorDocument_,
            particleEditorWorkspace_, emitter->emitterId, moduleId, !module->enabled));
}

bool EditorSceneContext::MoveParticleEditorModule(kb::scene::ParticleStableId moduleId,
                                                  std::uint32_t targetOrder) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::ReorderModule(particleEditorDocument_,
            particleEditorWorkspace_, particleEditorWorkspace_.SelectedEmitterId(), moduleId, targetOrder));
}

bool EditorSceneContext::BeginParticleEditorModuleDrag(kb::scene::ParticleStableId moduleId) noexcept {
    const auto* asset = ParticleEditorWorkingAsset();
    return asset != nullptr && particleEditorWorkspace_.BeginModuleDrag(
        *asset, particleEditorWorkspace_.SelectedEmitterId(), moduleId);
}
void EditorSceneContext::UpdateParticleEditorModuleDrag(std::uint32_t targetOrder) noexcept {
    particleEditorWorkspace_.UpdateModuleDrag(targetOrder);
}
bool EditorSceneContext::CommitParticleEditorModuleDrag() {
    if (!particleEditorWorkspace_.ModuleDragActive()) return false;
    const auto moduleId = particleEditorWorkspace_.DraggedModuleId();
    const auto targetOrder = particleEditorWorkspace_.ModuleDragTargetOrder();
    particleEditorWorkspace_.EndModuleDrag();
    return MoveParticleEditorModule(moduleId, targetOrder);
}
void EditorSceneContext::CancelParticleEditorModuleDrag() noexcept { particleEditorWorkspace_.EndModuleDrag(); }

bool EditorSceneContext::RemoveParticleEditorModule(kb::scene::ParticleStableId moduleId) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::RemoveModule(particleEditorDocument_,
            particleEditorWorkspace_, particleEditorWorkspace_.SelectedEmitterId(), moduleId));
}

bool EditorSceneContext::SetParticleEditorOutputType(kb::scene::ParticleOutputType type) {
    const auto* asset = ParticleEditorWorkingAsset();
    if (asset == nullptr) return false;
    const auto* emitter = kb::particle_editor::ParticleEmitterListModel::Find(
        *asset, particleEditorWorkspace_.SelectedEmitterId());
    if (emitter == nullptr) return false;
    auto output = emitter->output;
    output.type = type;
    output.payload = kb::scene::DefaultParticleOutputPayload(type);
    auto capabilityCandidate = *asset;
    auto candidateEmitter = std::find_if(capabilityCandidate.emitters.begin(), capabilityCandidate.emitters.end(),
        [id = emitter->emitterId](const auto& value) { return value.emitterId == id; });
    candidateEmitter->output = output;
    const auto capabilities = kb::particle_plugin::ParticleEffectCompiler::ValidateCapabilities(capabilityCandidate);
    const auto rejected = std::find_if(capabilities.begin(), capabilities.end(), [id = emitter->emitterId](const auto& diagnostic) {
        return diagnostic.emitterId == id && diagnostic.propertyPath.find(".output.type") != std::string::npos;
    });
    if (rejected != capabilities.end()) {
        console_.Error("Particles", kb::scene::FormatParticleEffectDiagnostic(*rejected));
        return false;
    }
    output.mesh = {};
    return FinalizeParticleEditorCommand(kb::particle_editor::ParticleEditorCommands::SetEmitterOutput(
        particleEditorDocument_, particleEditorWorkspace_, emitter->emitterId, std::move(output)));
}

bool EditorSceneContext::SetParticleEditorOutputReference(kb::assets::AssetKind kind, kb::assets::AssetId id) {
    const auto* asset = ParticleEditorWorkingAsset();
    if (asset == nullptr || !id.IsValid()) return false;
    const auto& registry = scene_->Assets().Manager().Registry();
    const auto* metadata = registry.Find(id);
    if (metadata == nullptr || !kb::assets::AssetMatchesKind(*metadata, kind)) {
        console_.Error("Particles", "Particle output picker returned an asset of the wrong kind.");
        return false;
    }
    const auto* emitter = kb::particle_editor::ParticleEmitterListModel::Find(
        *asset, particleEditorWorkspace_.SelectedEmitterId());
    if (emitter == nullptr) return false;
    auto output = emitter->output;
    const kb::scene::ParticleAssetReference reference{.assetId = id.value,
        .virtualPath = metadata->virtualPath.generic_string()};
    if (kind == kb::assets::AssetKind::Material) output.material = reference;
    else if (kind == kb::assets::AssetKind::Mesh) output.mesh = reference;
    else if (kind == kb::assets::AssetKind::Texture) output.textureAtlas = reference;
    else return false;
    return FinalizeParticleEditorCommand(kb::particle_editor::ParticleEditorCommands::SetEmitterOutput(
        particleEditorDocument_, particleEditorWorkspace_, emitter->emitterId, std::move(output)));
}

bool EditorSceneContext::SetParticleEditorSpawn(kb::scene::ParticleSpawnAsset spawn) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::SetEmitterSpawn(particleEditorDocument_,
            particleEditorWorkspace_, particleEditorWorkspace_.SelectedEmitterId(), std::move(spawn)));
}

bool EditorSceneContext::SetParticleEditorModulePayload(kb::scene::ParticleStableId moduleId,
                                                       kb::scene::ParticleModulePayload payload) {
    return HasParticleEditorAsset() && FinalizeParticleEditorCommand(
        kb::particle_editor::ParticleEditorCommands::SetModulePayload(particleEditorDocument_,
            particleEditorWorkspace_, particleEditorWorkspace_.SelectedEmitterId(), moduleId, std::move(payload)));
}

bool EditorSceneContext::FocusParticleEditorDiagnostic(std::size_t diagnosticIndex) noexcept {
    const auto inspector = ParticleEditorInspector();
    if (diagnosticIndex >= inspector.diagnostics.size()) return false;
    const auto& diagnostic = inspector.diagnostics[diagnosticIndex];
    particleEditorWorkspace_.FocusDiagnostic(diagnostic.propertyPath, diagnostic.emitterId, diagnostic.moduleId);
    return true;
}

bool EditorSceneContext::NavigateParticleEditorDependency(std::size_t dependencyIndex) {
    const auto inspector = ParticleEditorInspector();
    return dependencyIndex < inspector.dependencies.size() &&
        assetBrowser_.SelectAsset(inspector.dependencies[dependencyIndex].assetId, scene_->Assets().Manager());
}

bool EditorSceneContext::EditParticleEditorProperty(std::size_t propertyIndex, std::string_view text) {
    const auto inspector = ParticleEditorInspector();
    if (propertyIndex >= inspector.properties.size() || !inspector.properties[propertyIndex].editable) return false;
    const auto& row = inspector.properties[propertyIndex];
    const auto* asset = ParticleEditorWorkingAsset();
    const auto* emitter = asset == nullptr ? nullptr : kb::particle_editor::ParticleEmitterListModel::Find(*asset, inspector.emitterId);
    if (emitter == nullptr) return false;
    auto spawn = emitter->spawn;
    auto output = emitter->output;
    float floatValue = 0.0F;
    std::uint32_t uintValue = 0U;
    bool boolValue = false;
    kb::math::Vec3 vectorValue{};
    const auto setFloat = [&](float& target) { return ParseNumber(text, floatValue) && (target = floatValue, true); };
    const auto setUInt = [&](std::uint32_t& target) { return ParseNumber(text, uintValue) && (target = uintValue, true); };
    const auto setBool = [&](bool& target) { return ParseBool(text, boolValue) && (target = boolValue, true); };
    bool parsed = false;
    bool editsSpawn = true;
    switch (row.property) {
    case kb::particle_editor::ParticleEditorProperty::SpawnLifetimeMin: parsed = setFloat(spawn.lifetimeMin); break;
    case kb::particle_editor::ParticleEditorProperty::SpawnLifetimeMax: parsed = setFloat(spawn.lifetimeMax); break;
    case kb::particle_editor::ParticleEditorProperty::SpawnSpeedMin: parsed = setFloat(spawn.speedMin); break;
    case kb::particle_editor::ParticleEditorProperty::SpawnSpeedMax: parsed = setFloat(spawn.speedMax); break;
    case kb::particle_editor::ParticleEditorProperty::SpawnDirection:
        parsed = ParseVec3(text, vectorValue); if (parsed) spawn.direction = vectorValue; break;
    case kb::particle_editor::ParticleEditorProperty::SpawnSpreadDegrees: parsed = setFloat(spawn.spreadDegrees); break;
    case kb::particle_editor::ParticleEditorProperty::SpawnRandomization: parsed = setFloat(spawn.randomization); break;
    case kb::particle_editor::ParticleEditorProperty::SpawnPrewarmSeconds: parsed = setFloat(spawn.prewarmSeconds); break;
    default: editsSpawn = false; break;
    }
    if (editsSpawn) {
        if (!parsed) { console_.Error("Particles", "Particle property value is malformed."); return false; }
        return SetParticleEditorSpawn(std::move(spawn));
    }
    switch (row.property) {
    case kb::particle_editor::ParticleEditorProperty::OutputBlend:
        parsed = ParseNumber(text, uintValue) && uintValue <= static_cast<std::uint32_t>(kb::scene::ParticleBlendMode::Premultiplied);
        if (parsed) output.blend = static_cast<kb::scene::ParticleBlendMode>(uintValue); break;
    case kb::particle_editor::ParticleEditorProperty::OutputSort:
        parsed = ParseNumber(text, uintValue) && uintValue <= static_cast<std::uint32_t>(kb::scene::ParticleSortMode::Age);
        if (parsed) output.sort = static_cast<kb::scene::ParticleSortMode>(uintValue); break;
    case kb::particle_editor::ParticleEditorProperty::OutputDepthTest: parsed = setBool(output.depthTest); break;
    case kb::particle_editor::ParticleEditorProperty::OutputDepthWrite: parsed = setBool(output.depthWrite); break;
    case kb::particle_editor::ParticleEditorProperty::OutputSoftParticles: parsed = setBool(output.softParticles); break;
    case kb::particle_editor::ParticleEditorProperty::OutputAntiAliasing: parsed = setBool(output.antiAliasing); break;
    case kb::particle_editor::ParticleEditorProperty::OutputAlignment:
        parsed = ParseNumber(text, uintValue) && uintValue <= static_cast<std::uint32_t>(kb::scene::ParticleAlignment::Local);
        if (parsed) output.alignment = static_cast<kb::scene::ParticleAlignment>(uintValue); break;
    case kb::particle_editor::ParticleEditorProperty::FlipbookColumns:
    case kb::particle_editor::ParticleEditorProperty::FlipbookRows:
    case kb::particle_editor::ParticleEditorProperty::FlipbookFramesPerSecond:
    case kb::particle_editor::ParticleEditorProperty::FlipbookLooping:
    case kb::particle_editor::ParticleEditorProperty::OutputVelocityScale:
    case kb::particle_editor::ParticleEditorProperty::OutputMinimumLength:
    case kb::particle_editor::ParticleEditorProperty::OutputPointDiameter:
        std::visit([&](auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, kb::scene::ParticleBillboardOutput> ||
                          std::is_same_v<T, kb::scene::ParticleStretchedBillboardOutput> ||
                          std::is_same_v<T, kb::scene::ParticlePointSpriteOutput>) {
                if (row.property == kb::particle_editor::ParticleEditorProperty::FlipbookColumns) parsed = setUInt(payload.flipbook.columns);
                else if (row.property == kb::particle_editor::ParticleEditorProperty::FlipbookRows) parsed = setUInt(payload.flipbook.rows);
                else if (row.property == kb::particle_editor::ParticleEditorProperty::FlipbookFramesPerSecond) parsed = setFloat(payload.flipbook.framesPerSecond);
                else if (row.property == kb::particle_editor::ParticleEditorProperty::FlipbookLooping) parsed = setBool(payload.flipbook.looping);
                else if constexpr (std::is_same_v<T, kb::scene::ParticleStretchedBillboardOutput>) {
                    if (row.property == kb::particle_editor::ParticleEditorProperty::OutputVelocityScale) parsed = setFloat(payload.velocityScale);
                    else if (row.property == kb::particle_editor::ParticleEditorProperty::OutputMinimumLength) parsed = setFloat(payload.minimumLength);
                } else if constexpr (std::is_same_v<T, kb::scene::ParticlePointSpriteOutput>)
                    if (row.property == kb::particle_editor::ParticleEditorProperty::OutputPointDiameter) parsed = setFloat(payload.diameter);
            }
        }, output.payload);
        break;
    case kb::particle_editor::ParticleEditorProperty::ModulePayload: {
        const auto module = std::find_if(emitter->modules.begin(), emitter->modules.end(),
            [id = row.moduleId](const auto& value) { return value.moduleId == id; });
        if (module == emitter->modules.end()) return false;
        auto payload = module->payload;
        std::visit([&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, kb::scene::ParticleInitialVelocityModule>) {
                if (row.payloadField == 0U) { parsed = ParseVec3(text, vectorValue); if (parsed) value.direction = vectorValue; }
                else if (row.payloadField == 1U) parsed = setFloat(value.speedMin);
                else if (row.payloadField == 2U) parsed = setFloat(value.speedMax);
                else if (row.payloadField == 3U) parsed = setFloat(value.randomization);
                else if (row.payloadField == 4U) parsed = setFloat(value.spreadDegrees);
            } else if constexpr (std::is_same_v<T, kb::scene::ParticleGravityModule>) {
                if (row.payloadField == 0U) { parsed = ParseVec3(text, vectorValue); if (parsed) value.acceleration = vectorValue; }
                else if (row.payloadField == 1U) parsed = setFloat(value.sceneGravityScale);
            } else if constexpr (std::is_same_v<T, kb::scene::ParticleWindModule>) {
                parsed = ParseVec3(text, vectorValue); if (parsed) value.acceleration = vectorValue;
            } else if constexpr (std::is_same_v<T, kb::scene::ParticleDragModule>) parsed = setFloat(value.coefficient);
            else if constexpr (std::is_same_v<T, kb::scene::ParticleCollisionPlaneModule>) {
                if (row.payloadField == 0U) { parsed = ParseVec3(text, vectorValue); if (parsed) value.normal = vectorValue; }
                else if (row.payloadField == 1U) parsed = setFloat(value.distance);
                else if (row.payloadField == 2U) parsed = setFloat(value.restitution);
                else if (row.payloadField == 3U) parsed = setFloat(value.friction);
                else if (row.payloadField == 4U) parsed = setUInt(value.maxEventsPerStep);
            } else if constexpr (std::is_same_v<T, kb::scene::ParticleSubEmitterModule>) {
                if (row.payloadField == 0U) { std::uint64_t id = 0U; parsed = ParseNumber(text, id); if (parsed) value.targetEmitterId = id; }
                else if (row.payloadField == 1U) { parsed = ParseNumber(text, uintValue) && uintValue <= 2U; if (parsed) value.trigger = static_cast<kb::scene::ParticleEventTrigger>(uintValue); }
                else if (row.payloadField == 2U) parsed = setUInt(value.count);
                else if (row.payloadField == 3U) parsed = setUInt(value.maxDepth);
            }
        }, payload);
        if (!parsed) { console_.Error("Particles", "Particle module property value is malformed."); return false; }
        return SetParticleEditorModulePayload(row.moduleId, std::move(payload));
    }
    default: break;
    }
    if (!parsed) { console_.Error("Particles", "Particle property value is malformed."); return false; }
    return FinalizeParticleEditorCommand(kb::particle_editor::ParticleEditorCommands::SetEmitterOutput(
        particleEditorDocument_, particleEditorWorkspace_, emitter->emitterId, std::move(output)));
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
