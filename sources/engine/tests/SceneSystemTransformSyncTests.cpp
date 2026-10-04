#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/ecs/World.hpp"
#include "engine/library/EngineLibraryCommandBatch.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <flecs.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

struct SceneSystemCounters {
    int created = 0;
    int updated = 0;
    int fixedUpdated = 0;
    int destroyed = 0;
};

class RuntimeScaleFixedSystem final : public kb::scene::SceneSystem {
public:
    explicit RuntimeScaleFixedSystem(std::size_t& ticks) noexcept : ticks_(ticks) {}

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }
    void OnFixedUpdate(kb::scene::SceneSystemContext&) override { ++ticks_; }

private:
    std::size_t& ticks_;
};

[[nodiscard]] bool EnvironmentFlagEnabled(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&value, &size, name) != 0) {
        return false;
    }
    const bool enabled = value != nullptr && value[0] == '1' && value[1] == '\0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
}

void RunSceneRuntimeScaleBenchmark() {
    const bool forceAnimatorScan = EnvironmentFlagEnabled("KB_SCENE_RUNTIME_SCALE_FORCE_ANIMATOR_SCAN");
    const bool forceTopologyRebuild = EnvironmentFlagEnabled("KB_SCENE_RUNTIME_SCALE_FORCE_TOPO_REBUILD");
    const bool largeScale = EnvironmentFlagEnabled("KB_SCENE_RUNTIME_SCALE_LARGE");
    std::size_t fixedTicks = 0U;
    kb::scene::Scene scene;
    if (largeScale) {
        scene.Runtime().SetPlaying(true);
        scene.Runtime().SetEcsProfilerEnabled(true);
        kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    } else {
        scene.Runtime().AddSceneSystem(std::make_unique<RuntimeScaleFixedSystem>(fixedTicks));
        scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
            .fixedDeltaSeconds = 1.0F / 60.0F,
            .maxFrameDeltaSeconds = 0.25F,
            .maxFixedStepsPerFrame = 1U,
        });
    }
    constexpr std::array<std::size_t, 4U> normalTargets{ 1'000U, 3'000U, 10'000U, 30'000U };
    constexpr std::array<std::size_t, 3U> largeTargets{ 100'000U, 300'000U, 500'000U };
    const std::span<const std::size_t> targets = largeScale
        ? std::span<const std::size_t>{ largeTargets }
        : std::span<const std::size_t>{ normalTargets };
    std::size_t created = 0U;
    kb::scene::SceneEntity firstEntity;
    const auto milliseconds = [](std::uint64_t nanoseconds) {
        return static_cast<double>(nanoseconds) / 1'000'000.0;
    };
    std::cout << std::fixed << std::setprecision(3);
    for (const std::size_t target : targets) {
        const auto spawnStart = std::chrono::steady_clock::now();
        while (created < target) {
            kb::scene::SceneObjectDesc object{ .name = "Runtime Scale Cube" };
            if (largeScale) {
                const float radius = 0.65F * std::sqrt(static_cast<float>(created));
                const float angle = static_cast<float>(created) * 2.39996323F;
                object.transform.localPosition = kb::scene::Vec3{ std::cos(angle) * radius, 0.0F, std::sin(angle) * radius };
                object.transform.localScale = kb::scene::Vec3{ 0.45F, 0.45F, 0.45F };
            } else {
                object.transform.localPosition.x = static_cast<float>(created % 1000U);
            }
            const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(object));
            kb::tests::Require(entity.IsValid(), "Scale benchmark failed to create a scene entity");
            if (created == 0U) {
                firstEntity = entity;
            }
            scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
                .meshAssetId = 1U,
                .materialAssetId = 2U,
                .castsShadow = !largeScale,
            });
            kb::tests::Require(scene.Components().MeshRenderers().Has(entity), "Scale benchmark lost a mesh renderer");
            if (created == 0U && forceAnimatorScan) {
                scene.Components().Animators().Set(entity, kb::scene::Animator{ .enabled = false });
                kb::tests::Require(scene.Components().Animators().Has(entity), "Scale benchmark lost its disabled animator");
            }
            if (created % 1'000U == 0U) {
                kb::scene::LightComponent light{};
                light.castsShadow = !largeScale;
                scene.Components().Lights().Set(entity, light);
            }
            ++created;
        }
        const double spawnMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - spawnStart).count();
        kb::tests::Require(scene.Entities().Count() == target, "Scale benchmark live count differs from created count");
        const std::uint64_t topologyBuildsBefore = scene.Runtime().HotPathReport().transformTopologicalBatchBuildCount;
        if (forceTopologyRebuild) {
            scene.Entities().SetName(firstEntity, "Runtime Scale Renamed Root");
        }
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        const kb::scene::SceneRuntimeHotPathReport growing = scene.Runtime().HotPathReport();
        std::array<double, 5U> staticUpdateMs{};
        std::array<double, 5U> staticCaptureMs{};
        std::array<double, 5U> staticSyncMs{};
        for (std::size_t sample = 0U; sample < staticUpdateMs.size(); ++sample) {
            static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
            const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
            staticUpdateMs[sample] = milliseconds(report.runtimeUpdateNanoseconds);
            staticCaptureMs[sample] = milliseconds(report.runtimeFixedCaptureStartNanoseconds + report.runtimeFixedCaptureEndNanoseconds);
            staticSyncMs[sample] = milliseconds(report.runtimeTransformSyncNanoseconds);
        }
        std::ranges::sort(staticUpdateMs);
        std::ranges::sort(staticCaptureMs);
        std::ranges::sort(staticSyncMs);
        std::array<double, 5U> noFixedUpdateMs{};
        std::array<double, 5U> worldProgressMs{};
        for (std::size_t sample = 0U; sample < noFixedUpdateMs.size(); ++sample) {
            static_cast<void>(scene.Runtime().Update(0.0F));
            noFixedUpdateMs[sample] = milliseconds(scene.Runtime().HotPathReport().runtimeUpdateNanoseconds);
            const auto progressStart = std::chrono::steady_clock::now();
            static_cast<void>(scene.Runtime().EcsWorld().Progress(0.0F));
            worldProgressMs[sample] = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - progressStart).count();
        }
        std::ranges::sort(noFixedUpdateMs);
        std::ranges::sort(worldProgressMs);
        std::cout << "scene_runtime_scale,entities=" << target
                  << ",large_scale=" << (largeScale ? 1 : 0)
                  << ",forced_animator_scan=" << (forceAnimatorScan ? 1 : 0)
                  << ",forced_topology_rebuild=" << (forceTopologyRebuild ? 1 : 0)
                  << ",topology_full_builds_before=" << topologyBuildsBefore
                  << ",topology_full_builds=" << growing.transformTopologicalBatchBuildCount
                  << ",fixed_ticks=" << fixedTicks
                  << ",spawn_ms=" << spawnMs
                  << ",growing_update_ms=" << milliseconds(growing.runtimeUpdateNanoseconds)
                  << ",growing_capture_ms=" << milliseconds(growing.runtimeFixedCaptureStartNanoseconds + growing.runtimeFixedCaptureEndNanoseconds)
                  << ",growing_sync_ms=" << milliseconds(growing.runtimeTransformSyncNanoseconds)
                  << ",static_update_median_ms=" << staticUpdateMs[staticUpdateMs.size() / 2U]
                  << ",static_capture_median_ms=" << staticCaptureMs[staticCaptureMs.size() / 2U]
                  << ",static_sync_median_ms=" << staticSyncMs[staticSyncMs.size() / 2U]
                  << ",no_fixed_update_median_ms=" << noFixedUpdateMs[noFixedUpdateMs.size() / 2U]
                  << ",world_progress_median_ms=" << worldProgressMs[worldProgressMs.size() / 2U] << '\n';
    }
    if (!largeScale || !EnvironmentFlagEnabled("KB_SCENE_RUNTIME_SCALE_PACED_TAIL")) {
        return;
    }

    using Clock = std::chrono::steady_clock;
    constexpr double entitiesPerSecond = 1'000.0;
    constexpr double frameBudgetMs = 1'000.0 / 60.0;
    constexpr auto tailDuration = std::chrono::seconds{ 10 };
    constexpr auto diagnosticHold = std::chrono::seconds{ 2 };
    const std::size_t firstTailEntity = created;
    const auto started = Clock::now();
    auto previousFrame = started;
    auto nextFrame = started;
    auto lastReport = started;
    auto stopTime = Clock::time_point{};
    const auto framePeriod = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{ 1.0 / 120.0 });
    std::vector<double> frameMs;
    frameMs.reserve(256U);
    std::size_t lastReportedCount = created;
    std::size_t frameCount = 0U;
    double spawnSumMs = 0.0;
    double updateSumMs = 0.0;
    double syncSumMs = 0.0;
    double slowSeconds = 0.0;
    bool spawnStopped = false;
    while (true) {
        const auto frameStart = Clock::now();
        const double elapsedSeconds = std::chrono::duration<double>(frameStart - started).count();
        if (spawnStopped && frameStart - stopTime >= diagnosticHold) {
            break;
        }
        const double deltaSeconds = std::chrono::duration<double>(frameStart - previousFrame).count();
        if (frameStart != started) {
            frameMs.push_back(deltaSeconds * 1'000.0);
        }
        previousFrame = frameStart;
        const std::size_t scheduled = firstTailEntity + std::min<std::size_t>(
            10'000U, static_cast<std::size_t>(elapsedSeconds * entitiesPerSecond));
        const std::size_t toCreate = spawnStopped ? 0U : std::min<std::size_t>(scheduled - created, 5'000U);
        const auto spawnStart = Clock::now();
        for (std::size_t count = 0U; count < toCreate; ++count) {
            kb::scene::SceneObjectDesc object{ .name = "Runtime Scale Tail Cube" };
            const float radius = 0.65F * std::sqrt(static_cast<float>(created));
            const float angle = static_cast<float>(created) * 2.39996323F;
            object.transform.localPosition = kb::scene::Vec3{ std::cos(angle) * radius, 0.0F, std::sin(angle) * radius };
            object.transform.localScale = kb::scene::Vec3{ 0.45F, 0.45F, 0.45F };
            const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(object));
            kb::tests::Require(entity.IsValid(), "Paced scale failed to create a scene entity");
            scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
                .meshAssetId = 1U,
                .materialAssetId = 2U,
                .castsShadow = false,
            });
            kb::tests::Require(scene.Components().MeshRenderers().Has(entity), "Paced scale lost a mesh renderer");
            if (created % 1'000U == 0U) {
                kb::scene::LightComponent light{};
                light.castsShadow = false;
                scene.Components().Lights().Set(entity, light);
                kb::tests::Require(scene.Components().Lights().Has(entity), "Paced scale lost a light");
            }
            ++created;
        }
        if (!spawnStopped && frameStart - started >= tailDuration && created == firstTailEntity + 10'000U) {
            spawnStopped = true;
            stopTime = Clock::now();
            const double stopSeconds = std::chrono::duration<double>(stopTime - started).count();
            std::cout << "scene_runtime_paced_tail_stop,reason=target,seconds=" << stopSeconds
                      << ",created=" << created - firstTailEntity
                      << ",actual_per_second=" << static_cast<double>(created - firstTailEntity) / stopSeconds
                      << ",live=" << scene.Entities().Count() << '\n';
        }
        spawnSumMs += std::chrono::duration<double, std::milli>(Clock::now() - spawnStart).count();
        static_cast<void>(scene.Runtime().Update(static_cast<float>(std::min(deltaSeconds, 1.0 / 15.0))));
        const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
        updateSumMs += milliseconds(report.runtimeUpdateNanoseconds);
        syncSumMs += milliseconds(report.runtimeTransformSyncNanoseconds);
        ++frameCount;
        const auto now = Clock::now();
        const double reportSeconds = std::chrono::duration<double>(now - lastReport).count();
        if (reportSeconds >= 1.0) {
            kb::tests::Require(!frameMs.empty(), "Paced scale produced no frame samples");
            std::ranges::sort(frameMs);
            const double p50 = frameMs[frameMs.size() / 2U];
            const double p95 = frameMs[(frameMs.size() * 95U + 99U) / 100U - 1U];
            const std::size_t live = scene.Entities().Count();
            kb::tests::Require(live == created, "Paced scale live count differs from created count");
            if (!spawnStopped) {
                slowSeconds = p95 > frameBudgetMs ? slowSeconds + reportSeconds : 0.0;
                if (slowSeconds >= 5.0) {
                    spawnStopped = true;
                    stopTime = now;
                    const double stopSeconds = std::chrono::duration<double>(stopTime - started).count();
                    std::cout << "scene_runtime_paced_tail_stop,reason=p95,seconds=" << stopSeconds
                              << ",created=" << created - firstTailEntity
                              << ",actual_per_second=" << static_cast<double>(created - firstTailEntity) / stopSeconds
                              << ",live=" << live << '\n';
                }
            }
            std::cout << "scene_runtime_paced_tail,seconds=" << elapsedSeconds
                      << ",live=" << live
                      << ",rate=" << static_cast<double>(live - lastReportedCount) / reportSeconds
                      << ",fps=" << static_cast<double>(frameMs.size()) / reportSeconds
                      << ",frame_p50_ms=" << p50
                      << ",frame_p95_ms=" << p95
                      << ",spawn_avg_ms=" << spawnSumMs / static_cast<double>(frameCount)
                      << ",update_avg_ms=" << updateSumMs / static_cast<double>(frameCount)
                      << ",sync_avg_ms=" << syncSumMs / static_cast<double>(frameCount)
                      << ",slow_seconds=" << slowSeconds
                      << ",spawn_stopped=" << (spawnStopped ? 1 : 0) << '\n';
            lastReportedCount = live;
            lastReport = now;
            frameMs.clear();
            frameCount = 0U;
            spawnSumMs = updateSumMs = syncSumMs = 0.0;
        }
        nextFrame += framePeriod;
        if (nextFrame < now) {
            nextFrame = now;
        }
        std::this_thread::sleep_until(nextFrame);
    }
}

void RunSceneRuntimeHeadlessStress() {
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t targetEntities = 1'000'000U;
    constexpr std::size_t maximumCatchUpPerFrame = 5'000U;
    constexpr double entitiesPerSecond = 1'000.0;
    constexpr double frameBudgetMs = 1000.0 / 60.0;
    constexpr auto diagnosticHold = std::chrono::seconds{ 30 };
    std::size_t fixedTicks = 0U;
    kb::scene::Scene scene;
    scene.Runtime().SetPlaying(true);
    scene.Runtime().SetEcsProfilerEnabled(true);
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    kb::tests::Require(scene.Runtime().IsPlaying() && scene.Runtime().EcsProfilerEnabled() &&
            kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
        "Headless stress did not initialize the Play runtime and lighting settings");
    const bool fixedStepEnabled = EnvironmentFlagEnabled("KB_SCENE_RUNTIME_STRESS_FIXED");
    if (fixedStepEnabled) {
        scene.Runtime().AddSceneSystem(std::make_unique<RuntimeScaleFixedSystem>(fixedTicks));
        scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
            .fixedDeltaSeconds = 1.0F / 60.0F,
            .maxFrameDeltaSeconds = 1.0F / 15.0F,
            .maxFixedStepsPerFrame = kb::scene::kSceneRuntimeDefaultMaxFixedStepsPerFrame,
        });
    }
    std::error_code error;
    const auto runId = std::chrono::system_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::current_path() / "Saved" / "Logs"
        / ("scene-runtime-headless-stress-" + std::to_string(runId) + ".csv");
    std::filesystem::create_directories(path.parent_path(), error);
    kb::tests::Require(!error, "Could not create the headless stress log directory");
    std::ofstream log{ path, std::ios::out | std::ios::trunc };
    kb::tests::Require(log.is_open(), "Could not open the headless stress log");
    std::cout << "headless_stress_log=" << path.string() << std::endl;
    log << "seconds,live_entities,scheduled,actual_per_second,fps,frame_p50_ms,frame_p95_ms,"
           "spawn_avg_ms,runtime_update_avg_ms,transform_sync_avg_ms,fixed_capture_avg_ms,fixed_ticks,"
           "fixed_step_enabled,slow_seconds,spawn_stopped\n";
    log.flush();
    const auto clockSeconds = [] {
        return std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
    };
    std::atomic<std::int64_t> heartbeat{ clockSeconds() };
    std::jthread watchdog{ [&heartbeat, &path, &clockSeconds](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds{ 1 });
            if (clockSeconds() - heartbeat.load(std::memory_order_relaxed) > 20) {
                std::ofstream timedOut{ path, std::ios::app };
                timedOut << "# watchdog_timeout_seconds=20\n";
                timedOut.flush();
                std::_Exit(EXIT_FAILURE);
            }
        }
    } };
    std::vector<double> frameMs;
    frameMs.reserve(256U);
    double spawnSumMs = 0.0;
    double updateSumMs = 0.0;
    double syncSumMs = 0.0;
    double captureSumMs = 0.0;
    std::size_t updateSampleCount = 0U;
    std::size_t created = 0U;
    std::size_t lastReportedCount = 0U;
    double slowSeconds = 0.0;
    bool spawnStopped = false;
    const auto started = Clock::now();
    auto lastReport = started;
    auto previousFrame = started;
    auto stopTime = Clock::time_point{};
    const auto framePeriod = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{ 1.0 / 120.0 });
    auto nextFrame = started;
    while (true) {
        const auto frameStart = Clock::now();
        heartbeat.store(clockSeconds(), std::memory_order_relaxed);
        const double deltaSeconds = std::chrono::duration<double>(frameStart - previousFrame).count();
        if (frameStart != started) {
            frameMs.push_back(deltaSeconds * 1000.0);
        }
        previousFrame = frameStart;
        const double seconds = std::chrono::duration<double>(frameStart - started).count();
        const std::size_t scheduled = std::min(targetEntities, static_cast<std::size_t>(seconds * entitiesPerSecond));
        const std::size_t toCreate = spawnStopped ? 0U : std::min(scheduled - created, maximumCatchUpPerFrame);
        const auto spawnStart = Clock::now();
        for (std::size_t count = 0U; count < toCreate; ++count) {
            const float radius = 0.65F * std::sqrt(static_cast<float>(created));
            const float angle = static_cast<float>(created) * 2.39996323F;
            kb::scene::SceneObjectDesc object{ .name = "Runtime Stress Cube" };
            object.transform.localPosition = kb::scene::Vec3{ std::cos(angle) * radius, 0.0F, std::sin(angle) * radius };
            object.transform.localScale = kb::scene::Vec3{ 0.45F, 0.45F, 0.45F };
            const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(object));
            kb::tests::Require(entity.IsValid(), "Headless stress failed to create a live scene entity");
            scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
                .meshAssetId = 1U,
                .materialAssetId = 2U,
                .castsShadow = false,
            });
            kb::tests::Require(scene.Components().MeshRenderers().Has(entity), "Headless stress failed to set a mesh renderer");
            if (created % 1'000U == 0U) {
                kb::scene::LightComponent light{};
                light.kind = kb::scene::LightKind::Point;
                light.castsShadow = false;
                scene.Components().Lights().Set(entity, light);
                kb::tests::Require(scene.Components().Lights().Has(entity), "Headless stress failed to set a point light");
            }
            ++created;
        }
        spawnSumMs += std::chrono::duration<double, std::milli>(Clock::now() - spawnStart).count();
        static_cast<void>(scene.Runtime().Update(static_cast<float>(std::min(deltaSeconds, 1.0 / 15.0))));
        heartbeat.store(clockSeconds(), std::memory_order_relaxed);
        const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
        updateSumMs += static_cast<double>(report.runtimeUpdateNanoseconds) / 1'000'000.0;
        syncSumMs += static_cast<double>(report.runtimeTransformSyncNanoseconds) / 1'000'000.0;
        captureSumMs += static_cast<double>(report.runtimeFixedCaptureStartNanoseconds + report.runtimeFixedCaptureEndNanoseconds) / 1'000'000.0;
        ++updateSampleCount;
        const auto now = Clock::now();
        const double reportSeconds = std::chrono::duration<double>(now - lastReport).count();
        if (reportSeconds >= 1.0) {
            kb::tests::Require(!frameMs.empty(), "Headless stress produced no frame samples");
            std::ranges::sort(frameMs);
            const double p50 = frameMs[frameMs.size() / 2U];
            const double p95 = frameMs[(frameMs.size() * 95U + 99U) / 100U - 1U];
            const std::size_t live = scene.Entities().Count();
            kb::tests::Require(live == created, "Headless stress live count differs from created count");
            const double fps = static_cast<double>(frameMs.size()) / reportSeconds;
            if (!spawnStopped) {
                slowSeconds = p95 > frameBudgetMs ? slowSeconds + reportSeconds : 0.0;
                if (slowSeconds >= 5.0 || created == targetEntities) {
                    spawnStopped = true;
                    stopTime = now;
                }
            }
            log << std::fixed << std::setprecision(3)
                << seconds << ',' << live << ',' << scheduled << ','
                << static_cast<double>(live - lastReportedCount) / reportSeconds << ','
                << fps << ',' << p50 << ',' << p95 << ','
                << spawnSumMs / static_cast<double>(updateSampleCount) << ','
                << updateSumMs / static_cast<double>(updateSampleCount) << ','
                << syncSumMs / static_cast<double>(updateSampleCount) << ','
                << captureSumMs / static_cast<double>(updateSampleCount) << ','
                << fixedTicks << ',' << (fixedStepEnabled ? 1 : 0) << ',' << slowSeconds << ','
                << (spawnStopped ? 1 : 0) << '\n';
            log.flush();
            std::cout << "headless_stress,seconds=" << seconds << ",live=" << live
                      << ",rate=" << static_cast<double>(live - lastReportedCount) / reportSeconds
                      << ",fps=" << fps << ",p95_ms=" << p95
                      << ",stopped=" << spawnStopped << '\n';
            lastReportedCount = live;
            lastReport = now;
            frameMs.clear();
            spawnSumMs = 0.0;
            updateSumMs = syncSumMs = captureSumMs = 0.0;
            updateSampleCount = 0U;
        }
        if (spawnStopped && now - stopTime >= diagnosticHold) {
            break;
        }
        nextFrame += framePeriod;
        if (nextFrame < now) {
            nextFrame = now;
        }
        std::this_thread::sleep_until(nextFrame);
    }
}

struct MeshRendererProxyStats {
    std::size_t visited = 0U;
    bool sawVisibleMesh = false;
    bool sawHiddenMesh = false;
    float visibleTranslationX = 0.0F;
};

struct CameraProxyStats {
    std::size_t visited = 0U;
    bool sawPrimary = false;
    float translationX = 0.0F;
};

struct LightProxyStats {
    std::size_t visited = 0U;
    bool sawPoint = false;
    float translationX = 0.0F;
};

struct PhysicsBodyStats {
    std::size_t visited = 0U;
    bool sawDynamic = false;
    bool sawSphere = false;
    float localPositionX = 0.0F;
};

void AccumulateMeshRendererProxyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::WorldTransformAffine3x4& worldTransform,
    const kb::scene::MeshRendererComponent& renderer,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<MeshRendererProxyStats*>(context);
    ++stats.visited;
    if (renderer.meshAssetId == 42U) {
        stats.sawVisibleMesh = true;
        stats.visibleTranslationX = worldTransform.values[9];
    } else if (renderer.meshAssetId == 77U) {
        stats.sawHiddenMesh = true;
    }
}

void AccumulateCameraProxyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::WorldTransformAffine3x4& worldTransform,
    const kb::scene::CameraComponent& camera,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<CameraProxyStats*>(context);
    ++stats.visited;
    if (camera.primary) {
        stats.sawPrimary = true;
        stats.translationX = worldTransform.values[9];
    }
}

void AccumulateLightProxyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::WorldTransformAffine3x4& worldTransform,
    const kb::scene::LightComponent& light,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<LightProxyStats*>(context);
    ++stats.visited;
    if (light.kind == kb::scene::LightKind::Point) {
        stats.sawPoint = true;
        stats.translationX = worldTransform.values[9];
    }
}

void AccumulatePhysicsBodyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    const kb::scene::RigidbodyComponent& rigidbody,
    const kb::scene::ColliderComponent& collider,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<PhysicsBodyStats*>(context);
    ++stats.visited;
    stats.sawDynamic = stats.sawDynamic || rigidbody.bodyType == kb::scene::RigidbodyBodyType::Dynamic;
    stats.sawSphere = stats.sawSphere || collider.shape == kb::scene::ColliderShape::Sphere;
    stats.localPositionX = transform.localPosition.x;
}

class MoveEntitySceneSystem final : public kb::scene::SceneSystem {
public:
    MoveEntitySceneSystem(SceneSystemCounters& counters, kb::scene::SceneEntity entity, kb::scene::Vec3 targetPosition) noexcept
        : counters_(counters)
        , entity_(entity)
        , targetPosition_(targetPosition) {}

    void OnCreate(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++counters_.created;
    }

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.updated;
        kb::scene::TransformComponent transform = context.Transforms().Get(entity_);
        transform.localPosition = targetPosition_;
        context.Transforms().Set(entity_, transform);
    }

    void OnDestroy(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++counters_.destroyed;
    }

private:
    SceneSystemCounters& counters_;
    kb::scene::SceneEntity entity_{};
    kb::scene::Vec3 targetPosition_{};
};

class CountingFixedSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit CountingFixedSceneSystem(SceneSystemCounters& counters) noexcept
        : counters_(counters) {}

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.updated;
        lastVariableDeltaSeconds_ = context.DeltaSeconds();
    }

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.fixedUpdated;
        lastFixedDeltaSeconds_ = context.DeltaSeconds();
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

    [[nodiscard]] float LastVariableDeltaSeconds() const noexcept { return lastVariableDeltaSeconds_; }
    [[nodiscard]] float LastFixedDeltaSeconds() const noexcept { return lastFixedDeltaSeconds_; }

private:
    SceneSystemCounters& counters_;
    float lastVariableDeltaSeconds_ = 0.0F;
    float lastFixedDeltaSeconds_ = 0.0F;
};

class RemovableSceneSystem final : public kb::scene::SceneSystem {
public:
    RemovableSceneSystem(SceneSystemCounters& counters, bool fixed, bool throwOnDestroy = false) noexcept
        : counters_(counters)
        , fixed_(fixed)
        , throwOnDestroy_(throwOnDestroy) {}

    void OnCreate(kb::scene::SceneSystemContext&) override { ++counters_.created; }
    void OnUpdate(kb::scene::SceneSystemContext&) override { ++counters_.updated; }
    void OnDestroy(kb::scene::SceneSystemContext&) override {
        ++counters_.destroyed;
        if (throwOnDestroy_) {
            throw std::runtime_error("remove lifecycle probe");
        }
    }
    [[nodiscard]] bool RequiresFixedStep() const override { return fixed_; }

private:
    SceneSystemCounters& counters_;
    bool fixed_ = false;
    bool throwOnDestroy_ = false;
};

void RunSceneSystemHandleRemovalTest() {
    SceneSystemCounters firstCounters;
    SceneSystemCounters secondCounters;
    SceneSystemCounters throwingCounters;
    kb::scene::Scene firstScene;
    kb::scene::Scene secondScene;

    const kb::scene::SceneSystemHandle first = firstScene.Runtime().AddSceneSystem(
        std::make_unique<RemovableSceneSystem>(firstCounters, true));
    const kb::scene::SceneSystemHandle second = secondScene.Runtime().AddSceneSystem(
        std::make_unique<RemovableSceneSystem>(secondCounters, false));
    kb::tests::Require(first.IsValid() && second.IsValid() && first != second,
        "Scene-system handles must encode distinct scheduler lifetimes");
    kb::tests::Require(!secondScene.Runtime().RemoveSceneSystem(first)
            && firstScene.Runtime().HasSceneSystem(first)
            && secondScene.Runtime().HasSceneSystem(second),
        "A foreign scene-system handle affected a different scheduler");
    kb::tests::Require(firstScene.Runtime().RemoveSceneSystem(first)
            && firstCounters.destroyed == 1
            && !firstScene.Runtime().HasSceneSystem(first),
        "Scene-system removal did not run OnDestroy exactly once");

    const kb::scene::SceneSystemHandle throwing = firstScene.Runtime().AddSceneSystem(
        std::make_unique<RemovableSceneSystem>(throwingCounters, false, true));
    kb::tests::Require(!firstScene.Runtime().RemoveSceneSystem(first)
            && firstCounters.destroyed == 1
            && firstScene.Runtime().HasSceneSystem(throwing),
        "A stale scene-system handle destroyed a later system or repeated OnDestroy");
    kb::tests::Require(firstScene.Runtime().RemoveSceneSystem(throwing)
            && throwingCounters.destroyed == 1
            && !firstScene.Runtime().HasSceneSystem(throwing),
        "A throwing OnDestroy escaped removal or retained its scene system");

    firstScene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.01F,
        .maxFrameDeltaSeconds = 0.1F,
        .maxFixedStepsPerFrame = 4U,
    });
    static_cast<void>(firstScene.Runtime().Update(0.05F));
    kb::tests::Require(firstScene.Runtime().LastFixedStepCount() == 0U,
        "Removing the final fixed-step scene system did not recompute scheduler demand");
    kb::tests::Require(secondScene.Runtime().RemoveSceneSystem(second) && secondCounters.destroyed == 1,
        "The independently owned scene system could not be removed");
}

class FixedMoveSceneSystem final : public kb::scene::SceneSystem {
public:
    FixedMoveSceneSystem(kb::scene::SceneEntity entity, kb::scene::Vec3 targetPosition) noexcept
        : entity_(entity)
        , targetPosition_(targetPosition) {}

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        kb::scene::TransformComponent transform = context.Transforms().Get(entity_);
        transform.localPosition = targetPosition_;
        context.Transforms().Set(entity_, transform);
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

private:
    kb::scene::SceneEntity entity_{};
    kb::scene::Vec3 targetPosition_{};
};

// LIB-089: records `entity`'s world.x AT THE MOMENT OnUpdate() runs — used
// to prove/disprove whether a same-Update()-call, earlier scene system's
// transform change is visible mid-pass via world*, per the timing contract
// now documented on kb::scene::SceneRuntime::SynchronizeTransforms().
class ObserveWorldXSceneSystem final : public kb::scene::SceneSystem {
public:
    ObserveWorldXSceneSystem(kb::scene::SceneEntity entity, float& observedWorldX) noexcept
        : entity_(entity)
        , observedWorldX_(observedWorldX) {}

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        observedWorldX_ = context.Transforms().Get(entity_).worldPosition.x;
    }

private:
    kb::scene::SceneEntity entity_{};
    float& observedWorldX_;
};

// LIB-089: same as ObserveWorldXSceneSystem, but observing from
// OnFixedUpdate() (RequiresFixedStep()==true) instead of OnUpdate() — used
// to prove a fixed-step (physics-shaped) scene system DOES see fresh,
// synced world* data automatically, unlike a plain variable-update system.
class ObserveWorldXFixedSceneSystem final : public kb::scene::SceneSystem {
public:
    ObserveWorldXFixedSceneSystem(kb::scene::SceneEntity entity, float& observedWorldX) noexcept
        : entity_(entity)
        , observedWorldX_(observedWorldX) {}

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        observedWorldX_ = context.Transforms().Get(entity_).worldPosition.x;
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

private:
    kb::scene::SceneEntity entity_{};
    float& observedWorldX_;
};

void RunSceneSystemTransformSyncTest() {
    SceneSystemCounters counters;

    {
        kb::scene::Scene scene;

        kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Parent",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            },
        });

        kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Child",
            .parent = parent,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
            },
        });

        scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
        kb::tests::Require(counters.created == 1, "Scene system OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
        kb::tests::Require(counters.updated == 1, "Scene system OnUpdate was not called");

        const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
        kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 12.0F), "Scene system transform changes were not synchronized in the same update");
    }

    kb::tests::Require(counters.destroyed == 1, "Scene system OnDestroy was not called");
}

// LIB-089: locks in the "scripts must self-sync across phases" half of the
// timing contract now documented on
// kb::scene::SceneRuntime::SynchronizeTransforms() — a transform change one
// scene system makes in OnUpdate() is NOT automatically visible via world*
// to a LATER scene system's OnUpdate() within the SAME Update() call
// (SceneSystemScheduler::Update only calls SynchronizeTransformHierarchy
// once, before the whole pass, and once more after the whole pass — never
// between two systems inside it), even though it genuinely IS visible once
// Update() itself returns (RunSceneSystemTransformSyncTest above already
// proves that half; this test contrasts the two).
void RunTransformSyncContractScriptsRequireExplicitSyncAcrossSystemsTest() {
    // Declared before `scene`: destructor order is the REVERSE of
    // declaration order, and MoveEntitySceneSystem::OnDestroy (which scene
    // owns and invokes during its own destructor) reads counters_ - a
    // stack-use-after-scope caught by AddressSanitizer when this was
    // declared after `scene` instead (counters destroyed first, then read
    // by scene's own, later, teardown).
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "ContractParent",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "ContractChild",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });
    // Establish a baseline synced world position: parent(1) + child(2) = 3.
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 3.0F), "Contract test fixture baseline world position must be 3 before the test proper");

    float observedChildWorldXDuringLaterSystem = -1.0F;
    // Registration order matters: MoveEntitySceneSystem must run BEFORE
    // ObserveWorldXSceneSystem within the same Update() call's scheduler
    // pass (SceneSystemScheduler::Update iterates systems_ in push_back
    // order).
    scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
    scene.Runtime().AddSceneSystem(std::make_unique<ObserveWorldXSceneSystem>(child.Entity(), observedChildWorldXDuringLaterSystem));

    static_cast<void>(scene.Runtime().Update(0.016F));

    kb::tests::Require(kb::tests::NearlyEqual(observedChildWorldXDuringLaterSystem, 3.0F),
        "LIB-089 contract: a same-pass, earlier scene system's transform change must NOT be visible via world* to a later scene system's OnUpdate() without an explicit SynchronizeTransforms() call");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 12.0F),
        "LIB-089 contract: after Update() returns, world* must reflect every change made during that call, even ones a same-pass later system could not see mid-pass");
}

// LIB-089: locks in the "physics gets fresh data automatically" half of the
// timing contract — a fixed-step scene system (RequiresFixedStep()==true,
// the shape JoltPhysicsSceneSystem has) sees a transform change made by an
// EARLIER, plain variable-update scene system in the SAME Update() call,
// with no manual sync call of its own, because SceneRuntimeService::Update
// calls SynchronizeTransformHierarchy immediately before entering the
// fixed-step loop (in addition to before/after every individual step).
void RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest() {
    // Declared before `scene` - see the identical reasoning on
    // RunTransformSyncContractScriptsRequireExplicitSyncAcrossSystemsTest
    // above (destructor order is the reverse of declaration order;
    // MoveEntitySceneSystem::OnDestroy reads counters_ during scene's own
    // teardown).
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FixedContractParent" });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "FixedContractChild",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });

    float observedChildWorldXDuringFixedUpdate = -1.0F;
    scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
    scene.Runtime().AddSceneSystem(std::make_unique<ObserveWorldXFixedSceneSystem>(child.Entity(), observedChildWorldXDuringFixedUpdate));

    static_cast<void>(scene.Runtime().Update(0.03F)); // Exceeds fixedDeltaSeconds, so at least one fixed step runs.

    kb::tests::Require(kb::tests::NearlyEqual(observedChildWorldXDuringFixedUpdate, 12.0F),
        "LIB-089 contract: a fixed-step scene system must see fresh, synced world* data automatically, including a change from an earlier same-Update()-call variable-update system, with no manual SynchronizeTransforms() call of its own");
}

// LIB-128: a fake, physics-shaped stand-in (RequiresFixedStep()==true) for
// the real JoltPhysicsSceneSystem - a SceneSystemScheduler-level ordering
// fact is provable without a real Jolt plugin, the same way this file's
// other Observe*FixedSceneSystem fakes already do for LIB-089.
class RecordingFixedPhysicsStandIn final : public kb::scene::SceneSystem {
public:
    explicit RecordingFixedPhysicsStandIn(kb::scene::SceneEntity entity = {}) noexcept
        : entity_(entity) {}

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        ++stepsRun;
        lastWrittenValue = stepsRun;
        if (entity_.IsValid()) {
            observedCommandValue = context.Transforms().Get(entity_).worldPosition.x;
        }
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

    int stepsRun = 0;
    int lastWrittenValue = 0;
    float observedCommandValue = 0.0F;

private:
    kb::scene::SceneEntity entity_{};
};

// LIB-128: a real installed ScriptRuntimeSceneSystem runs FixedTick in
// PreSimulation. A CommandBatch flushed there must be applied and its
// transform hierarchy synchronized before a Simulation-phase system reads
// it, independent of scene-system registration order.
void RunFixedTickCommandBufferRunsBeforePhysicsTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });

    constexpr kb::assets::AssetId kAsset{ 9700U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FixedTickCommandTarget" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto physicsStandIn = std::make_unique<RecordingFixedPhysicsStandIn>(object.Entity());
    RecordingFixedPhysicsStandIn* physicsView = physicsStandIn.get();
    scene.Runtime().AddSceneSystem(std::move(physicsStandIn));

    kb::script::ScriptRuntimeHost host{
        scene,
        kb::script::ScriptRuntimeHostOptions{
            .frameSettings = kb::script::ScriptRuntimeFrameSettings{ .fixedDeltaSeconds = 0.02F, .maxFixedStepsPerFrame = 4U },
        },
    };
    kb::tests::Require(host.Succeeded(), "LIB-128 timing contract test host did not initialize");

    int fixedTickCount = 0;
    bool commandBufferFlushed = false;
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&scene, object, &fixedTickCount, &commandBufferFlushed](kb::script::ScriptExecutionContext&) {
                           ++fixedTickCount;
                           kb::scene::TransformComponent transform = scene.Transforms().Get(object.Entity());
                           transform.localPosition.x = 42.0F;
                           kb::library::CommandBatch batch{ scene };
                           const kb::library::EntityHandle handle{ object.Entity(), scene.Id() };
                           kb::tests::Require(batch.Add<kb::scene::TransformComponent>(handle, transform),
                               "LIB-128 FixedTick command buffer failed to record the transform command");
                           commandBufferFlushed = batch.Flush().has_value();
                       }),
        "LIB-128 timing contract test FixedTick registration failed");

    kb::tests::Require(host.InstallSceneSystem(), "LIB-128 timing contract test scene system install failed");

    // dt == fixedDeltaSeconds exactly, so every Update() call consumes
    // exactly one fixed step with zero accumulator remainder - no
    // carry-over arithmetic to reason about.
    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(physicsView->stepsRun == 1, "LIB-128 timing contract test fixture: physics stand-in must have run exactly once");
    kb::tests::Require(fixedTickCount == 1, "LIB-128 timing contract test fixture: FixedTick must have dispatched exactly once");
    kb::tests::Require(commandBufferFlushed, "LIB-128 command buffer must flush successfully inside FixedTick");
    kb::tests::Require(kb::tests::NearlyEqual(physicsView->observedCommandValue, 42.0F),
        "LIB-128 contract: physics must observe a CommandBatch flushed by FixedTick in the SAME fixed step");
}

// LIB-128: installing ScriptRuntimeSceneSystem promotes its host fixed
// settings into SceneRuntimeFixedStepSettings. Both FixedTick and every
// Simulation-phase system consume that one accumulator, including pause and
// resume without fixed-step debt.
void RunFixedTickAndPhysicsShareSceneAccumulatorTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 8U,
    });

    auto physicsStandIn = std::make_unique<RecordingFixedPhysicsStandIn>();
    RecordingFixedPhysicsStandIn* physicsView = physicsStandIn.get();
    scene.Runtime().AddSceneSystem(std::move(physicsStandIn));

    // Installing the script system makes its configured fixed step the
    // scene's authoritative fixed step. Physics and FixedTick must consume
    // the same two 0.01s substeps from this 0.02s frame.
    kb::script::ScriptRuntimeHost host{
        scene,
        kb::script::ScriptRuntimeHostOptions{
            .frameSettings = kb::script::ScriptRuntimeFrameSettings{ .fixedDeltaSeconds = 0.01F, .maxFixedStepsPerFrame = 8U },
        },
    };
    kb::tests::Require(host.Succeeded(), "LIB-128 shared-accumulator test host did not initialize");

    constexpr kb::assets::AssetId kAsset{ 9701U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FixedTickCounter" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    int fixedTickCount = 0;
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&fixedTickCount](kb::script::ScriptExecutionContext&) {
                           ++fixedTickCount;
                       }),
        "LIB-128 shared-accumulator test FixedTick registration failed");
    kb::tests::Require(host.InstallSceneSystem(), "LIB-128 shared-accumulator test scene system install failed");

    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedStepSettings().fixedDeltaSeconds, 0.01F),
        "LIB-128 must expose one authoritative scene fixed delta after installing the script runtime");
    kb::tests::Require(physicsView->stepsRun == 2, "LIB-128 physics must consume the same two authoritative scene fixed steps as FixedTick");
    kb::tests::Require(fixedTickCount == 2, "LIB-128 FixedTick and physics step counts must remain exactly equal");

    scene.Runtime().SetPlaying(false);
    static_cast<void>(scene.Runtime().Update(0.10F));
    kb::tests::Require(physicsView->stepsRun == 2 && fixedTickCount == 2 && scene.Runtime().LastFixedStepCount() == 0U,
        "LIB-128 pausing the scene must freeze both FixedTick and physics at the shared accumulator boundary");
    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0.01F));
    kb::tests::Require(physicsView->stepsRun == 3 && fixedTickCount == 3,
        "LIB-128 resuming must produce one normal shared step without replaying paused time as fixed-step debt");
}

void RunSceneRuntimeFixedStepTest() {
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });

    auto system = std::make_unique<CountingFixedSceneSystem>(counters);
    CountingFixedSceneSystem* systemView = system.get();
    scene.Runtime().AddSceneSystem(std::move(system));

    static_cast<void>(scene.Runtime().Update(0.01F));
    kb::tests::Require(counters.updated == 1, "Variable scene update should run once even before a fixed step is due");
    kb::tests::Require(counters.fixedUpdated == 0, "Fixed scene update should wait until the accumulator reaches fixed dt");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.5F), "Fixed interpolation alpha should expose the partial accumulator");

    static_cast<void>(scene.Runtime().Update(0.03F));
    kb::tests::Require(counters.updated == 2, "Variable scene update should run once per frame");
    kb::tests::Require(counters.fixedUpdated == 2, "Fixed scene update should consume accumulated fixed steps");
    kb::tests::Require(scene.Runtime().LastFixedStepCount() == 2U, "Runtime should report fixed steps from the last frame");
    kb::tests::Require(kb::tests::NearlyEqual(systemView->LastVariableDeltaSeconds(), 0.03F), "Variable scene update should receive frame dt");
    kb::tests::Require(kb::tests::NearlyEqual(systemView->LastFixedDeltaSeconds(), 0.02F), "Fixed scene update should receive fixed dt");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.0F), "Fixed interpolation alpha should reset after exact fixed consumption");

    static_cast<void>(scene.Runtime().Update(1.0F));
    kb::tests::Require(scene.Runtime().LastFixedStepCount() == 4U, "Runtime should cap fixed steps per frame");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.0F), "Runtime should drop excess fixed time after max-step safety triggers");
}

// LIB-065: FrameIndex/FixedStepIndex are monotonic across the scene's
// whole lifetime (never reset per frame, unlike LastFixedStepCount) —
// reuses the exact fixture from RunSceneRuntimeFixedStepTest so the
// expected cumulative fixed-step counts are directly comparable.
void RunSceneRuntimeFrameAndPlayStateTest() {
    // Declared before `scene` for consistency with the fix on
    // RunTransformSyncContractScriptsRequireExplicitSyncAcrossSystemsTest
    // above - CountingFixedSceneSystem does not currently override
    // OnDestroy, so this specific ordering is not an active bug today, but
    // matches the safer pattern rather than relying on that staying true.
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    kb::tests::Require(scene.Runtime().IsPlaying(), "A scene must report IsPlaying() == true by default");
    scene.Runtime().SetPlaying(false);
    kb::tests::Require(!scene.Runtime().IsPlaying(), "SetPlaying(false) must be reflected by IsPlaying()");
    scene.Runtime().SetPlaying(true);
    kb::tests::Require(scene.Runtime().IsPlaying(), "SetPlaying(true) must be reflected by IsPlaying()");

    kb::tests::Require(scene.Runtime().FrameIndex() == 0U, "A freshly constructed scene must report FrameIndex() == 0 before any Update()");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 0U, "A freshly constructed scene must report FixedStepIndex() == 0 before any Update()");

    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });
    scene.Runtime().AddSceneSystem(std::make_unique<CountingFixedSceneSystem>(counters));

    static_cast<void>(scene.Runtime().Update(0.01F));
    kb::tests::Require(scene.Runtime().FrameIndex() == 1U, "FrameIndex must increment by exactly one per Update() call");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 0U, "FixedStepIndex must not advance before the accumulator reaches one fixed step");

    static_cast<void>(scene.Runtime().Update(0.03F));
    kb::tests::Require(scene.Runtime().FrameIndex() == 2U, "FrameIndex must keep incrementing across successive Update() calls");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 2U, "FixedStepIndex must accumulate the 2 fixed steps this frame consumed, on top of the previous 0");

    static_cast<void>(scene.Runtime().Update(1.0F));
    kb::tests::Require(scene.Runtime().FrameIndex() == 3U, "FrameIndex must reach 3 after a third Update() call");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 6U, "FixedStepIndex must accumulate the 4 capped fixed steps on top of the previous 2, never resetting like LastFixedStepCount does");
}

void RunSceneRuntimeReadSnapshotAndCommandQueueTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneRuntimeQueries workerView =
        static_cast<const kb::scene::Scene&>(scene).Runtime();
    const std::shared_ptr<const kb::scene::SceneRuntimeReadSnapshot> before =
        workerView.ReadSnapshot();
    kb::tests::Require(before != nullptr && before->revision == 0U &&
            before->playing && kb::tests::NearlyEqual(before->timeScale, 1.0F),
        "A fresh runtime must expose an immutable initial read snapshot");

    const bool invalidAccepted = scene.Runtime().EnqueueCommand(
        kb::scene::SceneRuntimeCommand{
            .kind = kb::scene::SceneRuntimeCommandKind::SetTimeScale,
            .timeScale = -1.0F,
        });
    kb::tests::Require(!invalidAccepted,
        "Runtime command queue must reject an invalid time scale before enqueueing it");

    bool enqueued = false;
    std::thread worker{ [&scene, &enqueued] {
        enqueued = scene.Runtime().EnqueueCommand(
                kb::scene::SceneRuntimeCommand{
                    .kind = kb::scene::SceneRuntimeCommandKind::SetPlaying,
                    .playing = false,
                }) && scene.Runtime().EnqueueCommand(
                kb::scene::SceneRuntimeCommand{
                    .kind = kb::scene::SceneRuntimeCommandKind::SetTimeScale,
                    .timeScale = 0.25F,
                }) && scene.Runtime().EnqueueCommand(
                kb::scene::SceneRuntimeCommand{
                    .kind = kb::scene::SceneRuntimeCommandKind::RequestQuit,
                });
    } };
    worker.join();
    kb::tests::Require(enqueued, "Worker failed to enqueue runtime commands");

    kb::tests::Require(scene.Runtime().IsPlaying() && !scene.Runtime().ShouldQuit(),
        "Queued commands must not mutate scene-owned state before Update drains them");
    static_cast<void>(scene.Runtime().Update(0.016F));

    const std::shared_ptr<const kb::scene::SceneRuntimeReadSnapshot> after =
        workerView.ReadSnapshot();
    kb::tests::Require(after != nullptr && after->revision > before->revision &&
            after->frameIndex == 1U && !after->playing && after->shouldQuit &&
            kb::tests::NearlyEqual(after->timeScale, 0.25F),
        "Runtime must publish the post-drain state as a new immutable snapshot");
    kb::tests::Require(before->revision == 0U && before->playing &&
            kb::tests::NearlyEqual(before->timeScale, 1.0F),
        "A worker-held older snapshot must remain immutable after the next publish");
}

void RunSceneRuntimeFixedInterpolationTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });

    kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Interpolated",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        },
    });
    // Interpolation is opt-in for an entity without a physics body.
    scene.Transforms().SetInterpolated(object.Entity(), true);
    const auto moveSystem = scene.Runtime().AddSceneSystem(std::make_unique<FixedMoveSceneSystem>(object.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));

    std::array<kb::scene::SceneEntity, 3U> otherArchetypes;
    for (std::size_t index = 0U; index < otherArchetypes.size(); ++index) {
        otherArchetypes[index] = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Interpolation neighbour",
            .transform = kb::scene::TransformComponent{ .localPosition = {100.0F + static_cast<float>(index), 0.0F, 0.0F} },
        });
    }
    scene.Components().Cameras().Set(otherArchetypes[0], kb::scene::CameraComponent{});
    scene.Components().MeshRenderers().Set(otherArchetypes[1], kb::scene::MeshRendererComponent{});

    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(object).localPosition.x, 10.0F), "Fixed system should write the current transform");
    std::optional<kb::scene::TransformComponent> interpolated = scene.Runtime().InterpolatedTransform(object.Entity());
    kb::tests::Require(interpolated.has_value(), "Runtime should expose an interpolated transform sample");
    kb::tests::Require(kb::tests::NearlyEqual(interpolated->localPosition.x, 0.0F), "Alpha 0 should expose the previous fixed transform sample");

    static_cast<void>(scene.Runtime().Update(0.01F));
    interpolated = scene.Runtime().InterpolatedTransform(object.Entity());
    kb::tests::Require(interpolated.has_value(), "Runtime should keep interpolation samples across frames without a fixed step");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.5F), "Runtime should expose half-step interpolation alpha");
    kb::tests::Require(kb::tests::NearlyEqual(interpolated->localPosition.x, 5.0F), "Interpolated transform should blend previous and current fixed samples");
    for (std::size_t index = 0U; index < otherArchetypes.size(); ++index) {
        const auto neighbour = scene.Runtime().InterpolatedTransform(otherArchetypes[index]);
        kb::tests::Require(neighbour.has_value() &&
            kb::tests::NearlyEqual(neighbour->localPosition.x, 100.0F + static_cast<float>(index)),
            "Sorting interpolation samples must preserve poses across different archetypes");
    }

    for (std::size_t step = 0U; step < 4U; ++step) {
        if (step == 2U) {
            scene.Components().MeshRenderers().Set(otherArchetypes[2], kb::scene::MeshRendererComponent{});
        }
        auto edited = scene.Transforms().Get(object);
        edited.localPosition.x = 20.0F + 2.0F * static_cast<float>(step);
        scene.Transforms().Set(object.Entity(), edited);
        static_cast<void>(scene.Runtime().Update(0.02F));
        interpolated = scene.Runtime().InterpolatedTransform(object.Entity());
        kb::tests::Require(interpolated.has_value() &&
            kb::tests::NearlyEqual(interpolated->localPosition.x, 15.0F + static_cast<float>(step)),
            "Reused interpolation storage must capture each step's current starting pose");
        for (std::size_t index = 0U; index < otherArchetypes.size(); ++index) {
            const auto neighbour = scene.Runtime().InterpolatedTransform(otherArchetypes[index]);
            kb::tests::Require(neighbour.has_value() &&
                kb::tests::NearlyEqual(neighbour->localPosition.x, 100.0F + static_cast<float>(index)),
                "Reusing interpolation indices must preserve poses when archetype row order changes");
        }
    }

    scene.Entities().Destroy(object.Entity());
    kb::tests::Require(!scene.Runtime().InterpolatedTransform(object.Entity()).has_value(),
        "Destroyed entities must not expose retained interpolation samples");
    kb::tests::Require(scene.Runtime().RemoveSceneSystem(moveSystem), "Fixed move system must detach");

    class ReplaceOnFixed final : public kb::scene::SceneSystem {
    public:
        kb::scene::SceneEntity entity;
        explicit ReplaceOnFixed(kb::scene::SceneEntity initial) : entity(initial) {}
        bool RequiresFixedStep() const override { return true; }
        void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
            auto& scene = context.GetScene();
            scene.Entities().Destroy(entity);
            entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
                .name = "Created during fixed simulation",
                .transform = kb::scene::TransformComponent{.localPosition = {42.0F, 0.0F, 0.0F}},
            });
        }
    };
    const auto initial = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Removed during fixed simulation",
        .transform = kb::scene::TransformComponent{.localPosition = {-17.0F, 0.0F, 0.0F}},
    });
    auto replacement = std::make_unique<ReplaceOnFixed>(initial);
    auto* replacementSystem = replacement.get();
    scene.Runtime().AddSceneSystem(std::move(replacement));
    static_cast<void>(scene.Runtime().Update(0.03F));
    kb::tests::Require(!scene.Runtime().InterpolatedTransform(initial).has_value(),
        "Entities removed inside fixed simulation must not remain in its final samples");
    interpolated = scene.Runtime().InterpolatedTransform(replacementSystem->entity);
    kb::tests::Require(interpolated.has_value() && kb::tests::NearlyEqual(interpolated->localPosition.x, 42.0F),
        "New generations created inside a fixed step must interpolate from their own initial pose");
    const auto previousGeneration = replacementSystem->entity;
    static_cast<void>(scene.Runtime().Update(0.02F));
    interpolated = scene.Runtime().InterpolatedTransform(replacementSystem->entity);
    kb::tests::Require(!scene.Runtime().InterpolatedTransform(previousGeneration).has_value() &&
            interpolated.has_value() && kb::tests::NearlyEqual(interpolated->localPosition.x, 42.0F),
        "Reused interpolation indices must rebuild when a fixed step replaces an entity generation");
}

void RunSceneTransformLookupLifetimeAndFallbackTest() {
    kb::scene::Scene scene;
    const kb::scene::Scene& readOnly = scene;
    auto& world = scene.Runtime().EcsWorld();
    const auto entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Transform lookup",
        .transform = kb::scene::TransformComponent{.localPosition = {17.0F, 0.0F, 0.0F}},
    });
    kb::tests::Require(scene.Transforms().TryGet({}) == nullptr && readOnly.Transforms().TryGet({}) == nullptr,
        "Scene transform lookup must reject an invalid handle");
    world.Remove<kb::scene::VisibilityComponent>(entity);
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().TryGet(entity)->localPosition.x, 17.0F) &&
        readOnly.Transforms().TryGet(entity) == scene.Transforms().TryGet(entity),
        "Scene transform lookup lost its native column after archetype migration");
    world.Remove<kb::scene::TransformComponent>(entity);
    kb::tests::Require(scene.Transforms().TryGet(entity) == nullptr && readOnly.Transforms().TryGet(entity) == nullptr,
        "Scene transform lookup retained a removed component");

    const auto component = world.Component<kb::scene::TransformComponent>();
    const auto base = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Inherited transform base",
        .transform = kb::scene::TransformComponent{.localPosition = {31.0F, 0.0F, 0.0F}},
    });
    ecs_add_pair(world.NativeHandle(), component, EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world.NativeHandle(), ecs_strip_generation(entity.Id()), EcsIsA, ecs_strip_generation(base.Id()));
    const auto* inherited = readOnly.Transforms().TryGet(entity);
    kb::tests::Require(inherited != nullptr && kb::tests::NearlyEqual(inherited->localPosition.x, 31.0F),
        "Scene transform lookup lost its inherited backend fallback");
    const kb::scene::TransformComponent backendPose{.localPosition = {43.0F, 0.0F, 0.0F}};
    ecs_set_id(world.NativeHandle(), ecs_strip_generation(entity.Id()), component, sizeof(backendPose), &backendPose);
    auto* backend = scene.Transforms().TryGet(entity);
    kb::tests::Require(backend != nullptr && kb::tests::NearlyEqual(backend->localPosition.x, 43.0F),
        "Mutable scene transform lookup lost an owned backend fallback");
    world.Set(entity, kb::scene::TransformComponent{.localPosition = {59.0F, 0.0F, 0.0F}});
    kb::tests::Require(kb::tests::NearlyEqual(readOnly.Transforms().TryGet(entity)->localPosition.x, 59.0F),
        "Scene transform lookup retained backend data after native component recreation");
    scene.Entities().Destroy(entity);
    const auto replacement = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{.name = "Lookup replacement"});
    kb::tests::Require(scene.Transforms().TryGet(entity) == nullptr && readOnly.Transforms().TryGet(entity) == nullptr &&
        scene.Transforms().TryGet(replacement) != nullptr,
        "Scene transform lookup accepted a destroyed generation after entity replacement");
}

void RunSceneRuntimeDestroyedDirtyTransformTest() {
    kb::scene::Scene scene;
    const auto parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Dirty hierarchy parent",
        .transform = kb::scene::TransformComponent{.localPosition = {10.0F, 0.0F, 0.0F}},
    });
    const auto child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Dirty hierarchy child", .parent = parent,
    });
    const auto doomed = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{.name = "Removed dirty root"});
    scene.Runtime().SynchronizeTransforms();
    auto pose = scene.Transforms().Get(doomed);
    pose.localPosition.x = 100.0F;
    scene.Transforms().Set(doomed, pose);
    scene.Entities().Destroy(doomed);
    const auto replacement = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Replacement root",
        .transform = kb::scene::TransformComponent{.localPosition = {42.0F, 0.0F, 0.0F}},
    });
    auto parentPose = scene.Transforms().Get(parent);
    parentPose.localPosition.x = 20.0F;
    scene.Transforms().Set(parent, parentPose);
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(!scene.Entities().IsAlive(doomed) &&
        kb::tests::NearlyEqual(scene.Transforms().Get(replacement).worldPosition.x, 42.0F) &&
        kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 20.0F),
        ("Removing a dirty root must preserve propagation for live generations and descendants: replacement=" +
            std::to_string(scene.Transforms().Get(replacement).worldPosition.x) + " child=" +
            std::to_string(scene.Transforms().Get(child).worldPosition.x)).c_str());
}

void RunSceneRuntimeMixedTransformWritesTest() {
    kb::scene::Scene scene;
    const auto apiParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "API parent"});
    const auto nativeParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Native parent"});
    const auto apiChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "API child", .parent = apiParent,
        .transform = kb::scene::TransformComponent{.localPosition = {2.0F, 0.0F, 0.0F}},
    });
    const auto nativeChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Native child", .parent = nativeParent,
        .transform = kb::scene::TransformComponent{.localPosition = {3.0F, 0.0F, 0.0F}},
    });
    scene.Runtime().SynchronizeTransforms();
    for (const float offset : {0.0F, 10.0F}) {
        auto apiPose = scene.Transforms().Get(apiParent);
        apiPose.localPosition.x = 15.0F + offset;
        scene.Transforms().Set(apiParent, apiPose);
        auto nativePose = scene.Transforms().Get(nativeParent);
        nativePose.localPosition.x = 25.0F + offset;
        nativePose.worldDirty = true;
        ++nativePose.localVersion;
        scene.Runtime().EcsWorld().Set(nativeParent.Entity(), nativePose);
        scene.Runtime().SynchronizeTransforms();
        kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(apiChild).worldPosition.x, 17.0F + offset) &&
            kb::tests::NearlyEqual(scene.Transforms().Get(nativeChild).worldPosition.x, 28.0F + offset),
            "Scene API writes must not hide native ECS transform writes in another hierarchy");
        const auto childVersion = scene.Transforms().Get(nativeChild).worldVersion;
        scene.Runtime().SynchronizeTransforms();
        kb::tests::Require(scene.Transforms().Get(nativeChild).worldVersion == childVersion &&
            scene.Runtime().HotPathReport().transformHierarchyInspectedCount == 0U,
            "Clean hierarchies must retain world versions without inspecting unchanged transforms");
    }
}

void RunSceneRuntimeTransformHotPathReportTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Path Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject hiddenChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hidden Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject cameraChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Camera Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 8.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject lightChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Light Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F },
        },
    });
    scene.Components().MeshRenderers().Set(child.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 42U, .materialAssetId = 5U });
    scene.Components().MeshRenderers().Set(hiddenChild.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 77U, .materialAssetId = 9U });
    scene.Components().Visibility().Set(hiddenChild.Entity(), kb::scene::VisibilityComponent{ .visible = false });
    scene.Components().Cameras().Set(cameraChild.Entity(), kb::scene::CameraComponent{ .primary = true });
    scene.Components().Lights().Set(lightChild.Entity(), kb::scene::LightComponent{ .kind = kb::scene::LightKind::Point, .intensity = 3.0F });

    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 7.0F), "Scene transform hot path did not update hierarchy without a virtual scene system");

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyUsesBatchPath, "Scene transform hot path should report the batch path");
    kb::tests::Require(report.transformHierarchyUsesKernelContract, "Scene transform hot path should report the kernel contract path");
    kb::tests::Require(!report.transformHierarchyUsesVirtualSceneSystem, "Scene transform hot path should not report virtual SceneSystem execution");
    kb::tests::Require(report.transformTopologicalBatchCount >= 2U, "Scene transform hot path should expose topological batches");
    kb::tests::Require(report.transformRenderProxyUpdateCount >= 2U, "Scene transform hot path should expose render-proxy update batching");
    kb::tests::Require(report.transformRenderProxyMeshRendererCount == 2U, "Scene transform hot path did not compact mesh render proxy indices");
    kb::tests::Require(report.transformRenderProxyVisibleMeshRendererCount == 1U, "Scene transform hot path did not compact visible mesh render proxy indices");
    kb::tests::Require(report.transformRenderProxyCameraCount == 1U, "Scene transform hot path did not compact camera render proxy indices");
    kb::tests::Require(report.transformRenderProxyLightCount == 1U, "Scene transform hot path did not compact light render proxy indices");
    kb::tests::Require(report.transformRenderProxyIdentityAffineFastPathCount == report.transformRenderProxyUpdateCount, "Scene transform render proxy did not use the identity affine fast path for identity transforms");

    const std::span<const kb::scene::SceneEntity> renderProxyEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
    const std::span<const kb::scene::WorldTransformAffine3x4> renderProxyWorlds = scene.Runtime().TransformRenderProxyWorldAffine3x4();
    kb::tests::Require(renderProxyWorlds.size() == renderProxyEntities.size(), "Scene transform compact render proxy payload count diverged from entity count");
    bool foundChildProxy = false;
    for (std::size_t index = 0; index < renderProxyEntities.size(); ++index) {
        if (renderProxyEntities[index] == child.Entity()) {
            foundChildProxy = true;
            kb::tests::Require(kb::tests::NearlyEqual(renderProxyWorlds[index].values[9], 7.0F), "Scene transform compact render proxy wrote invalid child translation X");
            kb::tests::Require(kb::tests::NearlyEqual(renderProxyWorlds[index].values[10], 0.0F), "Scene transform compact render proxy wrote invalid child translation Y");
            kb::tests::Require(kb::tests::NearlyEqual(renderProxyWorlds[index].values[11], 0.0F), "Scene transform compact render proxy wrote invalid child translation Z");
        }
    }
    kb::tests::Require(foundChildProxy, "Scene transform compact render proxy did not include the updated child");

    MeshRendererProxyStats allMeshProxyStats;
    scene.Components().Visitors().ForEachUpdatedMeshRendererRenderProxy(&AccumulateMeshRendererProxyVisit, &allMeshProxyStats);
    kb::tests::Require(allMeshProxyStats.sawVisibleMesh, "Scene compact mesh renderer proxy did not visit the visible mesh");
    kb::tests::Require(allMeshProxyStats.sawHiddenMesh, "Scene compact mesh renderer proxy did not visit the hidden mesh in the unfiltered pass");
    kb::tests::Require(kb::tests::NearlyEqual(allMeshProxyStats.visibleTranslationX, 7.0F), "Scene compact mesh renderer proxy provided an invalid visible mesh transform");

    MeshRendererProxyStats visibleMeshProxyStats;
    scene.Components().Visitors().ForEachVisibleUpdatedMeshRendererRenderProxy(&AccumulateMeshRendererProxyVisit, &visibleMeshProxyStats);
    kb::tests::Require(visibleMeshProxyStats.sawVisibleMesh, "Scene visible compact mesh renderer proxy did not visit the visible mesh");
    kb::tests::Require(!visibleMeshProxyStats.sawHiddenMesh, "Scene visible compact mesh renderer proxy visited a hidden mesh");

    CameraProxyStats cameraProxyStats;
    scene.Components().Visitors().ForEachUpdatedCameraRenderProxy(&AccumulateCameraProxyVisit, &cameraProxyStats);
    kb::tests::Require(cameraProxyStats.visited == 1U, "Scene compact camera proxy visited an invalid number of cameras");
    kb::tests::Require(cameraProxyStats.sawPrimary, "Scene compact camera proxy did not visit the primary camera");
    kb::tests::Require(kb::tests::NearlyEqual(cameraProxyStats.translationX, 11.0F), "Scene compact camera proxy provided an invalid camera transform");

    LightProxyStats lightProxyStats;
    scene.Components().Visitors().ForEachUpdatedLightRenderProxy(&AccumulateLightProxyVisit, &lightProxyStats);
    kb::tests::Require(lightProxyStats.visited == 1U, "Scene compact light proxy visited an invalid number of lights");
    kb::tests::Require(lightProxyStats.sawPoint, "Scene compact light proxy did not visit the point light");
    kb::tests::Require(kb::tests::NearlyEqual(lightProxyStats.translationX, 13.0F), "Scene compact light proxy provided an invalid light transform");

    scene.Components().MeshRenderers().Remove(child.Entity());
    kb::scene::TransformComponent movedChild = scene.Transforms().Get(child);
    movedChild.localPosition.x = 5.0F;
    scene.Transforms().Set(child, movedChild);
    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::SceneRuntimeHotPathReport removedRendererReport = scene.Runtime().HotPathReport();
    kb::tests::Require(removedRendererReport.transformRenderProxyUpdateCount >= 1U, "Scene render proxy mask regression did not update the moved child");
    kb::tests::Require(removedRendererReport.transformRenderProxyMeshRendererCount == 0U, "Scene render proxy mask kept a removed mesh renderer in the compact proxy list");
    kb::tests::Require(removedRendererReport.transformRenderProxyVisibleMeshRendererCount == 0U, "Scene render proxy mask kept a removed visible mesh renderer in the compact proxy list");
}

void RunSceneRuntimeRootTransformFastPathCorrectnessTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject identityRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Identity Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, -3.0F, 5.0F },
            .localScale = kb::scene::Vec3{ 2.0F, 4.0F, 8.0F },
        },
    });
    const kb::scene::SceneObject rotatedRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Rotated Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ -1.0F, 7.0F, 0.5F },
            .localRotation = kb::scene::Quat{ .z = 0.5F, .w = 0.5F },
            .localScale = kb::scene::Vec3{ 3.0F, 3.0F, 3.0F },
        },
    });

    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::TransformComponent identity = scene.Transforms().Get(identityRoot);
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldPosition.x, identity.localPosition.x), "Root transform fast path did not copy local X to world X");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldPosition.y, identity.localPosition.y), "Root transform fast path did not copy local Y to world Y");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldPosition.z, identity.localPosition.z), "Root transform fast path did not copy local Z to world Z");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldScale.x, identity.localScale.x), "Root transform fast path did not copy local scale X");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldScale.y, identity.localScale.y), "Root transform fast path did not copy local scale Y");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldScale.z, identity.localScale.z), "Root transform fast path did not copy local scale Z");
    kb::tests::Require(identity.worldRotation.x == 0.0F && identity.worldRotation.y == 0.0F && identity.worldRotation.z == 0.0F && identity.worldRotation.w == 1.0F, "Root transform fast path did not preserve identity rotation");
    kb::tests::Require(identity.parentVersion == 0U && !identity.worldDirty, "Root transform fast path did not publish clean root metadata");

    const kb::scene::TransformComponent rotated = scene.Transforms().Get(rotatedRoot);
    kb::tests::Require(kb::tests::NearlyEqual(rotated.worldRotation.z, 0.70710677F), "Root transform fallback did not normalize non-identity rotation Z");
    kb::tests::Require(kb::tests::NearlyEqual(rotated.worldRotation.w, 0.70710677F), "Root transform fallback did not normalize non-identity rotation W");
    kb::tests::Require(!rotated.worldDirty, "Root transform fallback did not publish clean metadata");
}

void RunSceneRuntimeRootOnlyNativeDirtyRangePathTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Native Dirty A",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
        },
    });
    kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Native Dirty B",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 5.0F, 6.0F },
        },
    });
    kb::scene::SceneObject rotated = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Native Dirty Rotated",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 7.0F, 8.0F, 9.0F },
            .localRotation = kb::scene::Quat{ .z = 0.5F, .w = 0.5F },
        },
    });

    static_cast<void>(scene.Runtime().SynchronizeTransforms());
    const kb::scene::SceneRuntimeHotPathReport dirtyReport = scene.Runtime().HotPathReport();
    kb::tests::Require(dirtyReport.transformHierarchyInspectedCount == 3U, "Scene root-only native dirty path did not inspect dirty roots");
    kb::tests::Require(dirtyReport.transformHierarchyUpdatedCount == 3U, "Scene root-only native dirty path did not update dirty roots");
    kb::tests::Require(dirtyReport.transformHierarchyRootFastPathCount == 2U, "Scene root-only native dirty path did not isolate identity root fast path updates");
    kb::tests::Require(dirtyReport.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only native dirty path built the transform cache");
    kb::tests::Require(dirtyReport.transformHierarchyEntryBuildNanoseconds == 0U, "Scene root-only native dirty path built hierarchy entries");
    kb::tests::Require(dirtyReport.transformHierarchyBatchFlushCount == 0U, "Scene root-only native dirty path used a batch flush");
    kb::tests::Require(dirtyReport.transformHierarchyFlushedEntityCount == 3U, "Scene root-only native dirty path did not report direct flushed roots");
    kb::tests::Require(dirtyReport.transformRenderProxyUpdateCount == 3U, "Scene root-only native dirty path did not cache render proxy updates");
    kb::tests::Require(dirtyReport.transformRenderProxyIdentityAffineFastPathCount == 2U, "Scene root-only native dirty path did not isolate identity affine proxy writes");

    const kb::scene::TransformComponent firstTransform = scene.Transforms().Get(first);
    const kb::scene::TransformComponent secondTransform = scene.Transforms().Get(second);
    const kb::scene::TransformComponent rotatedTransform = scene.Transforms().Get(rotated);
    kb::tests::Require(!firstTransform.worldDirty && !secondTransform.worldDirty && !rotatedTransform.worldDirty, "Scene root-only native dirty path left roots dirty");
    kb::tests::Require(kb::tests::NearlyEqual(firstTransform.worldPosition.x, 1.0F), "Scene root-only native dirty path wrote invalid first root X");
    kb::tests::Require(kb::tests::NearlyEqual(secondTransform.worldPosition.y, 5.0F), "Scene root-only native dirty path wrote invalid second root Y");
    kb::tests::Require(kb::tests::NearlyEqual(rotatedTransform.worldRotation.z, 0.70710677F), "Scene root-only native dirty path did not normalize rotated root Z");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport cleanReport = scene.Runtime().HotPathReport();
    kb::tests::Require(cleanReport.transformHierarchyInspectedCount == 0U, "Scene root-only native clean path inspected clean roots");
    kb::tests::Require(cleanReport.transformHierarchyUpdatedCount == 0U, "Scene root-only native clean path updated clean roots");
    kb::tests::Require(cleanReport.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only native clean path built the transform cache");

    kb::scene::TransformComponent movedAgain = scene.Transforms().Get(first);
    movedAgain.localPosition.x = 12.0F;
    scene.Transforms().Set(first, movedAgain);
    // the write composed the plain root; flag it so the sync's cached root query has a row to observe
    scene.Transforms().MarkModified(first.Entity());
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(scene.Runtime().HotPathReport().transformHierarchyUpdatedCount == 1U,
        "Cached root query did not observe a transform write without a structural change");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(first).worldPosition.x, 12.0F),
        "Cached root query did not update a modified root world transform");

    const kb::scene::SceneObject appended = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Query Added",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 14.0F, 0.0F, 0.0F } },
    });
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(scene.Entities().Count() == 4U &&
            kb::tests::NearlyEqual(scene.Transforms().Get(appended).worldPosition.x, 14.0F),
        "Cached root query did not rebuild after adding a root");

    const kb::scene::SceneEntity removedEntity = second.Entity();
    scene.Entities().Destroy(second);
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(scene.Entities().Count() == 3U && !scene.Entities().IsAlive(removedEntity),
        "Cached root query retained a destroyed root");
    const kb::scene::SceneObject replacement = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Query Reused Slot",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F } },
    });
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(replacement.Entity() != removedEntity && scene.Entities().Count() == 4U &&
            kb::tests::NearlyEqual(scene.Transforms().Get(replacement).worldPosition.x, 20.0F),
        "Cached root query kept a stale entity after destroying and recreating a root");

    {
        kb::scene::Scene independent;
        const kb::scene::SceneObject independentRoot = independent.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Independent Root Query",
            .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 31.0F, 0.0F, 0.0F } },
        });
        independent.Runtime().SynchronizeTransforms();
        kb::tests::Require(kb::tests::NearlyEqual(independent.Transforms().Get(independentRoot).worldPosition.x, 31.0F),
            "Root query cache shared data with another scene");
    }
    movedAgain = scene.Transforms().Get(first);
    movedAgain.localPosition.x = 18.0F;
    scene.Transforms().Set(first, movedAgain);
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(first).worldPosition.x, 18.0F),
        "Root query cache became invalid after another scene was destroyed");
}

void RunSceneRuntimeRootOnlyNativeDirtyRangeParallelPathTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kRootCount = 1024U;
    std::vector<kb::scene::SceneObject> roots;
    roots.reserve(kRootCount);
    for (std::size_t index = 0U; index < kRootCount; ++index) {
        roots.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Parallel Root Native Dirty",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 2.0F, 3.0F },
            },
        }));
    }

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyInspectedCount == kRootCount, "Scene root-only parallel native dirty path did not inspect all roots");
    kb::tests::Require(report.transformHierarchyUpdatedCount == kRootCount, "Scene root-only parallel native dirty path did not update all roots");
    kb::tests::Require(report.transformHierarchyParallelBatchCount == 1U, "Scene root-only parallel native dirty path did not report a parallel batch");
    kb::tests::Require(report.transformHierarchyParallelChunkCount >= 8U, "Scene root-only parallel native dirty path did not split work into chunks");
    kb::tests::Require(report.transformHierarchyParallelEntityCount == kRootCount, "Scene root-only parallel native dirty path reported an invalid entity count");
    kb::tests::Require(report.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only parallel native dirty path built the transform cache");
    kb::tests::Require(report.transformHierarchyEntryBuildNanoseconds == 0U, "Scene root-only parallel native dirty path built hierarchy entries");
    kb::tests::Require(report.transformRenderProxyUpdateCount == kRootCount, "Scene root-only parallel native dirty path missed render-proxy updates");
    kb::tests::Require(report.transformRenderProxyIdentityAffineFastPathCount == kRootCount, "Scene root-only parallel native dirty path did not batch identity affine proxy writes");

    const kb::scene::TransformComponent sampled = scene.Transforms().Get(roots[777U]);
    kb::tests::Require(kb::tests::NearlyEqual(sampled.worldPosition.x, 777.0F), "Scene root-only parallel native dirty path wrote invalid world X");
    kb::tests::Require(!sampled.worldDirty, "Scene root-only parallel native dirty path left a root dirty");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport cleanReport = scene.Runtime().HotPathReport();
    kb::tests::Require(cleanReport.transformHierarchyInspectedCount == 0U, "Scene root-only parallel native clean path inspected clean roots");
    kb::tests::Require(cleanReport.transformHierarchyUpdatedCount == 0U, "Scene root-only parallel native clean path updated clean roots");
    kb::tests::Require(cleanReport.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only parallel native clean path built the transform cache");
}

void RunSceneTransformIterationUsesUnsafeHotQueryTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kRootCount = 32U;
    for (std::size_t index = 0U; index < kRootCount; ++index) {
        static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Hot Query Iteration Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F },
            },
        }));
    }

    scene.Runtime().SynchronizeTransforms();
    const kb::ecs::WorldTelemetrySnapshot beforeRead = scene.Runtime().EcsWorld().TelemetrySnapshot();
    std::size_t readCount = 0U;
    scene.Transforms().ForEach([](kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context) {
        static_cast<void>(entity);
        static_cast<void>(transform);
        ++(*static_cast<std::size_t*>(context));
    }, &readCount);
    const kb::ecs::WorldTelemetrySnapshot afterRead = scene.Runtime().EcsWorld().TelemetrySnapshot();
    kb::tests::Require(readCount == kRootCount, "Scene transform hot iteration did not visit all transforms");
    kb::tests::Require(afterRead.queryExecutions == beforeRead.queryExecutions, "Scene transform read iteration used the safe query executor");

    const kb::ecs::WorldTelemetrySnapshot beforeWrite = scene.Runtime().EcsWorld().TelemetrySnapshot();
    std::size_t writeCount = 0U;
    scene.Transforms().ForEachMutable([](kb::scene::SceneEntity entity, kb::scene::TransformComponent& transform, void* context) {
        static_cast<void>(entity);
        transform.localPosition.y += 1.0F;
        transform.worldDirty = true;
        ++(*static_cast<std::size_t*>(context));
    }, &writeCount);
    const kb::ecs::WorldTelemetrySnapshot afterWrite = scene.Runtime().EcsWorld().TelemetrySnapshot();
    kb::tests::Require(writeCount == kRootCount, "Scene transform mutable hot iteration did not visit all transforms");
    kb::tests::Require(afterWrite.queryExecutions == beforeWrite.queryExecutions, "Scene transform mutable iteration used the safe query executor");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyUpdatedCount == kRootCount, "Scene transform mutable hot iteration did not mark transforms dirty");
}

void RunSceneRuntimeTransformHierarchyUsesUnsafeQueryPlanTest() {
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 1.0F, 2.0F },
        },
    });
    std::vector<kb::scene::SceneObject> children;
    children.reserve(4096);
    for (int index = 0; index < 4096; ++index) {
        children.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Child",
            .parent = parent,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 1.0F, 2.0F },
            },
        }));
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(
        counters,
        parent.Entity(),
        kb::scene::Vec3{ 10.0F, 6.0F, 2.0F }));
    scene.Runtime().SetTransformPropagationBudget(kb::scene::SceneTransformPropagationBudget{
        .maxInspectedEntitiesPerSync = children.size() + 2U,
    });

    const kb::ecs::WorldTelemetrySnapshot before = scene.Runtime().EcsWorld().TelemetrySnapshot();
    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::ecs::WorldTelemetrySnapshot after = scene.Runtime().EcsWorld().TelemetrySnapshot();
    kb::tests::Require(counters.updated == 1, "Scene transform hierarchy unsafe query test did not run the scene system update");

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformTopologicalBatchCount >= 2U, "Scene transform hierarchy unsafe query test did not build topological batches");
    kb::tests::Require(report.transformRenderProxyUpdateCount >= children.size() + 1U, "Scene transform hierarchy unsafe query path missed render-proxy updates");
    kb::tests::Require(after.queryExecutions == before.queryExecutions, "Scene transform hierarchy hot path used the safe query executor");

    const kb::scene::TransformComponent transform = scene.Transforms().Get(children[123]);
    kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.x, 133.0F), "Scene transform hierarchy unsafe hot query path wrote an invalid child world X");
    kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.y, 7.0F), "Scene transform hierarchy unsafe hot query path did not publish modified parent transform");
    kb::tests::Require(!transform.worldDirty, "Scene transform hierarchy unsafe hot query path left a root transform dirty");
}

void RunSceneSystemPhysicsBodyQueryAccessTest() {
    kb::scene::Scene scene;
    kb::scene::SceneObject physicsBody = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "PhysicsBody",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 7.0F, 1.0F, 2.0F },
        },
    });
    kb::scene::SceneObject transformOnly = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "TransformOnly",
    });
    kb::scene::SceneObject missingCollider = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "MissingCollider",
    });

    scene.Components().Rigidbodies().Set(physicsBody.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 3.0F,
    });
    scene.Components().Colliders().Set(physicsBody.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Sphere,
        .radius = 2.0F,
    });
    scene.Components().Rigidbodies().Set(missingCollider.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
        .mass = 1.0F,
    });

    static_cast<void>(transformOnly);
    kb::scene::SceneSystemContext context(scene, 0.016F);
    PhysicsBodyStats firstPass;
    context.Queries().ForEachPhysicsBody(&AccumulatePhysicsBodyVisit, &firstPass);
    kb::tests::Require(firstPass.visited == 1U, "Scene physics body query did not filter to complete physics bodies");
    kb::tests::Require(firstPass.sawDynamic, "Scene physics body query missed rigidbody data");
    kb::tests::Require(firstPass.sawSphere, "Scene physics body query missed collider data");
    kb::tests::Require(kb::tests::NearlyEqual(firstPass.localPositionX, 7.0F), "Scene physics body query missed transform data");

    PhysicsBodyStats secondPass;
    context.Queries().ForEachPhysicsBody(&AccumulatePhysicsBodyVisit, &secondPass);
    kb::tests::Require(secondPass.visited == 1U, "Scene physics body cached query returned inconsistent results");
}

} // namespace

namespace kb::tests {

void RunSceneBulkMarkModifiedTest() {
    kb::scene::Scene scene;
    const std::array<kb::scene::SceneObject, 3U> objects{
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "A", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } } }),
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "B", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F } } }),
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "C", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 3.0F } } }),
    };
    static_cast<void>(scene.Runtime().Update(0.016F)); // compute world, clear dirty

    // Mutate local transforms directly, then signal them all with one bulk call.
    std::array<kb::scene::SceneEntity, 3U> entities{};
    for (std::size_t index = 0; index < objects.size(); ++index) {
        entities[index] = objects[index].Entity();
        kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entities[index]);
        kb::tests::Require(transform != nullptr, "Bulk mark test could not fetch a transform");
        transform->localPosition = kb::scene::Vec3{ static_cast<float>(index) + 10.0F, 0.0F, 0.0F };
    }
    scene.Transforms().MarkModified(std::span<const kb::scene::SceneEntity>{ entities });

    static_cast<void>(scene.Runtime().Update(0.016F));
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const kb::scene::TransformComponent transform = scene.Transforms().Get(objects[index]);
        kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.x, static_cast<float>(index) + 10.0F), "Bulk mark did not propagate the mutated local position to world");
        kb::tests::Require(!transform.worldDirty, "Bulk mark left a transform dirty after update");
    }
    kb::tests::Require(scene.Runtime().TransformRenderProxyUpdateEntities().size() >= objects.size(), "Bulk mark did not emit render proxy updates for the marked entities");
}

void RunSceneBulkCreateObjectsTest() {
    // H8: bulk scene spawn creates one object per descriptor in one call; the
    // structural changes batch through the world's lazy query-plan invalidation.
    kb::scene::Scene scene;
    std::array<kb::scene::SceneObjectDesc, 4U> descs{
        kb::scene::SceneObjectDesc{ .name = "Bulk0", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F } } },
        kb::scene::SceneObjectDesc{ .name = "Bulk1", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F } } },
        kb::scene::SceneObjectDesc{ .name = "Bulk2", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 30.0F, 0.0F, 0.0F } } },
        kb::scene::SceneObjectDesc{ .name = "Bulk3", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 40.0F, 0.0F, 0.0F } } },
    };

    const std::vector<kb::scene::SceneObject> created = scene.Entities().CreateObjects(std::span<const kb::scene::SceneObjectDesc>{ descs });
    kb::tests::Require(created.size() == descs.size(), "Bulk create did not create one object per descriptor");
    for (const kb::scene::SceneObject object : created) {
        kb::tests::Require(scene.Entities().IsAlive(object), "Bulk-created object is not alive");
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    for (std::size_t index = 0; index < created.size(); ++index) {
        const kb::scene::TransformComponent transform = scene.Transforms().Get(created[index]);
        kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.x, 10.0F * static_cast<float>(index + 1U)), "Bulk-created object world transform was not computed");
    }

    scene.Entities().Destroy(std::span<const kb::scene::SceneObject>{ created });
    for (const kb::scene::SceneObject object : created) {
        kb::tests::Require(!scene.Entities().IsAlive(object), "Bulk-destroyed object is still alive");
    }
}

// Creating objects in bulk takes about one heap allocation per object, not a chunk created and destroyed for every
// intermediate archetype of every object.
void RunSceneBulkCreateObjectsAllocationTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObjectDesc> descs(20000U);
    static_cast<void>(scene.Entities().CreateObjects(descs));
    for (std::size_t index = 0U; index < descs.size(); ++index) {
        descs[index].transform.localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F };
    }
    kb::tests::BeginAllocationTally();
    const std::vector<kb::scene::SceneObject> created = scene.Entities().CreateObjects(descs);
    const kb::tests::AllocationTally tally = kb::tests::EndAllocationTally();
    const double allocationsPerObject = static_cast<double>(tally.count) / static_cast<double>(created.size());
    std::cout << "bulk create allocations per object: " << allocationsPerObject << " bytes per object: " << static_cast<double>(tally.bytes) / static_cast<double>(created.size()) << '\n';
    kb::tests::Require(created.size() == descs.size() && allocationsPerObject <= 1.0, "Creating objects in bulk took more than one heap allocation per object");
}

// One CreateObject at a time is born in its final archetype too: no chunk of the empty or transform-only archetype is
// created and released for each object.
void RunSceneSingleCreateObjectAllocationTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kObjects = 20000U;
    for (std::size_t index = 0U; index < kObjects; ++index) {
        static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{}));
    }
    std::vector<kb::scene::SceneObject> created;
    created.reserve(kObjects);
    kb::tests::BeginAllocationTally();
    for (std::size_t index = 0U; index < kObjects; ++index) {
        kb::scene::SceneObjectDesc desc;
        desc.transform.localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F };
        created.push_back(scene.Entities().CreateObject(desc));
    }
    const kb::tests::AllocationTally tally = kb::tests::EndAllocationTally();
    const double allocationsPerObject = static_cast<double>(tally.count) / static_cast<double>(created.size());
    std::cout << "single create allocations per object: " << allocationsPerObject << " bytes per object: " << static_cast<double>(tally.bytes) / static_cast<double>(created.size()) << '\n';
    kb::tests::Require(allocationsPerObject <= 1.0, "Creating one object at a time took more than one heap allocation per object");
    kb::tests::Require(scene.Transforms().Get(created.back()).localPosition.x == static_cast<float>(kObjects - 1U), "A single created object lost its transform");
}

// The flagged-row count a table keeps for the transform sync matches the counts of its chunks through writes,
// destruction and the sync that clears them.
void RunTransformTableDirtyCountTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObjectDesc> descs(5000U);
    std::vector<kb::scene::SceneEntity> entities;
    for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) entities.push_back(object.Entity());
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    const kb::ecs::ComponentId transformId = world.Component<kb::scene::TransformComponent>();
    const auto countsMatch = [&world, transformId] {
        std::vector<kb::ecs::QueryTableDispatchRecord> records;
        const std::array<kb::ecs::ComponentId, 1U> ids{ transformId };
        world.NativeStorage().CollectQueryRecords(ids, {}, {}, records);
        std::map<std::size_t, std::size_t> chunkSums;
        for (const kb::ecs::QueryTableDispatchRecord& record : records) {
            chunkSums[record.nativeArchetypeIndex] += world.NativeStorage().ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, transformId);
        }
        return std::ranges::all_of(chunkSums, [&world, transformId](const auto& entry) {
            return world.NativeStorage().ArchetypeComponentDirtyCount(entry.first, transformId) == entry.second;
        });
    };
    kb::tests::Require(countsMatch(), "Table dirty count differs from its chunks after creation");
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(countsMatch(), "Table dirty count differs from its chunks after a sync");
    std::vector<kb::scene::TransformComponent> transforms(entities.size() / 3U);
    scene.Transforms().SetMany(std::span<const kb::scene::SceneEntity>{ entities }.first(transforms.size()), transforms);
    scene.Transforms().Set(entities.back(), kb::scene::TransformComponent{});
    for (std::size_t index = 0U; index < entities.size(); index += 7U) scene.Entities().Destroy(entities[index]);
    kb::tests::Require(countsMatch(), "Table dirty count differs from its chunks after writes and destruction");
    scene.Runtime().SynchronizeTransforms();
    kb::tests::Require(countsMatch(), "Table dirty count differs from its chunks after the sync cleared them");
}

// Golden transform state of a fixed-dt run: FNV-1a over local and world TRS, versions and the dirty flag of every
// object (in creation order), its interpolated pose and the runtime's render-proxy transform list, after every
// write and every Update. The expected values were recorded from the transform path before the per-row lanes.
struct TransformGoldenHash {
    std::uint64_t value = 1469598103934665603ULL;

    void Bytes(const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0U; index < size; ++index) {
            value ^= bytes[index];
            value *= 1099511628211ULL;
        }
    }
    void U64(std::uint64_t number) noexcept { Bytes(&number, sizeof(number)); }
    void Float(float number) noexcept { Bytes(&number, sizeof(number)); }
    void Vec(const kb::scene::Vec3& vector) noexcept { Float(vector.x); Float(vector.y); Float(vector.z); }
    void Rotation(const kb::scene::Quat& rotation) noexcept { Float(rotation.x); Float(rotation.y); Float(rotation.z); Float(rotation.w); }
    void Transform(const kb::scene::TransformComponent& transform) noexcept {
        Vec(transform.localPosition); Rotation(transform.localRotation); Vec(transform.localScale);
        Vec(transform.worldPosition); Rotation(transform.worldRotation); Vec(transform.worldScale);
        U64(transform.localVersion); U64(transform.parentVersion); U64(transform.worldVersion); U64(transform.worldDirty ? 1U : 0U);
    }
};

enum class TransformGoldenScene { Flat, Overlay, Observed, Hierarchy };

class TransformGoldenFixedSystem final : public kb::scene::SceneSystem {
public:
    [[nodiscard]] bool RequiresFixedStep() const override { return true; }
};

// How the scenario writes the movers: SetMany with the state hashed before and after every Update (the golden
// hash), SetMany or a transform pass with the state hashed after every Update only.
enum class TransformGoldenWrites { SetMany, SetManyAfterUpdate, Pass };

std::uint64_t RunTransformGoldenScenario(TransformGoldenScene kind, TransformGoldenWrites writes = TransformGoldenWrites::SetMany) {
    kb::ecs::WorldConfig config{};
    config.mirrorValueWritesOnlyForObservedComponents = kind != TransformGoldenScene::Observed;
    kb::scene::Scene scene{ config };
    std::vector<kb::scene::SceneEntity> objects;
    std::vector<kb::scene::SceneEntity> movers;
    std::vector<kb::scene::SceneEntity> overlay;
    const auto create = [&](kb::scene::Vec3 position) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .transform = kb::scene::TransformComponent{ .localPosition = position } }).Entity();
        objects.push_back(entity);
        return entity;
    };
    const auto createMovers = [&](std::size_t count) {
        std::vector<kb::scene::SceneObjectDesc> descs(count);
        for (std::size_t index = 0U; index < count; ++index) {
            descs[index].transform.localPosition = kb::scene::Vec3{ static_cast<float>(movers.size() + index), 0.5F, 1.0F };
        }
        for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) {
            objects.push_back(object.Entity());
            movers.push_back(object.Entity());
        }
    };
    if (kind == TransformGoldenScene::Overlay || kind == TransformGoldenScene::Observed) {
        // the benchmark's screen overlay: a canvas with parented children, created before the crowd
        const kb::scene::SceneEntity canvas = create(kb::scene::Vec3{ 0.0F, 0.0F, 0.0F });
        for (int child = 0; child < 8; ++child) {
            overlay.push_back(create(kb::scene::Vec3{ static_cast<float>(child), 1.0F, 0.0F }));
            kb::tests::Require(scene.Hierarchy().SetParent(overlay.back(), canvas), "Golden transform overlay could not be parented");
        }
    }
    createMovers(3000U);
    std::vector<kb::scene::SceneEntity> chainRoots;
    std::vector<kb::scene::SceneEntity> chainMiddles;
    std::vector<kb::scene::SceneEntity> chainLeaves;
    std::vector<kb::scene::SceneEntity> prefabRoots;
    if (kind == TransformGoldenScene::Hierarchy) {
        for (int chain = 0; chain < 20; ++chain) {
            chainRoots.push_back(create(kb::scene::Vec3{ static_cast<float>(chain), 2.0F, 0.0F }));
            chainMiddles.push_back(create(kb::scene::Vec3{ 0.0F, 1.0F, 0.5F }));
            kb::tests::Require(scene.Hierarchy().SetParent(chainMiddles.back(), chainRoots.back()), "Golden transform chain could not be parented");
            for (int leaf = 0; leaf < 2; ++leaf) {
                chainLeaves.push_back(create(kb::scene::Vec3{ static_cast<float>(leaf), 0.0F, 1.0F }));
                kb::tests::Require(scene.Hierarchy().SetParent(chainLeaves.back(), chainMiddles.back()), "Golden transform leaf could not be parented");
            }
        }
        kb::scene::ScenePrefab prefab;
        const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
            .name = "Golden Root", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 4.0F, 0.0F, 0.0F } } });
        static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
            .name = "Golden Child", .parentNode = rootNode, .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F } } }));
        for (const kb::scene::ScenePrefabInstance& instance : scene.Prefabs().InstantiateMany(prefab, 3U)) {
            prefabRoots.push_back(instance.ObjectAt(0U).Entity());
            objects.push_back(instance.ObjectAt(0U).Entity());
            objects.push_back(instance.ObjectAt(1U).Entity());
        }
        scene.Runtime().AddSceneSystem(std::make_unique<TransformGoldenFixedSystem>());
    }

    const auto indexOf = [&objects](kb::scene::SceneEntity entity) -> std::uint64_t {
        const auto found = std::ranges::find(objects, entity);
        return found == objects.end() ? UINT64_MAX : static_cast<std::uint64_t>(found - objects.begin());
    };
    TransformGoldenHash hash;
    const auto hashState = [&](bool afterUpdate) {
        for (const kb::scene::SceneEntity entity : objects) {
            const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
            hash.U64(transform == nullptr ? 0U : 1U);
            if (transform != nullptr) hash.Transform(*transform);
            if (afterUpdate) {
                const std::optional<kb::scene::TransformComponent> interpolated = scene.Runtime().InterpolatedTransform(entity);
                hash.U64(interpolated.has_value() ? 1U : 0U);
                if (interpolated.has_value()) hash.Transform(*interpolated);
            }
        }
        if (!afterUpdate) return;
        // the list is a set: its order follows storage, so it is folded order-independently
        const std::span<const kb::scene::SceneEntity> proxyEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
        const std::span<const kb::scene::WorldTransformAffine3x4> proxyAffines = scene.Runtime().TransformRenderProxyWorldAffine3x4();
        std::uint64_t proxySum = 0U;
        for (std::size_t index = 0U; index < proxyEntities.size(); ++index) {
            TransformGoldenHash entry;
            entry.U64(indexOf(proxyEntities[index]));
            if (index < proxyAffines.size()) entry.Bytes(proxyAffines[index].values, sizeof(proxyAffines[index].values));
            proxySum += entry.value;
        }
        hash.U64(proxyEntities.size());
        hash.U64(proxyAffines.size());
        hash.U64(proxySum);
        hash.U64(scene.Runtime().HotPathReport().transformRenderProxyUpdateCount);
    };

    std::vector<kb::scene::TransformComponent> batch;
    std::vector<kb::scene::SceneEntity> written;
    for (int frame = 0; frame < 8; ++frame) {
        if (frame == 2) createMovers(40U);
        if (frame == 4) {
            for (std::size_t index = 5U; index < 15U; ++index) scene.Entities().Destroy(movers[index]);
        }
        written = movers;
        // the comparison writes each row once, and the one written twice before the batch (the pass composes it)
        const bool once = writes != TransformGoldenWrites::SetMany;
        if (frame == 5 && !once) written.push_back(movers[1]);
        if (once) scene.Transforms().Set(movers[2], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(frame), 3.0F, 0.0F } });
        batch.resize(written.size());
        for (std::size_t index = 0U; index < written.size(); ++index) {
            const float phase = static_cast<float>(frame) * 0.25F + static_cast<float>(index) * 0.001F + (index + 1U == written.size() && frame == 5 ? 3.0F : 0.0F);
            kb::scene::TransformComponent& value = batch[index];
            value = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(index % 97) + std::sin(phase), 0.5F, static_cast<float>(index / 97) + std::cos(phase) } };
            if (index % 2U == 1U) value.localRotation = kb::scene::Quat{ 0.0F, std::sin(phase * 0.5F), 0.0F, std::cos(phase * 0.5F) };
            if (index % 7U == 0U) value.localScale = kb::scene::Vec3{ 1.5F, 1.5F, 1.5F };
        }
        if (writes == TransformGoldenWrites::Pass) {
            std::unordered_map<kb::scene::SceneEntity::IdType, std::size_t> batchIndex;
            for (std::size_t index = 0U; index < written.size(); ++index) batchIndex[written[index].Id()] = index;
            const kb::scene::TransformPassStats stats = scene.Transforms().ParallelForEachRoot(256U, [&batchIndex, &batch](kb::scene::TransformRowRange& range) {
                for (std::size_t row = 0U; row < range.Count(); ++row) {
                    const auto found = batchIndex.find(range.Entity(row).Id());
                    if (found == batchIndex.end()) continue;
                    const kb::scene::TransformComponent& value = batch[found->second];
                    range.SetLocal(row, value.localPosition, value.localRotation, value.localScale);
                }
            });
            const std::size_t alive = static_cast<std::size_t>(std::ranges::count_if(batchIndex, [&scene](const auto& entry) {
                return scene.Entities().IsAlive(kb::scene::SceneEntity{ entry.first });
            }));
            kb::tests::Require(stats.rowsWritten == alive, "Transform pass did not write every listed live row");
        } else {
            scene.Transforms().SetMany(written, batch);
        }
        if (!once) scene.Transforms().Set(movers[2], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(frame), 3.0F, 0.0F } });
        if (!overlay.empty() && frame == 3) {
            scene.Transforms().Set(overlay[0], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 5.0F, 0.0F } });
        }
        for (std::size_t chain = 0U; chain < chainRoots.size(); ++chain) {
            const float offset = static_cast<float>(frame) * 0.5F + static_cast<float>(chain);
            scene.Transforms().Set(chainRoots[chain], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ offset, 2.0F, 0.0F },
                .localRotation = kb::scene::Quat{ 0.0F, std::sin(offset * 0.1F), 0.0F, std::cos(offset * 0.1F) } });
            if (chain % 2U == 0U) {
                scene.Transforms().Set(chainMiddles[chain], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 1.0F + offset * 0.1F, 0.5F },
                    .localScale = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F } });
            }
        }
        if (!chainLeaves.empty() && frame % 2 == 1) {
            scene.Transforms().Set(chainLeaves[0], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(frame), 0.0F, 1.0F } });
        }
        if (!chainLeaves.empty() && frame == 5) {
            kb::tests::Require(scene.Hierarchy().SetParent(chainLeaves[3], chainMiddles[4]), "Golden transform leaf could not be moved");
        }
        if (!chainLeaves.empty() && frame == 6) {
            kb::tests::Require(scene.Hierarchy().SetParent(chainLeaves[5], kb::scene::SceneEntity{}), "Golden transform leaf could not be unparented");
        }
        for (std::size_t index = 0U; index < prefabRoots.size(); ++index) {
            scene.Transforms().Set(prefabRoots[index], kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(frame + index), 0.0F, 4.0F } });
        }
        if (writes == TransformGoldenWrites::SetMany) hashState(false);
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        hashState(true);
    }
    return hash.value;
}

void RunTransformGoldenHashTest() {
    // A plain entity's write composes its world transform at once: the state before each Update shows it.
    constexpr std::array<std::pair<TransformGoldenScene, std::uint64_t>, 4U> expected{ {
        { TransformGoldenScene::Flat, 0x81ebf71ba3a68884ULL },
        { TransformGoldenScene::Overlay, 0x3d4bc58dbb7dda50ULL },
        { TransformGoldenScene::Observed, 0x7e94174614290a5fULL },
        // the scene runs fixed steps; its plain entities, not interpolated, report their current transform
        { TransformGoldenScene::Hierarchy, 0x06e0adb696b5e531ULL },
    } };
    bool matches = true;
    for (const auto& [kind, value] : expected) {
        const std::uint64_t actual = RunTransformGoldenScenario(kind);
        if (actual != value) {
            std::cerr << "transform golden scene " << static_cast<int>(kind) << " hash 0x" << std::hex << actual << std::dec << '\n';
            matches = false;
        }
    }
    kb::tests::Require(matches, "Transform state of a fixed-dt run differs from the recorded golden hash");
}

// A transform pass writing the movers leaves every scene in the state SetMany and Update leave it in: local and world
// TRS, versions, interpolated poses and the render-proxy updates after each Update.
void RunTransformPassMatchesSetManyTest() {
    for (const TransformGoldenScene kind : { TransformGoldenScene::Flat, TransformGoldenScene::Overlay, TransformGoldenScene::Observed, TransformGoldenScene::Hierarchy }) {
        const std::uint64_t setMany = RunTransformGoldenScenario(kind, TransformGoldenWrites::SetManyAfterUpdate);
        const std::uint64_t pass = RunTransformGoldenScenario(kind, TransformGoldenWrites::Pass);
        if (setMany != pass) std::cerr << "transform pass scene " << static_cast<int>(kind) << " differs\n";
        kb::tests::Require(setMany == pass, "A transform pass left a different state than SetMany and Update");
    }
}

struct TransformPassAgentState {
    float speed = 0.0F;
};

// The contract of a transform pass: the application's component of the same rows, the rows-written counter, a
// structural change in the body, and an exception thrown by the body.
void RunTransformPassContractTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObjectDesc> descs(20000U);
    std::vector<kb::scene::SceneEntity> agents;
    for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) agents.push_back(object.Entity());
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    for (std::size_t index = 0U; index < agents.size(); index += 2U) world.Set(agents[index], TransformPassAgentState{ .speed = static_cast<float>(index) });
    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));

    const kb::scene::TransformPassStats stats = scene.Transforms().ParallelForEachRoot<TransformPassAgentState>(512U, [](kb::scene::TransformRowRange& range) {
        const TransformPassAgentState* states = range.Column<TransformPassAgentState>();
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            const kb::scene::TransformComponent& current = range.Get(row);
            range.SetLocal(row, kb::scene::Vec3{ states[row].speed, 1.0F, 0.0F }, current.localRotation, current.localScale);
        }
    });
    kb::tests::Require(stats.rowsVisited == agents.size() / 2U && stats.rowsWritten == stats.rowsVisited && stats.rowsDeferred == 0U,
        "A transform pass over an application component must visit and write exactly its rows");
    const kb::scene::TransformComponent moved = scene.Transforms().Get(agents[42]);
    kb::tests::Require(moved.localPosition.x == 42.0F && moved.worldPosition.x == 42.0F && !moved.worldDirty,
        "A transform pass must compose a plain row's world transform in place");
    kb::tests::Require(scene.Transforms().Get(agents[43]).localPosition.y == 0.0F, "A transform pass must not touch rows outside its archetypes");

    // objects created with the application's component are born in its archetype
    std::vector<TransformPassAgentState> spawnStates(3000U);
    for (std::size_t index = 0U; index < spawnStates.size(); ++index) spawnStates[index].speed = 1000.0F + static_cast<float>(index);
    const std::array<kb::ecs::World::BulkComponentView, 1U> spawnComponents{ kb::ecs::World::MakeBulkComponentView(std::span<const TransformPassAgentState>{ spawnStates }) };
    std::vector<kb::scene::SceneObjectDesc> spawnDescs(spawnStates.size());
    const std::vector<kb::scene::SceneObject> spawned = scene.Entities().CreateObjects(spawnDescs, spawnComponents);
    kb::tests::Require(spawned.size() == spawnStates.size() && world.TryGet<TransformPassAgentState>(spawned[7].Entity()) != nullptr &&
            world.TryGet<TransformPassAgentState>(spawned[7].Entity())->speed == 1007.0F,
        "Objects created with an application component must carry its value");
    const kb::scene::TransformPassStats spawnedStats = scene.Transforms().ParallelForEachRoot<TransformPassAgentState>(512U, [](kb::scene::TransformRowRange& range) {
        const TransformPassAgentState* states = range.Column<TransformPassAgentState>();
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            range.SetLocal(row, kb::scene::Vec3{ states[row].speed, 2.0F, 0.0F }, range.Get(row).localRotation, range.Get(row).localScale);
        }
    });
    kb::tests::Require(spawnedStats.rowsWritten == agents.size() / 2U + spawned.size() &&
            scene.Transforms().Get(spawned[7]).worldPosition.x == 1007.0F,
        "A transform pass must reach objects created with the application component");
    for (const kb::scene::SceneObject& object : spawned) scene.Entities().Destroy(object);

    bool structuralChangeRejected = false;
    try {
        static_cast<void>(scene.Transforms().ParallelForEachRoot(4096U, [&scene](kb::scene::TransformRowRange&) {
            static_cast<void>(scene.Entities().CreateObject());
        }));
    } catch (const std::exception&) {
        structuralChangeRejected = true;
    }
    kb::tests::Require(structuralChangeRejected && scene.Entities().Count() == agents.size(), "A structural change inside a transform pass must throw");

    bool rethrown = false;
    try {
        static_cast<void>(scene.Transforms().ParallelForEachRoot(512U, [](kb::scene::TransformRowRange& range) {
            for (std::size_t row = 0U; row < range.Count(); ++row) {
                const kb::scene::TransformComponent& current = range.Get(row);
                range.SetLocal(row, kb::scene::Vec3{ 7.0F, 7.0F, 7.0F }, current.localRotation, current.localScale);
            }
            throw std::runtime_error("body failed");
        }));
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    kb::tests::Require(rethrown, "A transform pass must rethrow the exception of its body");
    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
    for (const kb::scene::SceneEntity agent : agents) {
        const kb::scene::TransformComponent transform = scene.Transforms().Get(agent);
        if (transform.worldDirty || transform.worldPosition.x != transform.localPosition.x) {
            kb::tests::Require(false, "Rows written before a body threw must stay consistent");
        }
    }
}

// A spawning crowd grows the per-frame transform lists by a few hundred entries every frame. Reserving exactly the
// new size reallocated every one of them each frame (hundreds of MB per Update at a million entities).
void RunSceneRuntimeGrowingCrowdUpdateAllocationTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneEntity> movers;
    std::vector<kb::scene::SceneObjectDesc> descs(200000U);
    const auto spawn = [&scene, &movers, &descs]() {
        for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) movers.push_back(object.Entity());
    };
    spawn();
    descs.resize(333U);
    std::vector<kb::scene::TransformComponent> batch;
    std::size_t updateBytes = 0U;
    constexpr int kWarmup = 5;
    constexpr int kFrames = 40;
    for (int frame = 0; frame < kWarmup + kFrames; ++frame) {
        spawn();
        batch.assign(movers.size(), kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(frame), 0.0F, 0.0F } });
        scene.Transforms().SetMany(movers, batch);
        kb::tests::BeginAllocationTally();
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        const kb::tests::AllocationTally tally = kb::tests::EndAllocationTally();
        if (frame >= kWarmup) updateBytes += tally.bytes;
    }
    const double megabytesPerUpdate = static_cast<double>(updateBytes) / static_cast<double>(kFrames) / (1024.0 * 1024.0);
    std::cout << "growing crowd update allocations: " << megabytesPerUpdate << " MB per frame\n";
    kb::tests::Require(megabytesPerUpdate <= 1.0, "Update of a crowd growing by 333 entities per frame allocated more than 1 MB per frame");
}

// A batch write gives every row exactly what writing the same list one entity at a time gives: random batches over
// plain, parented and prefab rows, with dead entities and, in some batches, entities listed twice.
void RunSceneTransformSetManyMatchesPerEntityWritesTest() {
    struct World {
        kb::scene::Scene scene;
        std::vector<kb::scene::SceneEntity> entities;
    };
    const auto build = [](World& world) {
        std::vector<kb::scene::SceneObjectDesc> descs(40000U);
        for (const kb::scene::SceneObject& object : world.scene.Entities().CreateObjects(descs)) world.entities.push_back(object.Entity());
        for (std::size_t index = 0U; index < 200U; ++index) {
            kb::tests::Require(world.scene.Hierarchy().SetParent(world.entities[index * 7U + 1U], world.entities[index * 7U]), "Batch write test could not parent");
        }
        kb::scene::ScenePrefab prefab;
        const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Batch Root" });
        static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Batch Child", .parentNode = rootNode }));
        for (const kb::scene::ScenePrefabInstance& instance : world.scene.Prefabs().InstantiateMany(prefab, 50U)) {
            world.entities.push_back(instance.ObjectAt(0U).Entity());
            world.entities.push_back(instance.ObjectAt(1U).Entity());
        }
        for (std::size_t index = 30000U; index < 30400U; ++index) world.scene.Entities().Destroy(world.entities[index]);
        static_cast<void>(world.scene.Runtime().Update(1.0F / 60.0F));
    };
    World batched;
    World single;
    build(batched);
    build(single);
    std::uint32_t seed = 777U;
    const auto next = [&seed](std::uint32_t range) {
        seed = seed * 1664525U + 1013904223U;
        return (seed >> 8U) % range;
    };
    const auto compare = [&batched, &single](const char* message) {
        TransformGoldenHash batchedHash;
        TransformGoldenHash singleHash;
        for (std::size_t index = 0U; index < batched.entities.size(); ++index) {
            const kb::scene::TransformComponent* left = batched.scene.Transforms().TryGet(batched.entities[index]);
            const kb::scene::TransformComponent* right = single.scene.Transforms().TryGet(single.entities[index]);
            batchedHash.U64(left == nullptr ? 0U : 1U);
            singleHash.U64(right == nullptr ? 0U : 1U);
            if (left != nullptr) batchedHash.Transform(*left);
            if (right != nullptr) singleHash.Transform(*right);
        }
        kb::tests::Require(batchedHash.value == singleHash.value, message);
    };
    std::vector<std::size_t> picks;
    std::vector<kb::scene::SceneEntity> batch;
    std::vector<kb::scene::TransformComponent> values;
    for (int round = 0; round < 6; ++round) {
        picks.resize(20000U + next(15000U));
        for (std::size_t& pick : picks) pick = next(static_cast<std::uint32_t>(batched.entities.size()));
        if (round % 2 == 0) {
            // most batches list each entity once: sort the picks and drop repeats
            std::ranges::sort(picks);
            picks.erase(std::unique(picks.begin(), picks.end()), picks.end());
            for (std::size_t index = picks.size(); index > 1U; --index) std::swap(picks[index - 1U], picks[next(static_cast<std::uint32_t>(index))]);
        }
        batch.resize(picks.size());
        values.resize(picks.size());
        for (std::size_t index = 0U; index < picks.size(); ++index) {
            batch[index] = batched.entities[picks[index]];
            values[index] = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ static_cast<float>(round), static_cast<float>(index), 1.0F },
                .localRotation = kb::scene::Quat{ 0.0F, 0.6F, 0.0F, 0.8F }, .localScale = kb::scene::Vec3{ 1.0F, 2.0F, 1.0F } };
        }
        batched.scene.Transforms().SetMany(batch, values);
        for (std::size_t index = 0U; index < picks.size(); ++index) {
            if (single.scene.Entities().IsAlive(single.entities[picks[index]])) single.scene.Transforms().Set(single.entities[picks[index]], values[index]);
        }
        compare("A batch transform write differs from writing each entity on its own");
        static_cast<void>(batched.scene.Runtime().Update(1.0F / 60.0F));
        static_cast<void>(single.scene.Runtime().Update(1.0F / 60.0F));
        compare("A batch transform write synchronized differently from writing each entity on its own");
    }
}

void RunSceneSystemTransformSyncTests() {
    if (EnvironmentFlagEnabled("KB_SCENE_RUNTIME_STRESS")) {
        RunSceneRuntimeHeadlessStress();
        return;
    }
    if (EnvironmentFlagEnabled("KB_SCENE_RUNTIME_SCALE")) {
        RunSceneRuntimeScaleBenchmark();
        return;
    }
    RunSceneSystemHandleRemovalTest();
    RunSceneSystemTransformSyncTest();
    RunTransformSyncContractScriptsRequireExplicitSyncAcrossSystemsTest();
    RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest();
    RunFixedTickCommandBufferRunsBeforePhysicsTest();
    RunFixedTickAndPhysicsShareSceneAccumulatorTest();
    RunSceneRuntimeFixedStepTest();
    RunSceneRuntimeFrameAndPlayStateTest();
    RunSceneRuntimeReadSnapshotAndCommandQueueTest();
    RunSceneRuntimeFixedInterpolationTest();
    RunSceneTransformLookupLifetimeAndFallbackTest();
    RunSceneRuntimeDestroyedDirtyTransformTest();
    RunSceneRuntimeMixedTransformWritesTest();
    RunSceneRuntimeTransformHotPathReportTest();
    RunSceneRuntimeRootTransformFastPathCorrectnessTest();
    RunSceneRuntimeRootOnlyNativeDirtyRangePathTest();
    RunSceneRuntimeRootOnlyNativeDirtyRangeParallelPathTest();
    RunSceneTransformIterationUsesUnsafeHotQueryTest();
    RunSceneRuntimeTransformHierarchyUsesUnsafeQueryPlanTest();
    RunSceneSystemPhysicsBodyQueryAccessTest();
    RunSceneBulkMarkModifiedTest();
    RunSceneBulkCreateObjectsTest();
    RunTransformGoldenHashTest();
    RunTransformPassMatchesSetManyTest();
    RunTransformPassContractTest();
    RunSceneRuntimeGrowingCrowdUpdateAllocationTest();
    RunSceneTransformSetManyMatchesPerEntityWritesTest();
    RunSceneBulkCreateObjectsAllocationTest();
    RunSceneSingleCreateObjectAllocationTest();
    RunTransformTableDirtyCountTest();
}

} // namespace kb::tests
