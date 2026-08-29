#include "CliCommands.hpp"

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

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace kb::cli {

namespace {

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

    void PrintScript(const std::vector<kb::script::ScriptDiagnostic>& diagnostics, std::size_t frame) {
        for (const kb::script::ScriptDiagnostic& diagnostic : diagnostics) {
            ++total;
            std::string line = "[script error] ";
            const kb::assets::AssetMetadata* metadata =
                scene.Assets().Manager().Registry().Find(diagnostic.assetId);
            line += metadata != nullptr ? metadata->virtualPath.generic_string() : kb::assets::ToString(diagnostic.assetId);
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
            const kb::assets::AssetMetadata* metadata =
                scene.Assets().Manager().Registry().Find(diagnostic.assetId);
            line += metadata != nullptr ? metadata->virtualPath.generic_string() : kb::assets::ToString(diagnostic.assetId);
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

} // namespace

int RunRunCommand(const ArgumentList& arguments, CommandIo io) {
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
        if (!parsed.has_value() || *parsed <= 0.0F) {
            io.err << "error: --dt expects a positive number of seconds\n";
            return 1;
        }
        deltaSeconds = *parsed;
    }

    const bool quiet = arguments.Flag("--quiet");

    std::error_code pathError;
    const std::filesystem::path projectRoot =
        std::filesystem::absolute(std::filesystem::path{ *project }, pathError).lexically_normal();
    if (pathError || !std::filesystem::is_directory(projectRoot, pathError) || pathError) {
        io.err << "error: project directory was not found: " << *project << '\n';
        return 1;
    }

    const std::filesystem::path projectFile = projectRoot / "Project.21kbproject";
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
    std::filesystem::path scenePath;
    if (const std::optional<std::string> sceneOption = arguments.Option("--scene"); sceneOption.has_value()) {
        if (!sceneOption->empty() && sceneOption->front() == '/') {
            const kb::assets::AssetMetadata* metadata =
                FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), *sceneOption);
            if (metadata == nullptr) {
                io.err << "error: scene asset was not found: " << *sceneOption << '\n';
                return 1;
            }
            scenePath = metadata->physicalPath;
        } else {
            scenePath = ResolveInputPath(*sceneOption, projectRoot.string());
        }
    } else {
        io.err << "error: run requires --scene <path>\n";
        return 1;
    }

    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(scene, scenePath)) {
        io.err << "error: could not load scene into runtime: " << scenePath.generic_string() << '\n';
        return 1;
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

    for (int frame = 1; frame <= frames; ++frame) {
        static_cast<void>(scene.Runtime().Update(deltaSeconds));
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

    io.out << "done: " << frames << " frames, " << executedBehaviours << " behaviour executions, "
           << emittedEvents << " events, " << printer.total << " diagnostics\n";
    return printer.total == 0U ? 0 : 1;
}

} // namespace kb::cli
