#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"
#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/particles/ParticleGpuReadback.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#endif

namespace kb::cli {
namespace {

struct ProfileOptions {
    std::uint32_t warmup = 60U;
    std::uint32_t frames = 180U;
    std::uint32_t samples = 64U;
    std::uint64_t bufferBudget = 0U;
};

std::optional<ProfileOptions> ParseProfileOptions(const ArgumentList& arguments, CommandIo io) {
    constexpr std::array<std::string_view, 8U> allowed{
        "--project", "--effect", "--backend", "--warmup", "--frames", "--samples", "--require-gpu", "--gpu-buffer-budget"};
    if (!arguments.Errors().empty() || !arguments.UnknownOptions(allowed).empty() || !arguments.Positionals().empty()) {
        io.err << "error: vfx-profile received an unknown option or positional argument\n";
        return std::nullopt;
    }
    for (const auto name : allowed) {
        if (arguments.Options(name).size() > 1U) {
            io.err << "error: vfx-profile options must occur once\n";
            return std::nullopt;
        }
    }
    ProfileOptions options;
    const auto parse = [&](std::string_view name, auto& value, auto minimum, auto maximum) {
        const auto text = arguments.Option(name);
        if (!text) return true;
        const auto result = std::from_chars(text->data(), text->data() + text->size(), value);
        if (result.ec != std::errc{} || result.ptr != text->data() + text->size() || value < minimum || value > maximum) {
            io.err << "error: " << name << " expects an integer in " << minimum << ".." << maximum << '\n';
            return false;
        }
        return true;
    };
    if (!parse("--warmup", options.warmup, 0U, 36000U) ||
        !parse("--frames", options.frames, 1U, 36000U) ||
        !parse("--samples", options.samples, 1U, 4096U) ||
        !parse("--gpu-buffer-budget", options.bufferBudget, std::uint64_t{0U}, kb::scene::kParticleEffectMaxGpuResourceBytes)) return std::nullopt;
    return options;
}

#if defined(_WIN32)
using kb::core::JsonValue;
using Clock = std::chrono::steady_clock;

JsonValue Integer(std::uint64_t value) { return JsonValue::MakeString(std::to_string(value)); }

class ProfileSurface final : public kb::render::RenderSurface {
public:
    ProfileSurface() : window_(CreateWindowExW(0U, L"STATIC", L"Particle profile", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {}
    ~ProfileSurface() override { if (window_ != nullptr) DestroyWindow(window_); }
    ProfileSurface(const ProfileSurface&) = delete;
    ProfileSurface& operator=(const ProfileSurface&) = delete;
    std::uint32_t Width() const noexcept override { return 64U; }
    std::uint32_t Height() const noexcept override { return 64U; }
    void* NativeWindowHandle() const noexcept override { return window_; }
    void* NativeDisplayHandle() const noexcept override { return nullptr; }
private:
    HWND window_ = nullptr;
};

struct ProfileTarget {
    kb::render::SceneRenderTarget target;
    ~ProfileTarget() { target.Shutdown(); }
};

std::optional<std::uint64_t> CpuNanoseconds(bool process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool valid = process
        ? GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != 0
        : GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != 0;
    if (!valid) return std::nullopt;
    const auto ticks = [](FILETIME time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
    };
    return (ticks(kernel) + ticks(user)) * 100U;
}

JsonValue CpuDifference(std::optional<std::uint64_t> before, std::optional<std::uint64_t> after) {
    return before && after && *after >= *before ? Integer(*after - *before) : JsonValue::MakeNull();
}

JsonValue MemorySnapshot(const kb::render::Renderer* renderer = nullptr) {
    auto result = JsonValue::MakeObject();
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        result.Set("working_set_bytes", Integer(counters.WorkingSetSize));
        result.Set("peak_working_set_bytes", Integer(counters.PeakWorkingSetSize));
        result.Set("private_usage_bytes", Integer(counters.PrivateUsage));
        result.Set("peak_pagefile_usage_bytes", Integer(counters.PeakPagefileUsage));
    } else result.Set("process_memory_available", JsonValue::MakeBool(false));
    result.Set("gpu_emitter_requested_buffer_bytes", renderer ? Integer(renderer->GpuEmitterAllocatedBytes()) : JsonValue::MakeNull());
    const auto* stats = renderer && renderer->IsInitialized() ? bgfx::getStats() : nullptr;
    result.Set("bgfx_gpu_memory_used_bytes", stats && stats->gpuMemoryUsed >= 0
        ? Integer(static_cast<std::uint64_t>(stats->gpuMemoryUsed)) : JsonValue::MakeNull());
    return result;
}

JsonValue Distribution(std::vector<double> samples) {
    auto result = JsonValue::MakeObject();
    result.Set("sample_count", JsonValue::MakeNumber(static_cast<double>(samples.size())));
    if (samples.empty()) return result;
    std::sort(samples.begin(), samples.end());
    double sum = 0.0;
    for (const auto value : samples) sum += value;
    const auto quantile = [&](std::size_t percent) {
        return JsonValue::MakeNumber(samples[(samples.size() - 1U) * percent / 100U]);
    };
    result.Set("mean_ns", JsonValue::MakeNumber(sum / static_cast<double>(samples.size())));
    result.Set("p50_ns", quantile(50U));
    result.Set("p95_ns", quantile(95U));
    result.Set("p99_ns", quantile(99U));
    return result;
}

int Profile(const ArgumentList& arguments, ProfileOptions options, CommandIo io) {
    using namespace kb::render;
    const auto projectRoot = material::ProjectRoot(arguments, "vfx-profile", io);
    const auto effectText = arguments.Option("--effect");
    if (projectRoot.empty() || !effectText) {
        io.err << "error: vfx-profile requires --project <dir> --effect /Game/<path>.kbvfx\n";
        return 1;
    }
    const auto effect = material::ProjectAssetFile(projectRoot, *effectText, ".kbvfx", "particle effect", "kb_cli vfx-new", io);
    if (!effect) return 1;
    const auto backend = arguments.Option("--backend").value_or("d3d11");
    const auto rendererType = backend == "d3d11" ? bgfx::RendererType::Direct3D11
        : backend == "d3d12" ? bgfx::RendererType::Direct3D12
        : backend == "vulkan" ? bgfx::RendererType::Vulkan
        : backend == "opengl" ? bgfx::RendererType::OpenGL : bgfx::RendererType::Count;
    if (rendererType == bgfx::RendererType::Count) {
        io.err << "error: --backend expects d3d11, d3d12, vulkan or opengl\n";
        return 1;
    }
    std::string error;
    const auto projectFile = kb::project::ProjectManager::FindProjectFile(projectRoot, error);
    if (projectFile.empty()) { io.err << "error: " << error << '\n'; return 1; }
    auto loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) { io.err << "error: " << loaded.error << '\n'; return 1; }
    const auto policy = kb::project::ParticleProjectPolicy::Inspect(projectRoot, loaded.descriptor);
    if (!policy.IsRunnable()) { io.err << "error: " << policy.diagnostic << '\n'; return 1; }
    for (auto& plugin : loaded.descriptor.plugins) {
        const std::filesystem::path path{plugin.binaryPath};
        if (plugin.enabled && !path.empty() && path.is_relative()) {
            std::error_code pathError;
            if (std::filesystem::is_regular_file(projectRoot / path, pathError) && !pathError)
                plugin.binaryPath = (projectRoot / path).string();
        }
    }
    auto memory = JsonValue::MakeObject();
    memory.Set("before_scene", MemorySnapshot());
    auto sceneOwner = std::make_unique<kb::scene::Scene>(std::move(loaded.descriptor), kb::scene::SceneMode::Runtime);
    auto& scene = *sceneOwner;
    if (!scene.ModuleDiagnostics().empty() || !kb::particles::ParticlePlayback::HasBackend(scene)) {
        io.err << "error: particle provider could not initialize\n";
        for (const auto& diagnostic : scene.ModuleDiagnostics()) io.err << diagnostic << '\n';
        return 1;
    }
    if (!MountProjectAssets(scene, projectRoot, error)) { io.err << "error: " << error << '\n'; return 1; }
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath(effect->virtualPath);
    if (metadata == nullptr) { io.err << "error: particle effect was not discovered\n"; return 1; }
    ProfileSurface surface;
    if (surface.NativeWindowHandle() == nullptr) { io.err << "error: hidden render surface unavailable\n"; return 1; }
    Renderer renderer;
    DisplayConfig config;
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(rendererType);
    config.particleGpuBufferBudgetBytes = options.bufferBudget;
    if (!renderer.Initialize(surface, &config) || bgfx::getRendererType() != rendererType) {
        io.err << "error: requested bgfx backend unavailable\n"; return 1;
    }
    ProfileTarget target;
    if (!target.target.Ensure(64U, 64U)) { io.err << "error: profile target unavailable\n"; return 1; }
    RenderSceneSubmitDesc desc{};
    desc.target = {.frameBuffer = target.target.FrameBuffer(), .colorTexture = target.target.ColorTexture(),
        .resolvedColorTexture = target.target.ResolvedColorTexture(), .depthTexture = target.target.DepthTexture(),
        .viewport = {.id = {1U}, .extent = {64U, 64U}}, .colorFormat = target.target.ColorSelection().format,
        .depthFormat = target.target.DepthSelection().format};
    SceneRenderCamera camera{};
    bx::mtxLookAt(camera.view.data(), bx::Vec3{0.0F, 0.0F, -10.0F}, bx::Vec3{0.0F, 0.0F, 0.0F});
    SceneDepthPolicy::MakePerspective(camera.projection.data(), 60.0F, 1.0F, 0.1F, 100.0F, SceneDepthPolicy::HomogeneousDepth());
    desc.cameraOverride = camera;
    desc.meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent;
    desc.clearRgba = 0x000000FFU;
    desc.editorSceneOverlaysEnabled = false;
    desc.shadowPassEnabled = false;
    desc.postProcessEnabled = false;
    desc.selectionMaskEnabled = false;
    desc.selectionOutlineEnabled = false;
    renderer.PrepareParticleScene(scene);
    memory.Set("empty_host", MemorySnapshot(&renderer));
    const auto owner = scene.Entities().CreateEntity();
    scene.Transforms().Set(owner, {});
    scene.Components().ParticleEffects().Set(owner, {.effectAssetId = metadata->id.value,
        .deterministicSeed = 1U, .enabled = true, .autoPlay = true, .followTransform = true, .restartOnActivate = true});
    constexpr float delta = 1.0F / 60.0F;
    const auto deadline = Clock::now() + std::chrono::minutes(2);
    const auto frame = [&] {
        if (Clock::now() > deadline || !scene.Runtime().Update(delta) || !renderer.BeginFrame()) return false;
        renderer.SetFrameDeltaSeconds(delta);
        const bool submitted = renderer.SubmitScene(scene, desc);
        renderer.EndFrame();
        return submitted;
    };
    // Always admit the source before warmup, including a requested zero warmup.
    if (!frame()) { io.err << "error: effect admission failed\n"; return 1; }
    for (std::uint32_t i = 0U; i < options.warmup; ++i) {
        if (!frame()) { io.err << "error: warmup failed or exceeded two minutes\n"; return 1; }
    }
    memory.Set("after_warmup", MemorySnapshot(&renderer));
    // LastCompletedFrame is the front-end frame returned by bgfx::frame, not a completed GPU query.
    const auto firstFrame = renderer.LastCompletedFrame() + 1U;
    const auto lastFrame = firstFrame + options.frames - 1U;
    std::set<std::uint32_t> timedFrames;
    std::vector<double> gpuTimes;
    std::vector<double> wallTimes;
    gpuTimes.reserve(options.frames);
    wallTimes.reserve(options.frames);
    std::uint64_t computeSubmissions = 0U;
    std::uint64_t drawSubmissions = 0U;
    const auto collectGpu = [&] {
        const auto* stats = bgfx::getStats();
        if (stats && stats->gpuFrameNum >= firstFrame && stats->gpuFrameNum <= lastFrame &&
            stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin && timedFrames.insert(stats->gpuFrameNum).second) {
            gpuTimes.push_back(static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1.0e9 / static_cast<double>(stats->gpuTimerFreq));
        }
    };
    const auto processBefore = CpuNanoseconds(true);
    const auto threadBefore = CpuNanoseconds(false);
    bool renderingSucceeded = true;
    for (std::uint32_t i = 0U; i < options.frames; ++i) {
        const auto begin = Clock::now();
        if (!frame()) { io.err << "error: measured frame failed or exceeded two minutes\n"; return 1; }
        renderingSucceeded = renderingSucceeded && !renderer.LastSceneDiagnostics().HasErrors() &&
            !renderer.LastSceneSubmitStats().HasMissingResources() && renderer.LastSceneSubmitStats().droppedInstanceCount == 0U;
        wallTimes.push_back(std::chrono::duration<double, std::nano>(Clock::now() - begin).count());
        const auto* stats = bgfx::getStats();
        if (stats) {
            computeSubmissions = std::max(computeSubmissions, static_cast<std::uint64_t>(stats->numCompute));
            drawSubmissions = std::max(drawSubmissions, static_cast<std::uint64_t>(stats->numDraw));
        }
        collectGpu();
    }
    const auto threadAfter = CpuNanoseconds(false);
    const auto processAfter = CpuNanoseconds(true);
    const auto particleDraws = renderer.LastSceneSubmitStats().submittedParticleDrawCallCount;
    memory.Set("after_measurement", MemorySnapshot(&renderer));
    // Drain delayed timer queries without changing the scene or including readback in timings.
    for (std::uint32_t i = 0U; i < 16U; ++i) {
        if (!renderer.BeginFrame()) { io.err << "error: timer drain failed\n"; return 1; }
        renderer.EndFrame();
        collectGpu();
    }
    auto report = JsonValue::MakeObject();
    report.Set("schema", JsonValue::MakeString("21kb.particle-profile/v1"));
    report.Set("backend", JsonValue::MakeString(backend));
    report.Set("effect", JsonValue::MakeString(effect->virtualPath));
    report.Set("scope", JsonValue::MakeString("isolated effect, whole scene update and renderer, 64x64 offscreen target"));
    report.Set("fixed_delta_seconds", JsonValue::MakeNumber(delta));
    report.Set("admission_frames", JsonValue::MakeNumber(1.0));
    report.Set("warmup_frames", JsonValue::MakeNumber(options.warmup));
    report.Set("measured_frames", JsonValue::MakeNumber(options.frames));
    report.Set("gpu_buffer_budget_bytes", Integer(options.bufferBudget == 0U ? kb::scene::kParticleEffectMaxGpuResourceBytes : options.bufferBudget));
    report.Set("first_measured_bgfx_frame", Integer(firstFrame));
    report.Set("last_measured_bgfx_frame", Integer(lastFrame));
    report.Set("process_cpu_ns", CpuDifference(processBefore, processAfter));
    report.Set("calling_thread_cpu_ns", CpuDifference(threadBefore, threadAfter));
    report.Set("frame_wall", Distribution(std::move(wallTimes)));
    report.Set("frame_gpu", Distribution(gpuTimes));
    auto gpuFrameIds = JsonValue::MakeArray();
    for (const auto id : timedFrames) gpuFrameIds.Append(Integer(id));
    report.Set("gpu_timed_bgfx_frames", std::move(gpuFrameIds));
    report.Set("gpu_timer_scope", JsonValue::MakeString("unique completed bgfx GPU frames in measured submission range; drain and readback excluded"));
    report.Set("observed_max_bgfx_compute_submissions", Integer(computeSubmissions));
    report.Set("observed_max_bgfx_draw_submissions", Integer(drawSubmissions));
    report.Set("last_particle_draw_calls", Integer(particleDraws));
    auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    bool allGpu = snapshot && !snapshot->Emitters().empty();
    auto emitters = JsonValue::MakeArray();
    if (snapshot) {
        report.Set("fixed_step", Integer(snapshot->Header().fixedStepIndex));
        for (const auto& emitter : snapshot->Emitters()) {
            auto row = JsonValue::MakeObject();
            const bool gpu = emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Gpu;
            allGpu = allGpu && gpu && emitter.gpuFallbackReason == kb::particles::ParticleGpuFallbackReason::None && emitter.particleCount == 0U;
            row.Set("instance_id", Integer(emitter.instanceId));
            row.Set("emitter_id", Integer(emitter.emitterId));
            row.Set("path", JsonValue::MakeString(gpu ? "gpu" : "cpu"));
            row.Set("reason", JsonValue::MakeString(std::string{kb::particles::ParticleGpuFallbackReasonName(emitter.gpuFallbackReason)}));
            row.Set("cpu_render_record_count", JsonValue::MakeNumber(emitter.particleCount));
            row.Set("rejected_by_capacity", JsonValue::MakeNumber(emitter.rejectedByCapacity));
            row.Set("rejected_by_spawn_budget", JsonValue::MakeNumber(emitter.rejectedBySpawnBudget));
            row.Set("rejected_by_event_budget", JsonValue::MakeNumber(emitter.rejectedByEventBudget));
            row.Set("dropped_particle_count", JsonValue::MakeNumber(emitter.droppedParticleCount));
            row.Set("cpu_render_payload_bytes", Integer(static_cast<std::uint64_t>(emitter.particleCount) * sizeof(kb::particles::ParticleRenderRecord)));
            auto cpuSamples = JsonValue::MakeArray();
            const auto records = snapshot->Particles();
            if (emitter.firstParticle <= records.size() && emitter.particleCount <= records.size() - emitter.firstParticle) {
                for (const auto& particle : records.subspan(emitter.firstParticle, std::min(emitter.particleCount, options.samples))) {
                    auto sample = JsonValue::MakeArray();
                    for (const float value : {particle.position.x, particle.position.y, particle.position.z,
                        particle.velocity.x, particle.velocity.y, particle.velocity.z}) sample.Append(JsonValue::MakeNumber(value));
                    cpuSamples.Append(std::move(sample));
                }
            }
            row.Set("cpu_position_velocity_samples", std::move(cpuSamples));
            emitters.Append(std::move(row));
        }
    }
    report.Set("emitters", std::move(emitters));
    auto captures = JsonValue::MakeArray();
    bool readbackSucceeded = true;
    const auto draws = renderer.GpuParticleDraws(scene);
    const auto rejectedUpdates = renderer.RejectedGpuParticleUpdates(scene);
    if (snapshot) {
        for (const auto& emitter : snapshot->Emitters()) {
            if (emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Gpu) {
                allGpu = allGpu && std::any_of(draws.begin(), draws.end(), [&](const auto& draw) {
                    return draw.key.instanceId == emitter.instanceId && draw.key.emitterId == emitter.emitterId;
                });
            }
        }
    }
    for (const auto& draw : draws) {
        auto row = JsonValue::MakeObject();
        row.Set("instance_id", Integer(draw.key.instanceId));
        row.Set("emitter_id", Integer(draw.key.emitterId));
        row.Set("capacity", JsonValue::MakeNumber(draw.capacity));
        row.Set("output_enum", JsonValue::MakeNumber(static_cast<std::uint8_t>(draw.params->output)));
        row.Set("ordered_motion_operation_count", JsonValue::MakeNumber(draw.params->motionModuleCount));
        row.Set("plane_kill_on_contact", JsonValue::MakeBool(draw.params->hasPlane && draw.params->plane.killOnContact));
        row.Set("requested_buffer_bytes", Integer(draw.requestedBufferBytes));
        ParticleGpuReadback capture;
        const bool captured = capture.Read(draw, std::min(options.samples, draw.capacity));
        readbackSucceeded = readbackSucceeded && captured;
        row.Set("readback_succeeded", JsonValue::MakeBool(captured));
        auto records = JsonValue::MakeArray();
        if (captured) for (const auto& record : capture.Records()) {
            auto values = JsonValue::MakeArray();
            for (const auto value : record) values.Append(JsonValue::MakeNumber(value));
            records.Append(std::move(values));
        }
        row.Set("raw_instance_records", std::move(records));
        captures.Append(std::move(row));
    }
    const bool execution = allGpu && !draws.empty() && readbackSucceeded && rejectedUpdates.empty() && computeSubmissions > 0U && particleDraws > 0U;
    report.Set("gpu_execution_observed", JsonValue::MakeBool(execution));
    report.Set("gpu_captures", std::move(captures));
    report.Set("readback_outside_measurement", JsonValue::MakeBool(true));
    auto rejections = JsonValue::MakeArray();
    for (const auto& rejection : rejectedUpdates) {
        auto row = JsonValue::MakeObject();
        row.Set("instance_id", Integer(rejection.key.instanceId));
        row.Set("emitter_id", Integer(rejection.key.emitterId));
        row.Set("reason", JsonValue::MakeString(std::string{ParticleGpuUpdateFailureName(rejection.failure)}));
        rejections.Append(std::move(row));
    }
    report.Set("rejected_gpu_updates", std::move(rejections));
    const bool succeeded = snapshot && !snapshot->Emitters().empty() && readbackSucceeded && renderingSucceeded && rejectedUpdates.empty() &&
        (!arguments.Flag("--require-gpu") || (execution && !gpuTimes.empty()));
    report.Set("succeeded", JsonValue::MakeBool(succeeded));
    snapshot.reset();
    renderer.ReleaseScene(scene);
    memory.Set("after_release", MemorySnapshot(&renderer));
    target.target.Shutdown();
    renderer.Shutdown();
    memory.Set("after_renderer_shutdown", MemorySnapshot());
    sceneOwner.reset();
    memory.Set("after_scene_shutdown", MemorySnapshot());
    report.Set("memory", std::move(memory));
    io.out << report.Dump() << '\n';
    if (!succeeded) io.err << "error: effect profiling did not satisfy the requested execution/readback/timing requirements\n";
    return succeeded && io.out.good() ? 0 : 1;
}
#endif

} // namespace

int RunVfxProfileCommand(const ArgumentList& arguments, CommandIo io) {
    const auto options = ParseProfileOptions(arguments, io);
    if (!options) return 1;
#if defined(_WIN32)
    return Profile(arguments, *options, io);
#else
    io.err << "error: vfx-profile has no native hidden GPU host on this platform\n";
    return 1;
#endif
}

} // namespace kb::cli
