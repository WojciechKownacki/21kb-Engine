#include "profiler/EditorProfilerSampler.hpp"

#include "profiler/ProfilerFiles.hpp"

#include "engine/ecs/SystemSchedulerTrace.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldTelemetry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

using kb::core::ProfilerArea;
using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerCounters;
using kb::core::ProfilerRegistry;

constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr double kNanosecondsPerMillisecond = 1'000'000.0;

void Publish(EditorProfilerState& profiler, ProfilerBuiltIn counter, double value) {
    profiler.Registry().Publish(ProfilerRegistry::Id(counter), value);
}

[[nodiscard]] double TimerMilliseconds(std::int64_t begin, std::int64_t end, std::int64_t frequency) {
    return static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency);
}

// The renderer's own figures; they describe the frame that finished before this one.
void SampleBgfx(EditorProfilerState& profiler, const ProfilerSelection& selection) {
    const bgfx::Stats* stats = bgfx::getStats();
    if (stats == nullptr) return;
    if (selection.AreaMeasured(ProfilerArea::Cpu) && stats->cpuTimerFreq > 0 && stats->cpuTimeEnd > stats->cpuTimeBegin) {
        Publish(profiler, ProfilerBuiltIn::RenderThreadTime,
            TimerMilliseconds(stats->cpuTimeBegin, stats->cpuTimeEnd, stats->cpuTimerFreq));
    }
    // The GPU's result comes some frames late and bgfx hands the same one out until the next is ready;
    // each is a reading once, under the number of the frame that made it.
    if (selection.AreaMeasured(ProfilerArea::Gpu) && stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin &&
        profiler.NewGpuFrame(stats->gpuFrameNum)) {
        Publish(profiler, ProfilerBuiltIn::GpuTime,
            TimerMilliseconds(stats->gpuTimeBegin, stats->gpuTimeEnd, stats->gpuTimerFreq));
    }
    if (selection.AreaMeasured(ProfilerArea::Render)) {
        Publish(profiler, ProfilerBuiltIn::DrawCalls, stats->numDraw);
        Publish(profiler, ProfilerBuiltIn::ComputeDispatches, stats->numCompute);
        Publish(profiler, ProfilerBuiltIn::Views, stats->numViews);
        Publish(profiler, ProfilerBuiltIn::Triangles,
            static_cast<double>(stats->numPrims[bgfx::Topology::TriList]) + static_cast<double>(stats->numPrims[bgfx::Topology::TriStrip]));
    }
    if (selection.AreaMeasured(ProfilerArea::Memory)) {
        if (stats->gpuMemoryUsed >= 0) Publish(profiler, ProfilerBuiltIn::GpuMemory, static_cast<double>(stats->gpuMemoryUsed) / kBytesPerMegabyte);
        if (stats->textureMemoryUsed >= 0) Publish(profiler, ProfilerBuiltIn::TextureMemory, static_cast<double>(stats->textureMemoryUsed) / kBytesPerMegabyte);
        if (stats->rtMemoryUsed >= 0) Publish(profiler, ProfilerBuiltIn::RenderTargetMemory, static_cast<double>(stats->rtMemoryUsed) / kBytesPerMegabyte);
    }
}

void SampleProcessMemory(EditorProfilerState& profiler) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        Publish(profiler, ProfilerBuiltIn::ProcessMemory, static_cast<double>(counters.WorkingSetSize) / kBytesPerMegabyte);
    }
#endif
}

void SampleEcs(EditorProfilerState& profiler, const kb::scene::Scene& scene) {
    const auto runtime = scene.Runtime();
    const kb::ecs::WorldTelemetrySnapshot world = runtime.EcsWorld().TelemetrySnapshot();
    Publish(profiler, ProfilerBuiltIn::EcsEntities, static_cast<double>(world.entityCount));
    Publish(profiler, ProfilerBuiltIn::EcsChunks, static_cast<double>(world.chunkCount));
    Publish(profiler, ProfilerBuiltIn::EcsPayload, static_cast<double>(world.committedBytes) / kBytesPerMegabyte);
    Publish(profiler, ProfilerBuiltIn::EcsEntityRecords, static_cast<double>(world.entityRecordBytes) / kBytesPerMegabyte);
    Publish(profiler, ProfilerBuiltIn::EcsOccupancy, world.occupancyPercent);
    Publish(profiler, ProfilerBuiltIn::EcsStorageAllocations, static_cast<double>(world.storageSystemAllocationCount));
    Publish(profiler, ProfilerBuiltIn::EcsQueryCacheHits, static_cast<double>(world.queryCacheHits));
    Publish(profiler, ProfilerBuiltIn::EcsQueryCacheMisses, static_cast<double>(world.queryCacheMisses));
    Publish(profiler, ProfilerBuiltIn::EcsQueryPrepare, static_cast<double>(world.queryPrepareElapsedNanoseconds) / kNanosecondsPerMillisecond);
    Publish(profiler, ProfilerBuiltIn::EcsQueryKernels, static_cast<double>(world.queryKernelElapsedNanoseconds) / kNanosecondsPerMillisecond);
    Publish(profiler, ProfilerBuiltIn::EcsQueryTraffic, static_cast<double>(world.queryBytesTouched) / kBytesPerMegabyte);

    // Each system of the scene's scheduler is a counter of its own while the scheduler measures
    // them (the editor turns that on in Play Mode); systems past the registry's limit are counted.
    for (const kb::ecs::SystemSchedulerSystemCounters& system : runtime.LastEcsProfilerTrace().systemCounters) {
        if (const std::optional<std::size_t> counter = profiler.Registry().RegisterCustom(
                "System " + system.systemName, ProfilerArea::Ecs, kb::core::ProfilerUnit::Milliseconds)) {
            profiler.Registry().Publish(*counter, static_cast<double>(system.cpuTimeNanoseconds) / kNanosecondsPerMillisecond);
        }
    }
}

} // namespace

void EditorProfilerSampler::Sample(EditorProfilerState& profiler, const ProfilerSelection& selection,
    const kb::scene::Scene& scene, const EditorProfilerFrame& frame, const kb::render::SceneRenderSubmitStats* renderStats) {
    if (profiler.Panel().paused) return;
    const auto sampleStart = std::chrono::steady_clock::now();
    profiler.SyncThresholds(selection);
    profiler.ForgetUnmeasured(selection);
    const double gameMilliseconds = frame.gameTickMilliseconds.value_or(0.0);
    const double frameMilliseconds = frame.presentMilliseconds + gameMilliseconds;
    if (selection.AreaMeasured(ProfilerArea::Cpu) && frameMilliseconds > 0.0) {
        Publish(profiler, ProfilerBuiltIn::FrameTime, frameMilliseconds);
        Publish(profiler, ProfilerBuiltIn::FramesPerSecond, 1000.0 / frameMilliseconds);
        if (frame.gameTickMilliseconds) Publish(profiler, ProfilerBuiltIn::GameThreadTime, gameMilliseconds);
    }
    SampleBgfx(profiler, selection);
    if (renderStats != nullptr && selection.AreaMeasured(ProfilerArea::Render)) {
        Publish(profiler, ProfilerBuiltIn::VisibleMeshes, renderStats->visibleMeshCount);
        Publish(profiler, ProfilerBuiltIn::CulledInstances, renderStats->culledInstanceCount);
        Publish(profiler, ProfilerBuiltIn::SceneLights, renderStats->sceneLightCount);
    }
    if (selection.AreaMeasured(ProfilerArea::Memory)) SampleProcessMemory(profiler);
    if (selection.AreaMeasured(ProfilerArea::Ecs)) SampleEcs(profiler, scene);

    // The frame's regions are filed under its number, and the recording takes its counters.
    const std::uint64_t number = profiler.NextFrame();
    if (selection.AreaMeasured(ProfilerArea::Cpu)) {
        const auto runtime = scene.Runtime();
        const kb::scene::SceneRuntimeHotPathReport hotPath = runtime.HotPathReport();
        RecordProfilerRegions(profiler.Regions(), frame, { .updateNanoseconds = hotPath.runtimeUpdateNanoseconds,
            .transformSyncNanoseconds = hotPath.runtimeTransformSyncNanoseconds, .systems = runtime.LastEcsProfilerTrace().systemCounters });
        profiler.FrameRegions().Capture(number, profiler.Regions().TimelineEvents());
        profiler.Regions().ResetTimeline();
    }
    // Its own cost: this sampling and what the profiler took outside it since the last frame.
    const double outsideSampling = profiler.TakeOwnCost();
    if (selection.AreaMeasured(ProfilerArea::Cpu)) {
        Publish(profiler, ProfilerBuiltIn::ProfilerCost,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sampleStart).count() + outsideSampling);
    }
    profiler.Recorder().Capture(profiler.Registry(), number,
        static_cast<double>(ProfilerCounters::NowNanoseconds() - profiler.RecordingStartNanoseconds()) / kNanosecondsPerMillisecond);
    // A recording that filled up is written out and starts over, so nothing is lost silently.
    if (profiler.Recorder().Full()) {
        profiler.Panel().lastSaved = ProfilerFiles::SaveRecording(profiler);
        profiler.Recorder().Clear();
    }
    constexpr std::uint64_t kDiagnoseEvery = 15U;
    if (number % kDiagnoseEvery == 1U) profiler.Diagnose();
}

} // namespace kb::editor
