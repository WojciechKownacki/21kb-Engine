#include "CliCommands.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include <optional>
#include <string>
#include <unordered_set>

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

    kb::scene::Scene scene;
    std::string error;
    std::size_t discovered = 0U;
    if (!MountProjectAssets(scene, *project, error, &discovered)) {
        io.err << "error: " << error << '\n';
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
            scenePath = ResolveInputPath(*sceneOption, *project);
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

    kb::script::ScriptRuntimeSceneSystem scriptSystem{ host.Runtime(), host.AssetPreparer() };

    io.out << "running " << scenePath.filename().generic_string()
           << " for " << frames << " frames (dt " << deltaSeconds << "s, "
           << discovered << " assets discovered)\n";

    DiagnosticPrinter printer{ .scene = scene, .err = io.err, .printed = {}, .total = 0U };
    std::size_t executedBehaviours = 0U;
    std::size_t emittedEvents = 0U;

    const kb::script::ScriptRuntimeExecutionResult& startup = scriptSystem.ExecuteStartup(scene, 0.0F);
    executedBehaviours += startup.executedBehaviours;
    emittedEvents += startup.emittedEvents.size();
    printer.PrintScript(startup.diagnostics, 0U);
    printer.PrintPrepare(scriptSystem.LastPrepareResult().diagnostics, 0U);

    for (int frame = 1; frame <= frames; ++frame) {
        static_cast<void>(scene.Runtime().Update(deltaSeconds));
        const kb::script::ScriptRuntimeExecutionResult& result = scriptSystem.ExecuteFrame(scene, deltaSeconds);
        executedBehaviours += result.executedBehaviours;
        emittedEvents += result.emittedEvents.size();
        if (!quiet) {
            for (const kb::script::ScriptEvent& event : result.emittedEvents) {
                io.out << "[event] frame " << frame << ' ' << event.name
                       << " (sender " << event.sender.Id() << ")\n";
            }
        }
        printer.PrintScript(result.diagnostics, static_cast<std::size_t>(frame));
        printer.PrintPrepare(scriptSystem.LastPrepareResult().diagnostics, static_cast<std::size_t>(frame));
    }

    const kb::script::ScriptRuntimeExecutionResult& shutdown = scriptSystem.ExecuteShutdown(scene, 0.0F);
    executedBehaviours += shutdown.executedBehaviours;
    printer.PrintScript(shutdown.diagnostics, static_cast<std::size_t>(frames) + 1U);

    io.out << "done: " << frames << " frames, " << executedBehaviours << " behaviour executions, "
           << emittedEvents << " events, " << printer.total << " diagnostics\n";
    return printer.total == 0U ? 0 : 1;
}

} // namespace kb::cli
