#include "scene/StaticMeshEditorSession.hpp"

#include "localization/EditorLocalization.hpp"
#include "scene/EditorPreviewStudio.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <span>
#include <thread>
#include <utility>

namespace kb::editor {
namespace {

constexpr float kBuildPulseSeconds = 0.15F;
constexpr float kMinimumFocusRadius = 0.25F;

[[nodiscard]] std::uint64_t LevelTriangles(const kb::render::RenderMeshAssetData& mesh, const kb::render::RenderMeshLodDesc& lod) {
    std::uint64_t indices = 0U;
    for (std::uint32_t section = 0U; section < lod.sectionCount; ++section) {
        indices += mesh.sections[lod.firstSection + section].indexCount;
    }
    return indices / 3U;
}

[[nodiscard]] std::uint64_t TriangleCount(const kb::render::RenderMeshAssetData& mesh) {
    return (mesh.indices32.empty() ? mesh.indices16.size() : mesh.indices32.size()) / 3U;
}

[[nodiscard]] std::string MaterialName(const std::vector<kb::assets::AssetMetadata>& registry, std::uint64_t id) {
    const auto found = std::ranges::find_if(registry, [id](const kb::assets::AssetMetadata& metadata) { return metadata.id.value == id; });
    if (found == registry.end()) return {};
    const std::string file = found->virtualPath.filename().string();
    return file.empty() ? found->name : file;
}

[[nodiscard]] std::string NotImportedText() {
    return std::string{ EditorText("staticMeshEditor.notImported",
        "This mesh is not an imported model, so its levels of detail cannot be edited.") };
}

[[nodiscard]] kb::scene::Vec3 ToVec3(const std::array<float, 3>& value) noexcept {
    return kb::scene::Vec3{ value[0], value[1], value[2] };
}

} // namespace

// One load-and-build on a worker. The worker reads the mesh unless the session already holds it, builds the levels
// the settings ask for exactly as the cooker bakes them, and publishes its result by setting `finished`.
struct StaticMeshEditorSession::Job {
    kb::assets::AssetMetadata metadata;
    std::optional<kb::assets::AssetMeshLodSettings> settings;
    std::shared_ptr<const kb::render::RenderMeshAssetData> source;
    std::shared_ptr<const kb::render::RenderMeshAssetData> built;
    std::string error;
    std::shared_ptr<kb::assets::AssetLoadCancellation> cancellation = std::make_shared<kb::assets::AssetLoadCancellation>();
    std::atomic_bool finished{ false };
    std::thread thread;

    ~Job() {
        cancellation->requested.store(true, std::memory_order_release);
        if (thread.joinable()) thread.join();
    }

    void Run() {
        if (source == nullptr) {
            kb::render::RenderMeshAssetLoader loader;
            const kb::assets::AssetLoadResult loaded = loader.Load(kb::assets::AssetLoadRequest{
                metadata, metadata.physicalPath, nullptr, std::nullopt, cancellation });
            if (!loaded.Succeeded()) {
                error = loaded.error;
                return;
            }
            source = std::static_pointer_cast<const kb::render::RenderMeshAssetData>(loaded.asset);
        }
        built = source;
        if (cancellation->requested.load(std::memory_order_acquire)) return;
        // A mesh that carries levels of its own keeps them; the baker would refuse to re-cluster them.
        if (!settings.has_value() || !settings->BuildsChain(TriangleCount(*source)) || source->lods.size() > 1U) return;
        kb::render::RenderMeshAssetData work = *source;
        work.lod = *settings;
        kb::render::RenderMeshAssetData chained;
        const kb::render::bake::MeshBakeStatus status = kb::render::bake::BuildRuntimeLodMesh(work, chained);
        if (status != kb::render::bake::MeshBakeStatus::Success) {
            error = EditorLocalization::Format("staticMeshEditor.buildFailed", "The levels could not be built ({status}).",
                { { "status", std::string{ kb::render::bake::ToString(status) } } });
            return;
        }
        chained.RefreshDesc();
        built = std::make_shared<const kb::render::RenderMeshAssetData>(std::move(chained));
    }
};

StaticMeshEditorSession::StaticMeshEditorSession() = default;

StaticMeshEditorSession::~StaticMeshEditorSession() = default;

StaticMeshEditorSession& StaticMeshEditorSession::Instance() {
    static StaticMeshEditorSession session;
    return session;
}

bool StaticMeshEditorSession::CanOpen(const kb::assets::AssetMetadata& metadata) noexcept {
    // Terrains and 2D maps are meshes to the loader too, but they have editors of their own.
    const std::filesystem::path extension = metadata.virtualPath.extension();
    return metadata.type == "RenderMesh" && extension != kb::assets::kTerrainAssetExtension &&
        extension != kb::assets::kMap2DAssetExtension;
}

bool StaticMeshEditorSession::Open(const kb::scene::Scene& source, const kb::assets::AssetMetadata& metadata) {
    if (!CanOpen(metadata)) return false;
    Close();
    asset_ = metadata;
    const std::span<const kb::assets::AssetMetadata> registry = source.Assets().Manager().Registry().All();
    registry_.assign(registry.begin(), registry.end());
    const std::optional<kb::assets::AssetMeshLodSettings> stored = metadata.physicalPath.empty()
        ? std::nullopt : kb::assets::AssetImportService::ReadMeshLod(metadata.physicalPath);
    editable_ = stored.has_value();
    if (editable_) {
        draft_ = applied_ = *stored;
    } else {
        problem_ = NotImportedText();
    }
    openRequested_ = true;
    StartBuild(stored, false);
    return true;
}

void StaticMeshEditorSession::Close() noexcept {
    RetireJob();
    asset_ = {};
    registry_.clear();
    editable_ = false;
    openRequested_ = false;
    problem_.clear();
    notice_.clear();
    draft_ = applied_ = {};
    summary_ = {};
    source_.reset();
    built_.reset();
    buildSeconds_ = 0.0F;
    pinned_.reset();
    viewMode_ = StaticMeshViewMode::Lit;
    ui_ = {};
    camera_ = {};
    preview_.reset();
    meshEntity_ = {};
    ++revision_;
}

bool StaticMeshEditorSession::ConsumeOpenRequest() noexcept {
    return std::exchange(openRequested_, false);
}

std::string StaticMeshEditorSession::Title() const {
    const std::string file = asset_.virtualPath.filename().string();
    return file.empty() ? asset_.name : file;
}

void StaticMeshEditorSession::StartBuild(std::optional<kb::assets::AssetMeshLodSettings> settings, bool reuseSource) {
    RetireJob();
    auto job = std::make_unique<Job>();
    job->metadata = asset_;
    job->settings = settings;
    if (!reuseSource) source_.reset();
    job->source = source_;
    Job* const running = job.get();
    running->thread = std::thread([running] { running->Run(); running->finished.store(true, std::memory_order_release); });
    job_ = std::move(job);
    buildSeconds_ = 0.0F;
}

void StaticMeshEditorSession::RetireJob() noexcept {
    if (job_ == nullptr) return;
    job_->cancellation->requested.store(true, std::memory_order_release);
    retired_.push_back(std::move(job_));
}

bool StaticMeshEditorSession::Tick(float deltaSeconds) {
    std::erase_if(retired_, [](const std::unique_ptr<Job>& job) { return job->finished.load(std::memory_order_acquire); });
    if (!IsOpen()) return false;
    bool changed = false;
    if (job_ != nullptr) {
        if (job_->finished.load(std::memory_order_acquire)) {
            AdoptFinishedJob();
            changed = true;
        } else {
            buildSeconds_ += deltaSeconds;
            pulseSeconds_ += deltaSeconds;
            if (pulseSeconds_ >= kBuildPulseSeconds) {
                pulseSeconds_ = 0.0F;
                changed = true;
            }
        }
    }
    changed = camera_.ApplyQueuedPointer() || changed;
    return camera_.TickFocus(deltaSeconds) || changed;
}

void StaticMeshEditorSession::AdoptFinishedJob() {
    const bool firstBuild = source_ == nullptr;
    const std::unique_ptr<Job> job = std::move(job_);
    problem_ = editable_ ? std::string{} : NotImportedText();
    if (!job->error.empty()) problem_ = job->error;
    if (job->source == nullptr) return;
    source_ = job->source;
    built_ = job->built;
    const kb::render::RenderMeshAssetData& mesh = *built_;
    summary_ = {};
    for (const kb::render::RenderMeshLodDesc& lod : mesh.lods) summary_.levelTriangles.push_back(LevelTriangles(mesh, lod));
    if (summary_.levelTriangles.empty()) summary_.levelTriangles.push_back(TriangleCount(mesh));
    summary_.vertices = std::max(source_->vertices.size(), source_->tangentVertices.size());
    summary_.sections = static_cast<std::uint32_t>(std::max<std::size_t>(source_->sections.size(), 1U));
    for (std::size_t axis = 0U; axis < 3U; ++axis) summary_.size[axis] = 2.0F * source_->boundsBox.halfExtents[axis];
    for (std::size_t slot = 0U; slot < source_->materialSlots.size(); ++slot) {
        summary_.materialSlots.push_back(StaticMeshMaterialSlot{
            .name = slot < source_->materialNames.size() ? source_->materialNames[slot] : std::string{},
            .material = MaterialName(registry_, source_->materialSlots[slot].defaultMaterialAssetId),
        });
    }
    if (pinned_.has_value() && *pinned_ >= summary_.levelTriangles.size()) pinned_.reset();
    RebuildPreview();
    if (firstBuild) Frame();
}

void StaticMeshEditorSession::RebuildPreview() {
    preview_ = std::make_unique<kb::scene::Scene>(kb::scene::SceneMode::Runtime);
    for (const kb::assets::AssetMetadata& metadata : registry_) {
        static_cast<void>(preview_->Assets().Manager().RegisterAsset(metadata));
    }
    const kb::render::RenderMeshAssetData& mesh = *built_;
    focusCenter_ = ToVec3(mesh.bounds.center);
    focusRadius_ = std::max(kMinimumFocusRadius, mesh.bounds.radius);
    const float floorHeight = mesh.boundsBox.center[1] - mesh.boundsBox.halfExtents[1];
    static_cast<void>(BuildEditorPreviewStudio(*preview_, camera_, floorHeight, std::max(2.0F, 3.0F * focusRadius_)));
    meshEntity_ = preview_->Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Static Mesh" });
    static_cast<void>(preview_->Components().MeshRenderers().Set(meshEntity_, kb::scene::MeshRendererComponent{ .meshAssetId = asset_.id.value }));
    if (!preview_->Assets().Manager().PublishRuntimeAsset(asset_.id, std::const_pointer_cast<kb::render::RenderMeshAssetData>(built_))) {
        problem_ = preview_->Assets().Manager().LastError();
    }
    ApplyPin();
    static_cast<void>(preview_->Runtime().Update(0.0F));
    ++revision_;
}

void StaticMeshEditorSession::ApplyPin() noexcept {
    if (preview_ == nullptr || !meshEntity_.IsValid()) return;
    if (!pinned_.has_value()) {
        preview_->Components().DetailSwitches().Remove(meshEntity_);
        return;
    }
    // A group of its own per pinned level: the renderer keeps a group's last level through its coverage
    // thresholds, and a pin has to take effect at once.
    preview_->Components().DetailSwitches().Set(meshEntity_, kb::scene::SceneDetailSwitchComponent{
        .groupId = (static_cast<std::uint64_t>(*pinned_) + 1U) << 32U | meshEntity_.Id(),
        .minimumLod = *pinned_,
        .maximumLod = *pinned_,
        .enabled = true,
    });
}

void StaticMeshEditorSession::SetPinned(std::optional<std::uint32_t> level) noexcept {
    if (pinned_ == level) return;
    pinned_ = level;
    ApplyPin();
    ++revision_;
}

void StaticMeshEditorSession::Frame() noexcept {
    camera_.FocusOn(focusCenter_, focusRadius_, 0.0F);
}

void StaticMeshEditorSession::SetLevelCount(std::uint8_t count) noexcept {
    count = std::clamp<std::uint8_t>(count, 1U, static_cast<std::uint8_t>(kb::assets::kMeshLodMaxLevels));
    for (std::uint32_t level = draft_.levelCount; level < count; ++level) {
        const float above = level <= 1U ? 100.0F : draft_.levels[level - 2U].trianglePercent;
        draft_.levels[level - 1U] = kb::assets::AssetMeshLodLevel{ .trianglePercent = above * 0.5F, .errorPercent = draft_.errorPercent };
    }
    draft_.levelCount = count;
    notice_.clear();
}

void StaticMeshEditorSession::SetManual(bool manual) noexcept {
    if (draft_.manual == manual) return;
    if (manual) {
        for (std::uint32_t level = 1U; level < draft_.levelCount; ++level) draft_.levels[level - 1U] = draft_.Level(level);
    }
    draft_.manual = manual;
    notice_.clear();
}

std::string StaticMeshEditorSession::Validate(const kb::assets::AssetMeshLodSettings& lod) {
    if (lod.levelCount < 1U || lod.levelCount > kb::assets::kMeshLodMaxLevels) {
        return EditorLocalization::Format("staticMeshEditor.invalidLevels", "Levels must be between 1 and {max}.",
            { { "max", std::to_string(kb::assets::kMeshLodMaxLevels) } });
    }
    const auto inDeviationRange = [](float value) { return std::isfinite(value) && value > 0.0F && value <= 100.0F; };
    if (!lod.manual) {
        if (!(std::isfinite(lod.keepPercent) && lod.keepPercent > 0.0F && lod.keepPercent < 100.0F)) {
            return std::string{ EditorText("staticMeshEditor.invalidKeep", "Keeps % must be above 0 and below 100.") };
        }
        if (!inDeviationRange(lod.errorPercent)) {
            return std::string{ EditorText("staticMeshEditor.invalidDeviation", "Deviation % must be above 0 and up to 100.") };
        }
        return {};
    }
    float previous = 100.0F;
    for (std::uint32_t level = 1U; level < lod.levelCount; ++level) {
        const kb::assets::AssetMeshLodLevel& entry = lod.levels[level - 1U];
        if (!std::isfinite(entry.trianglePercent) || !(entry.trianglePercent > 0.0F) || !(entry.trianglePercent < previous)) {
            return EditorLocalization::Format("staticMeshEditor.invalidLevelShare",
                "LOD{level} must keep a share of triangles above 0 and below LOD{above}.",
                { { "level", std::to_string(level) }, { "above", std::to_string(level - 1U) } });
        }
        if (!inDeviationRange(entry.errorPercent)) {
            return EditorLocalization::Format("staticMeshEditor.invalidLevelDeviation",
                "LOD{level} deviation must be above 0 and up to 100.", { { "level", std::to_string(level) } });
        }
        previous = entry.trianglePercent;
    }
    return {};
}

std::string StaticMeshEditorSession::Generate(kb::assets::AssetManager& manager) {
    notice_.clear();
    if (!editable_) {
        notice_ = problem_;
        return notice_;
    }
    notice_ = Validate(draft_);
    if (notice_.empty()) notice_ = kb::assets::AssetImportService::SetMeshLod(manager, asset_.id, draft_);
    if (!notice_.empty()) return notice_;
    applied_ = draft_;
    StartBuild(applied_, true);
    return {};
}

} // namespace kb::editor
