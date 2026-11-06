#include "scene/EditorTerrainJobs.hpp"

#include "TerrainErosion.hpp"
#include "TerrainLayerPainter.hpp"
#include "TerrainPresets.hpp"
#include "TerrainTask.hpp"
#include "console/EditorConsoleState.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace kb::editor {
namespace {

using kb::terrain_editor::TerrainBackgroundTask;
using kb::terrain_editor::TerrainTaskControl;

struct RunningJob {
    std::unique_ptr<TerrainBackgroundTask> task;
    EditorTerrainJobKind kind = EditorTerrainJobKind::None;
    kb::scene::SceneEntity entity{};
    kb::assets::AssetId assetId{};
    std::string label;
    int shownPercent = -1;
};

RunningJob& Job() noexcept {
    static RunningJob job;
    return job;
}

// One core is left to the editor, so the interface stays responsive while a job runs.
[[nodiscard]] std::uint32_t WorkerCount() noexcept {
    return std::max(1U, std::thread::hardware_concurrency() > 1U ? std::thread::hardware_concurrency() - 1U : 1U);
}

[[nodiscard]] bool Start(
    EditorSceneContext& context,
    kb::scene::SceneEntity entity,
    EditorTerrainJobKind kind,
    std::string label,
    TerrainBackgroundTask::Work work,
    std::string* error) {
    RunningJob& job = Job();
    if (job.task != nullptr) {
        if (error != nullptr) *error = "A terrain job is already running; wait for it or cancel it.";
        return false;
    }
    context.CancelTerrainBrushStroke();
    std::optional<EditorTerrainAssetState> captured = EditorTerrainService::Capture(context.Scene(), entity, error);
    if (!captured.has_value()) return false;
    job.task = std::make_unique<TerrainBackgroundTask>(
        std::move(captured->terrain),
        [work = std::move(work)](kb::assets::TerrainAsset& terrain, TerrainTaskControl& control, std::string& failure) {
            control.maxWorkers = WorkerCount();
            return work(terrain, control, failure);
        });
    job.kind = kind;
    job.entity = entity;
    job.assetId = captured->assetId;
    job.label = std::move(label);
    job.shownPercent = -1;
    if (error != nullptr) error->clear();
    return true;
}

// The finished task's result as one Undo step; the job is over either way.
[[nodiscard]] bool Deliver(EditorSceneContext& context, double waitSeconds, std::string* error) {
    RunningJob job = std::move(Job());
    Job() = RunningJob{};
    std::string failure;
    std::optional<kb::assets::TerrainAsset> result = job.task->Wait(waitSeconds, &failure);
    if (!result.has_value()) {
        if (error != nullptr) *error = failure;
        if (failure == "cancelled") context.Console().Info("Terrain", job.label + " cancelled.");
        else context.Console().Warning("Terrain", job.label + " failed: " + failure);
        return false;
    }
    if (!context.ReplaceTerrain(job.entity, job.assetId, std::move(*result), job.label, &failure)) {
        if (error != nullptr) *error = failure;
        context.Console().Warning("Terrain", job.label + " could not be applied: " + failure);
        return false;
    }
    if (error != nullptr) error->clear();
    context.Console().Info("Terrain", job.label + " done.");
    return true;
}

} // namespace

EditorTerrainJobKind EditorTerrainJobs::Active() noexcept {
    return Job().task != nullptr ? Job().kind : EditorTerrainJobKind::None;
}

float EditorTerrainJobs::Progress() noexcept {
    return Job().task != nullptr ? Job().task->Progress() : 0.0F;
}

bool EditorTerrainJobs::StartWorld(
    EditorSceneContext& context, kb::scene::SceneEntity entity, std::string_view preset, std::uint32_t seed, float character, std::string* error) {
    if (!kb::terrain_editor::IsTerrainPreset(preset)) {
        if (error != nullptr) *error = "Unknown world preset '" + std::string{ preset } + "'";
        return false;
    }
    return Start(context, entity, EditorTerrainJobKind::World, "New World", [name = std::string{ preset }, seed, character](
        kb::assets::TerrainAsset& terrain, TerrainTaskControl& control, std::string& failure) {
        return kb::terrain_editor::ApplyTerrainPreset(terrain, name, seed, character, control, failure);
    }, error);
}

bool EditorTerrainJobs::StartErosion(
    EditorSceneContext& context, kb::scene::SceneEntity entity, EditorTerrainJobKind kind, std::string* error) {
    if (kind == EditorTerrainJobKind::Thermal) {
        return Start(context, entity, kind, "Thermal Erosion", [](
            kb::assets::TerrainAsset& terrain, TerrainTaskControl& control, std::string& failure) {
            return kb::terrain_editor::ErodeTerrainThermal(terrain, {}, control, failure);
        }, error);
    }
    if (kind == EditorTerrainJobKind::Hydraulic) {
        return Start(context, entity, kind, "Hydraulic Erosion", [](
            kb::assets::TerrainAsset& terrain, TerrainTaskControl& control, std::string& failure) {
            return kb::terrain_editor::ErodeTerrainHydraulic(terrain, {}, control, failure);
        }, error);
    }
    if (error != nullptr) *error = "Erosion is thermal or hydraulic";
    return false;
}

bool EditorTerrainJobs::StartSettings(
    EditorSceneContext& context, kb::scene::SceneEntity entity, std::uint32_t splatResolution, float lodPixelError, std::uint32_t collisionStep,
    std::string* error) {
    if (splatResolution != 0U && !kb::terrain_editor::IsTerrainSplatResolution(splatResolution)) {
        if (error != nullptr) *error = "The splat map is 512, 1024 or 2048 texels across";
        return false;
    }
    return Start(context, entity, EditorTerrainJobKind::Settings, "Terrain Settings", [splatResolution, lodPixelError, collisionStep](
        kb::assets::TerrainAsset& terrain, TerrainTaskControl& control, std::string& failure) {
        terrain.lodPixelError = lodPixelError;
        terrain.collisionStep = collisionStep;
        if (splatResolution != 0U && !kb::terrain_editor::SetTerrainSplatResolution(terrain, splatResolution, control.maxWorkers)) {
            failure = "The splat map needs a material layer: add one in the Inspector first.";
            return false;
        }
        return kb::assets::IsTerrainAssetValid(terrain, &failure);
    }, error);
}

void EditorTerrainJobs::Cancel() noexcept {
    if (Job().task != nullptr) Job().task->Cancel();
}

bool EditorTerrainJobs::Poll(EditorSceneContext& context) {
    RunningJob& job = Job();
    if (job.task == nullptr) return false;
    if (job.task->Finished()) {
        static_cast<void>(Deliver(context, 0.0, nullptr));
        return true;
    }
    const int percent = static_cast<int>(job.task->Progress() * 100.0F);
    const bool moved = percent != job.shownPercent;
    job.shownPercent = percent;
    return moved;
}

bool EditorTerrainJobs::Finish(EditorSceneContext& context, double seconds, std::string* error) {
    if (Job().task == nullptr) {
        if (error != nullptr) *error = "No terrain job is running";
        return false;
    }
    return Deliver(context, seconds, error);
}

} // namespace kb::editor
