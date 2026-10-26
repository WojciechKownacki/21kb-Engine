#include "CliCommands.hpp"
#include "CliAudioJson.hpp"
#include "CliAudioMeterJson.hpp"

#include "engine/core/JsonValue.hpp"
#include "engine/ecs/SystemSchedulerTraceExport.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldTelemetryExport.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/world/WorldPartitionRuntime.hpp"

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace kb::cli {

namespace {

// The ECS scheduler of the last simulated frame. Wall time and CPU time are separate numbers: cpu_time_ns adds up the
// durations of all systems over all workers, so it can exceed the frame's wall time.
[[nodiscard]] kb::core::JsonValue EcsSchedulerJson(const kb::ecs::SystemSchedulerTrace& trace) {
    using kb::core::JsonValue;
    auto scheduler = JsonValue::MakeObject();
    scheduler.Set("scope", JsonValue::MakeString("ECS world scheduler, last simulated frame; the physics and render stages have their own schedulers and are not in this trace"));
    scheduler.Set("frame_index", JsonValue::MakeNumber(static_cast<double>(trace.frameIndex)));
    scheduler.Set("frame_wall_ns", JsonValue::MakeNumber(static_cast<double>(trace.frameDurationNanoseconds)));
    scheduler.Set("cpu_time_ns", JsonValue::MakeNumber(static_cast<double>(kb::ecs::SumSystemSchedulerCpuNanoseconds(trace))));
    scheduler.Set("cpu_time_basis", JsonValue::MakeString("sum of the durations of all systems over all workers; not wall time and may exceed frame_wall_ns"));
    scheduler.Set("worker_count", JsonValue::MakeNumber(static_cast<double>(trace.workers.size())));
    auto systems = JsonValue::MakeArray();
    for (const kb::ecs::SystemSchedulerTraceEvent& event : trace.events) {
        auto entry = JsonValue::MakeObject();
        entry.Set("name", JsonValue::MakeString(event.systemName));
        entry.Set("stage", JsonValue::MakeNumber(static_cast<double>(event.stageIndex)));
        entry.Set("worker", JsonValue::MakeNumber(static_cast<double>(event.workerIndex)));
        entry.Set("start_ns", JsonValue::MakeNumber(static_cast<double>(event.startTimeNanoseconds)));
        entry.Set("end_ns", JsonValue::MakeNumber(static_cast<double>(event.endTimeNanoseconds)));
        entry.Set("duration_ns", JsonValue::MakeNumber(static_cast<double>(event.durationNanoseconds)));
        entry.Set("wait_ns", JsonValue::MakeNumber(static_cast<double>(event.waitTimeNanoseconds)));
        entry.Set("wait_reason", JsonValue::MakeString(event.waitReason));
        auto blockedBy = JsonValue::MakeArray();
        for (const std::string& dependency : event.blockedDependencies) {
            blockedBy.Append(JsonValue::MakeString(dependency));
        }
        entry.Set("blocked_by", std::move(blockedBy));
        systems.Append(std::move(entry));
    }
    scheduler.Set("systems", std::move(systems));
    const kb::ecs::SystemSchedulerCriticalPath path = kb::ecs::ComputeSystemSchedulerCriticalPath(trace);
    auto critical = JsonValue::MakeObject();
    auto names = JsonValue::MakeArray();
    for (const std::string& name : path.systems) {
        names.Append(JsonValue::MakeString(name));
    }
    critical.Set("systems", std::move(names));
    critical.Set("wall_ns", JsonValue::MakeNumber(static_cast<double>(path.wallNanoseconds)));
    critical.Set("busy_ns", JsonValue::MakeNumber(static_cast<double>(path.busyNanoseconds)));
    critical.Set("basis", JsonValue::MakeString("from the system that finished last, the dependency that finished last of each system; wall_ns includes waits, busy_ns does not"));
    scheduler.Set("critical_path", std::move(critical));
    return scheduler;
}

[[nodiscard]] std::optional<int> ParseInt(const std::string& text) noexcept {
    try {
        std::size_t consumed = 0U;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<float> ParseFloat(const std::string& text) noexcept {
    try {
        std::size_t consumed = 0U;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

void RegisterStdoutLog(kb::script::ScriptRuntimeHost& host, std::ostream& out) {
    kb::script::ScriptFunctionDesc logDesc;
    logDesc.signature.name = "Log";
    logDesc.signature.inputs = { kb::script::ScriptFunctionPin{ "message", kb::script::ScriptValueType::String, true } };
    logDesc.callback = [&out](
                           const kb::script::ScriptFunctionCallContext&,
                           std::span<const kb::script::ScriptFunctionArgument> functionArguments) {
        for (const kb::script::ScriptFunctionArgument& argument : functionArguments) {
            if (argument.name == "message") {
                out << "[log] " << argument.value.AsString() << '\n';
                break;
            }
        }
        return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
    };
    static_cast<void>(host.RegisterFunction(std::move(logDesc)));
}

struct DiagnosticPrinter {
    kb::scene::Scene& scene;
    std::ostream& err;
    std::unordered_set<std::string> printed;
    std::size_t total = 0U;

    // The script's path, or what to do about a reference to a file that is not there.
    [[nodiscard]] std::string ScriptName(kb::assets::AssetId assetId) const {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId);
        return metadata != nullptr ? metadata->virtualPath.generic_string()
                                   : "missing script " + kb::assets::ToString(assetId) +
                " (moved, renamed or deleted - attach the script to the object again)";
    }

    void PrintScript(const std::vector<kb::script::ScriptDiagnostic>& diagnostics, std::size_t frame) {
        for (const kb::script::ScriptDiagnostic& diagnostic : diagnostics) {
            ++total;
            std::string line = "[script error] ";
            const std::string entity = scene.Entities().Name(diagnostic.entity);
            if (!entity.empty()) {
                line += "'" + std::string{ entity } + "', ";
            }
            line += ScriptName(diagnostic.assetId);
            line += ": " + diagnostic.message;
            if (printed.insert(line).second) {
                err << "frame " << frame << ' ' << line << '\n';
            }
        }
    }

    void PrintPrepare(const std::vector<kb::script::ScriptRuntimeAssetPrepareDiagnostic>& diagnostics, std::size_t frame) {
        for (const kb::script::ScriptRuntimeAssetPrepareDiagnostic& diagnostic : diagnostics) {
            ++total;
            std::string line = "[compile error] ";
            line += ScriptName(diagnostic.assetId);
            line += ": " + diagnostic.message;
            if (printed.insert(line).second) {
                err << "frame " << frame << ' ' << line << '\n';
            }
        }
    }

    void PrintSceneSystems(const std::vector<std::string>& diagnostics, std::size_t frame) {
        for (const std::string& diagnostic : diagnostics) {
            ++total;
            const std::string line = "[scene system error] " + diagnostic;
            if (printed.insert(line).second) {
                err << "frame " << frame << ' ' << line << '\n';
            }
        }
    }
};

[[nodiscard]] kb::core::JsonValue ParticleExecutionReport(
    const std::shared_ptr<const kb::particles::ParticleRenderSnapshot>& snapshot,
    bool providerAvailable, std::uint64_t updateWallNanoseconds, bool succeeded) {
    using kb::core::JsonValue;
    auto report = JsonValue::MakeObject();
    report.Set("schema", JsonValue::MakeString("21kb.particle-execution/v1"));
    report.Set("succeeded", JsonValue::MakeBool(succeeded));
    report.Set("provider_available", JsonValue::MakeBool(providerAvailable));
    report.Set("snapshot_available", JsonValue::MakeBool(snapshot != nullptr));
    report.Set("snapshot_revision", snapshot ? JsonValue::MakeString(std::to_string(snapshot->Revision())) : JsonValue::MakeNull());
    report.Set("fixed_step", snapshot ? JsonValue::MakeString(std::to_string(snapshot->Header().fixedStepIndex)) : JsonValue::MakeNull());
    report.Set("runtime_update_wall_ns", JsonValue::MakeString(std::to_string(updateWallNanoseconds)));
    report.Set("gpu_time_ns", JsonValue::MakeNull());
    auto emitters = JsonValue::MakeArray();
    if (snapshot) {
        for (const auto& emitter : snapshot->Emitters()) {
            auto row = JsonValue::MakeObject();
            row.Set("instance_id", JsonValue::MakeString(std::to_string(emitter.instanceId)));
            row.Set("effect_asset_id", JsonValue::MakeString(std::to_string(emitter.effectAssetId)));
            row.Set("emitter_id", JsonValue::MakeString(std::to_string(emitter.emitterId)));
            row.Set("path", JsonValue::MakeString(emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Gpu ? "gpu" : "cpu"));
            row.Set("reason", JsonValue::MakeString(std::string{kb::particles::ParticleGpuFallbackReasonName(emitter.gpuFallbackReason)}));
            row.Set("cpu_render_record_count", JsonValue::MakeNumber(emitter.particleCount));
            row.Set("cpu_render_payload_bytes", JsonValue::MakeNumber(
                static_cast<double>(emitter.particleCount) * sizeof(kb::particles::ParticleRenderRecord)));
            row.Set("cpu_time_ns", JsonValue::MakeNull());
            emitters.Append(std::move(row));
        }
    }
    report.Set("emitters", std::move(emitters));
    return report;
}

// What the streamed worlds of the scene hold, by kind, against what the streaming budget charged (see docs/world_memory.md).
[[nodiscard]] kb::core::JsonValue WorldMemoryJson(const kb::world::WorldMemoryReport& memory) {
    using kb::core::JsonValue;
    static constexpr const char* kKindNames[kb::world::kWorldMemoryKindCount] = { "assets_cpu", "instances", "scripts", "physics", "gpu" };
    static constexpr const char* kSourceNames[] = { "unavailable", "estimated", "measured" };
    auto categories = JsonValue::MakeArray();
    for (const kb::world::WorldMemoryCategory& category : memory.categories) {
        auto row = JsonValue::MakeObject();
        row.Set("kind", JsonValue::MakeString(kKindNames[static_cast<std::size_t>(category.kind)]));
        row.Set("source", JsonValue::MakeString(kSourceNames[static_cast<std::size_t>(category.source)]));
        row.Set("bytes", category.source == kb::world::WorldMemorySource::Unavailable ? JsonValue::MakeNull() : JsonValue::MakeNumber(static_cast<double>(category.bytes)));
        row.Set("baseline_bytes", category.source == kb::world::WorldMemorySource::Unavailable ? JsonValue::MakeNull() : JsonValue::MakeNumber(static_cast<double>(category.baselineBytes)));
        row.Set("note", JsonValue::MakeString(category.note));
        categories.Append(std::move(row));
    }
    auto report = JsonValue::MakeObject();
    report.Set("budget_bytes", JsonValue::MakeNumber(static_cast<double>(memory.budgetBytes)));
    report.Set("estimated_resident_bytes", JsonValue::MakeNumber(static_cast<double>(memory.estimatedResidentBytes)));
    report.Set("resident_units", JsonValue::MakeNumber(static_cast<double>(memory.residentUnits)));
    report.Set("measured_bytes", JsonValue::MakeNumber(static_cast<double>(memory.measuredBytes)));
    report.Set("has_unavailable_kinds", JsonValue::MakeBool(memory.hasUnavailableKinds));
    report.Set("error_ratio", JsonValue::MakeNumber(memory.errorRatio));
    report.Set("minimum_error_ratio", JsonValue::MakeNumber(memory.minimumErrorRatio));
    report.Set("maximum_error_ratio", JsonValue::MakeNumber(memory.maximumErrorRatio));
    report.Set("samples", JsonValue::MakeNumber(static_cast<double>(memory.samples)));
    report.Set("categories", std::move(categories));
    return report;
}

int RunSceneRuntime(const ArgumentList& arguments, CommandIo io, std::ostream* particleReport, std::ostream* audioReport = nullptr,
    std::ostream* ecsReport = nullptr, std::ostream* worldMemoryReport = nullptr) {
    const bool requireMeters = arguments.Option("--report") == std::optional<std::string>{"audio-meters"};
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: run requires --project <dir>\n";
        return 1;
    }

    int frames = 60;
    if (const std::optional<std::string> framesOption = arguments.Option("--frames"); framesOption.has_value()) {
        const std::optional<int> parsed = ParseInt(*framesOption);
        if (!parsed.has_value() || *parsed < 1) {
            io.err << "error: --frames expects a positive integer\n";
            return 1;
        }
        frames = *parsed;
    }

    float deltaSeconds = 1.0F / 60.0F;
    if (const std::optional<std::string> dtOption = arguments.Option("--dt"); dtOption.has_value()) {
        const std::optional<float> parsed = ParseFloat(*dtOption);
        if (!parsed.has_value() || !std::isfinite(*parsed) || *parsed <= 0.0F) {
            io.err << "error: --dt expects a positive number of seconds\n";
            return 1;
        }
        deltaSeconds = *parsed;
    }

    const bool quiet = arguments.Flag("--quiet");
    if (audioReport != nullptr && frames > 600) {
        io.err << "error: an audio execution probe accepts at most 600 simulation frames\n"; return 1;
    }

    std::error_code pathError;
    const std::filesystem::path projectRoot =
        std::filesystem::absolute(std::filesystem::path{ *project }, pathError).lexically_normal();
    if (pathError || !std::filesystem::is_directory(projectRoot, pathError) || pathError) {
        io.err << "error: project directory was not found: " << *project << '\n';
        return 1;
    }

    std::string projectError;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(projectRoot, projectError);
    if (projectFile.empty()) {
        io.err << "error: " << projectError << '\n';
        return 1;
    }
    kb::project::ProjectDescriptorReadResult loadedProject =
        kb::project::ProjectManager::LoadProject(projectFile);
    if (!loadedProject.succeeded) {
        io.err << "error: could not load project descriptor "
               << projectFile.generic_string() << ": " << loadedProject.error << '\n';
        return 1;
    }
    const kb::project::ParticleProjectPolicyResult particlePolicy =
        kb::project::ParticleProjectPolicy::Inspect(projectRoot, loadedProject.descriptor);
    if (!particlePolicy.IsRunnable()) {
        io.err << "error: " << particlePolicy.diagnostic << '\n';
        return 1;
    }

    // A persisted editor descriptor intentionally stores portable plugin
    // filenames. Prefer a project-local binary when one is packaged beside
    // the project; otherwise leave the filename intact so EngineModuleLoader
    // can resolve the current build/install layout.
    std::vector<std::string> requiredModules;
    for (kb::project::ProjectPluginReference& plugin : loadedProject.descriptor.plugins) {
        if (!plugin.enabled) {
            continue;
        }
        if (!plugin.name.empty()) {
            requiredModules.push_back(plugin.name);
        }
        const std::filesystem::path configuredPath{ plugin.binaryPath };
        if (configuredPath.empty() || configuredPath.is_absolute()) {
            continue;
        }
        const std::filesystem::path projectLocalPath = projectRoot / configuredPath;
        std::error_code pluginPathError;
        if (std::filesystem::is_regular_file(projectLocalPath, pluginPathError) && !pluginPathError) {
            plugin.binaryPath = projectLocalPath.string();
        }
    }

    // The physics layers a project uses are a setting, read from the project's own
    // settings file; an older project still carries them in its descriptor.
    const kb::project::ProjectSettingsLoadResult projectSettings =
        kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(projectRoot));
    const std::string physicsLayersAsset = projectSettings.found && projectSettings.Succeeded()
        ? projectSettings.settings.physicsLayersAsset
        : loadedProject.legacySettings.physicsLayersAsset;
    kb::scene::Scene scene{ std::move(loadedProject.descriptor) };
    // A headless run is a determinism tool: scripts see each physics step in the update that ran it.
    kb::scene::PhysicsBackend::SetStepPipelining(scene, false);
    if (!scene.ModuleDiagnostics().empty()) {
        for (const std::string& diagnostic : scene.ModuleDiagnostics()) {
            io.err << "[module error] " << diagnostic << '\n';
        }
        return 1;
    }
    for (const std::string& module : requiredModules) {
        if (!scene.IsModuleActive(module)) {
            io.err << "error: configured module did not become active: " << module << '\n';
            return 1;
        }
        io.out << "[module] active " << module << '\n';
    }

    std::string error;
    std::size_t discovered = 0U;
    if (!MountProjectAssets(scene, projectRoot.string(), error, &discovered)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (!physicsLayersAsset.empty() &&
        !kb::scene::PhysicsBackend::LoadAndConfigureLayers(scene, physicsLayersAsset)) {
        io.err << "error: project physics layers could not be loaded and applied: "
               << physicsLayersAsset << '\n';
        return 1;
    }

    // Resolve and load the scene file exactly the way scene-list does: virtual
    // paths go through the registry, physical paths through the project root.
    // Without --scene: the scene the game starts in, like entity/component and the game itself.
    std::filesystem::path scenePath;
    std::optional<std::string> sceneOption = arguments.Option("--scene");
    if (!sceneOption.has_value()) {
        const std::string startup = kb::project::ProjectSettingsStore::Load(
            kb::project::ProjectSettingsStore::FilePath(projectRoot)).settings.defaultMap;
        if (!startup.empty()) {
            sceneOption = startup;
        }
    }
    if (sceneOption.has_value()) {
        if (scene.Assets().Manager().Mounts().Resolve(*sceneOption).has_value()) {
            const kb::assets::AssetMetadata* metadata =
                FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), *sceneOption);
            if (metadata == nullptr) {
                io.err << "error: scene asset was not found: " << *sceneOption << '\n';
                return 1;
            }
            scenePath = metadata->physicalPath;
        } else {
            std::string outsideError;
            scenePath = ResolveProjectFile(*sceneOption, projectRoot, outsideError);
            if (scenePath.empty()) {
                io.err << "error: " << outsideError << '\n';
                return 1;
            }
        }
    } else {
        io.err << "error: the project has no startup scene; pass --scene <path>\n";
        return 1;
    }

    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(scene, scenePath)) {
        io.err << "error: could not load scene into runtime: " << scenePath.generic_string() << '\n';
        return 1;
    }

    std::uint32_t pcmFramesPerTick = 0U;
    if (audioReport != nullptr) {
        if (!kb::audio::AudioPlayback::ConfigureOffline(scene)) {
            io.err << "error: the active audio provider could not configure offline rendering\n"; return 1;
        }
        const auto format = kb::audio::AudioPlayback::DspReport(scene);
        double requested = std::round(static_cast<double>(format.sampleRate) * deltaSeconds);
        if (const auto option = arguments.Option("--pcm-frames"); option.has_value()) {
            const auto parsed = ParseInt(*option);
            if (!parsed.has_value()) { io.err << "error: --pcm-frames requires a positive integer\n"; return 1; }
            requested = *parsed;
        }
        if (requested < 1.0 || requested > kb::audio::kMaxOfflineAudioFramesPerRead
            || requested * frames > 1920000.0) {
            io.err << "error: audio probes require 1..384000 PCM frames per tick and at most 1920000 total frames\n"; return 1;
        }
        pcmFramesPerTick = static_cast<std::uint32_t>(requested);
        if (requireMeters && !kb::audio::AudioMetering::Configure(scene, true)) {
            io.err << "error: the active audio provider could not enable signal meters\n"; return 1;
        }
    }

    kb::script::ScriptRuntimeHost host{ scene };
    RegisterStdoutLog(host, io.out);
    if (!host.Succeeded()) {
        for (const std::string& diagnostic : host.Diagnostics()) {
            io.err << "[host error] " << diagnostic << '\n';
        }
        return 1;
    }

    if (!host.InstallSceneSystem()) {
        io.err << "error: script scene system could not be installed\n";
        return 1;
    }
    // The script heap is the one kind of memory only this host can read; the engine reads the instances and the assets itself.
    std::optional<kb::world::WorldPartitionRuntime> worlds;
    if (worldMemoryReport != nullptr) {
        worlds.emplace(scene);
        worlds->SetMemoryProbe(kb::world::WorldMemoryKind::Scripts, [&host]() -> std::optional<std::uint64_t> {
            return static_cast<std::uint64_t>(host.LuaRuntime().LuaMemoryUsedBytes());
        });
    }

    io.out << "running " << scenePath.filename().generic_string()
           << " for " << frames << " frames (dt " << deltaSeconds << "s, "
           << discovered << " assets discovered, "
           << scene.ActiveModuleCount() << " modules active)\n";

    DiagnosticPrinter printer{ .scene = scene, .err = io.err, .printed = {}, .total = 0U };
    std::size_t executedBehaviours = 0U;
    std::size_t emittedEvents = 0U;

    const kb::script::ScriptRuntimeExecutionResult* startup = host.InstalledSceneSystemLastResult();
    const kb::script::ScriptRuntimeAssetPrepareResult* startupPrepare = host.InstalledSceneSystemLastPrepareResult();
    if (startup == nullptr || startupPrepare == nullptr) {
        io.err << "error: installed script scene system did not expose startup state\n";
        return 1;
    }
    executedBehaviours += startup->executedBehaviours;
    emittedEvents += startup->emittedEvents.size();
    printer.PrintScript(startup->diagnostics, 0U);
    printer.PrintPrepare(startupPrepare->diagnostics, 0U);
    printer.PrintSceneSystems(scene.Runtime().DrainSceneSystemErrors(), 0U);
    static_cast<void>(host.DrainSceneSystemDiagnostics());

    std::uint64_t updateWallNanoseconds = 0U;
    std::uint64_t audioFrames = 0U, audioSamples = 0U, audioAboveUnit = 0U, mixWallNanoseconds = 0U;
    double audioEnergy = 0.0, audioPeak = 0.0;
    bool audioSucceeded = true;
    std::string audioError;
    int completedFrames = 0;
    if (ecsReport != nullptr) {
        scene.Runtime().SetEcsProfilerEnabled(true);
    }
    for (int frame = 1; frame <= frames; ++frame) {
        const auto updateStarted = std::chrono::steady_clock::now();
        static_cast<void>(scene.Runtime().Update(deltaSeconds));
        updateWallNanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - updateStarted).count());
        ++completedFrames;
        if (audioReport != nullptr) {
            const auto started = std::chrono::steady_clock::now();
            const auto pcm = kb::audio::AudioPlayback::RenderOffline(scene, pcmFramesPerTick);
            mixWallNanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
            audioFrames += pcm.frames;
            audioSamples += pcm.samples.size();
            for (const float sample : pcm.samples) {
                const double value = sample;
                audioPeak = std::max(audioPeak, std::abs(value)); audioEnergy += value * value;
                if (std::abs(value) > 1.0) ++audioAboveUnit;
            }
            if (!pcm.succeeded || pcm.frames != pcmFramesPerTick) {
                audioSucceeded = false;
                audioError = pcm.error.empty() ? "no complete audio PCM block was produced" : pcm.error;
                io.err << "error: " << audioError << '\n';
            }
        }
        const kb::script::ScriptRuntimeExecutionResult* result = host.InstalledSceneSystemLastResult();
        const kb::script::ScriptRuntimeAssetPrepareResult* prepare = host.InstalledSceneSystemLastPrepareResult();
        if (result == nullptr || prepare == nullptr) {
            io.err << "error: installed script scene system became unavailable\n";
            return 1;
        }
        executedBehaviours += result->executedBehaviours;
        emittedEvents += result->emittedEvents.size();
        if (!quiet) {
            for (const kb::script::ScriptEvent& event : result->emittedEvents) {
                io.out << "[event] frame " << frame << ' ' << event.name
                       << " (sender " << event.sender.Id() << ")\n";
            }
        }
        printer.PrintScript(result->diagnostics, static_cast<std::size_t>(frame));
        printer.PrintPrepare(prepare->diagnostics, static_cast<std::size_t>(frame));
        printer.PrintSceneSystems(scene.Runtime().DrainSceneSystemErrors(), static_cast<std::size_t>(frame));
        static_cast<void>(host.DrainSceneSystemDiagnostics());
        if (!audioSucceeded) break;
    }

    const auto particleSnapshot = particleReport != nullptr
        ? kb::particles::ParticlePlayback::ReadRenderSnapshot(scene) : nullptr;
    const bool particleProviderAvailable = kb::particles::ParticlePlayback::HasBackend(scene);
    const auto audioSnapshot = audioReport != nullptr ? kb::audio::AudioPlayback::DspReport(scene) : kb::audio::AudioDspRuntimeReport{};
    const auto meterSnapshot = requireMeters ? kb::audio::AudioMetering::Report(scene) : kb::audio::AudioMeterReport{};
    if (requireMeters) {
        bool valid = meterSnapshot.backendAvailable && meterSnapshot.supported && meterSnapshot.initialized
            && meterSnapshot.captureEnabled && meterSnapshot.master.available
            && meterSnapshot.master.processedFrames >= audioFrames && audioFrames > 0U
            && meterSnapshot.mixerAssetId == kb::scene::SceneAudioMixerAccess::ActiveMixer(scene);
        for (const auto& bus : meterSnapshot.buses) valid = valid && bus.available && bus.processedFrames > 0U;
        if (!valid) { audioSucceeded = false; audioError = "audio meters did not capture the actual mixer output"; }
    }
    if (audioReport != nullptr) {
        audioSucceeded = audioSucceeded && audioSnapshot.backendAvailable && audioFrames > 0U
            && audioSnapshot.mixerAssetId == kb::scene::SceneAudioMixerAccess::ActiveMixer(scene);
        for (const auto& bus : audioSnapshot.buses) {
            if (!bus.initialized || !bus.settingsCurrent) {
                audioSucceeded = false;
                io.err << "error: bus " << bus.bus << " DSP is not current: " << bus.error << '\n';
            }
        }
    }
    if (!host.DispatchShutdownLifecycle(0.0F)) {
        io.err << "error: installed script scene system could not dispatch shutdown\n";
        return 1;
    }
    const kb::script::ScriptRuntimeExecutionResult* shutdown = host.InstalledSceneSystemLastResult();
    if (shutdown == nullptr) {
        io.err << "error: installed script scene system did not expose shutdown state\n";
        return 1;
    }
    executedBehaviours += shutdown->executedBehaviours;
    printer.PrintScript(shutdown->diagnostics, static_cast<std::size_t>(frames) + 1U);
    printer.PrintSceneSystems(scene.Runtime().DrainSceneSystemErrors(), static_cast<std::size_t>(frames) + 1U);

    io.out << "done: " << completedFrames << " frames, " << executedBehaviours << " behaviour executions, "
           << emittedEvents << " events, " << printer.total << " diagnostics\n";
    if (particleReport != nullptr) {
        *particleReport << ParticleExecutionReport(particleSnapshot, particleProviderAvailable,
            updateWallNanoseconds, printer.total == 0U).Dump() << '\n';
        if (!particleReport->good()) {
            io.err << "error: particle execution report could not be written\n";
            return 1;
        }
    }
    if (worldMemoryReport != nullptr && worlds.has_value()) {
        using kb::core::JsonValue;
        auto report = JsonValue::MakeObject();
        report.Set("schema", JsonValue::MakeString("21kb.world-memory/v1"));
        report.Set("succeeded", JsonValue::MakeBool(printer.total == 0U));
        report.Set("simulation_frames", JsonValue::MakeNumber(frames));
        report.Set("completed_frames", JsonValue::MakeNumber(completedFrames));
        report.Set("diagnostics", JsonValue::MakeNumber(static_cast<double>(printer.total)));
        report.Set("worlds", JsonValue::MakeNumber(static_cast<double>(worlds->Worlds().size())));
        report.Set("memory", WorldMemoryJson(worlds->MemoryReport()));
        *worldMemoryReport << report.Dump() << '\n';
        if (!worldMemoryReport->good()) {
            io.err << "error: the world memory report could not be written" << '\n';
            return 1;
        }
    }
    if (ecsReport != nullptr) {
        // Read only: the telemetry of the scene's ECS world after the frames ran. Which numbers are measured,
        // which are estimates and which are unavailable is stated in its metric_basis object.
        using kb::core::JsonValue;
        JsonValue telemetry;
        std::string parseError;
        if (!JsonValue::Parse(kb::ecs::WorldTelemetrySnapshotToJson(scene.Runtime().EcsWorld().TelemetrySnapshot()), telemetry, parseError)) {
            io.err << "error: the ECS telemetry could not be formatted: " << parseError << '\n';
            return 1;
        }
        auto report = JsonValue::MakeObject();
        report.Set("schema", JsonValue::MakeString("21kb.ecs-telemetry/v1"));
        report.Set("succeeded", JsonValue::MakeBool(printer.total == 0U));
        report.Set("simulation_frames", JsonValue::MakeNumber(frames));
        report.Set("completed_frames", JsonValue::MakeNumber(completedFrames));
        report.Set("diagnostics", JsonValue::MakeNumber(static_cast<double>(printer.total)));
        report.Set("telemetry", std::move(telemetry));
        report.Set("scheduler", EcsSchedulerJson(scene.Runtime().LastEcsProfilerTrace()));
        *ecsReport << report.Dump() << '\n';
        if (!ecsReport->good()) {
            io.err << "error: the ECS telemetry report could not be written" << '\n';
            return 1;
        }
    }
    if (audioReport != nullptr) {
        auto report = AudioRuntimeJson(audioSnapshot);
        using kb::core::JsonValue;
        if (requireMeters) {
            report.Set("schema", JsonValue::MakeString("21kb.audio-meters/v1"));
            report.Set("meters", AudioMetersJson(meterSnapshot));
        }
        report.Set("succeeded", JsonValue::MakeBool(printer.total == 0U && audioSucceeded));
        report.Set("error", JsonValue::MakeString(audioError));
        report.Set("simulation_frames", JsonValue::MakeNumber(frames));
        report.Set("completed_frames", JsonValue::MakeNumber(completedFrames));
        report.Set("pcm_frames", JsonValue::MakeNumber(static_cast<double>(audioFrames)));
        report.Set("peak", JsonValue::MakeNumber(audioPeak));
        report.Set("rms", JsonValue::MakeNumber(audioSamples == 0U ? 0.0 : std::sqrt(audioEnergy / static_cast<double>(audioSamples))));
        report.Set("above_unit_samples", JsonValue::MakeNumber(static_cast<double>(audioAboveUnit)));
        report.Set("mix_wall_ns", JsonValue::MakeString(std::to_string(mixWallNanoseconds)));
        *audioReport << report.Dump() << '\n';
        if (!audioReport->good()) { io.err << "error: audio execution report could not be written\n"; return 1; }
    }
    return printer.total == 0U && audioSucceeded ? 0 : 1;
}

} // namespace

int RunRunCommand(const ArgumentList& arguments, CommandIo io) {
    const auto report = arguments.Option("--report");
    if (arguments.Flag("--dry-run")) { io.err << "error: run does not support --dry-run\n"; return 1; }
    if (arguments.Flag("--json") && !report) { io.err << "error: run JSON output requires --report particles, audio, audio-meters, ecs or world-memory\n"; return 1; }
    if (arguments.Option("--pcm-frames").has_value() && (!report || (*report != "audio" && *report != "audio-meters"))) {
        io.err << "error: --pcm-frames requires --report audio or audio-meters\n"; return 1;
    }
    if (report && *report != "particles" && *report != "audio" && *report != "audio-meters" && *report != "ecs" && *report != "world-memory") {
        io.err << "error: --report expects particles, audio, audio-meters, ecs or world-memory\n";
        return 1;
    }
    if (report && *report == "ecs") return RunSceneRuntime(arguments, {.out = io.err, .err = io.err}, nullptr, nullptr, &io.out);
    if (report && *report == "world-memory") return RunSceneRuntime(arguments, {.out = io.err, .err = io.err}, nullptr, nullptr, nullptr, &io.out);
    if (report && (*report == "audio" || *report == "audio-meters")) return RunSceneRuntime(arguments, {.out = io.err, .err = io.err}, nullptr, &io.out);
    return report ? RunSceneRuntime(arguments, {.out = io.err, .err = io.err}, &io.out)
                  : RunSceneRuntime(arguments, io, nullptr);
}

} // namespace kb::cli
