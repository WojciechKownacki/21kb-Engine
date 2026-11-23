#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"
#include "engine/script/ScriptLuaCheck.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/UISceneValidation.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace kb::cli {

namespace {

[[nodiscard]] bool ReadTextFile(const std::filesystem::path& path, std::string& content, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "could not open file: " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

// One checked file for the data field: whether it passed, and the warning and error lines the text prints for it.
void AddValidated(kb::core::JsonValue& items, std::string_view target, const std::vector<std::string>& warnings, const std::vector<std::string>& errors) {
    using kb::core::JsonValue;
    const auto lines = [](const std::vector<std::string>& text) {
        auto array = JsonValue::MakeArray();
        for (const std::string& line : text) array.Append(JsonValue::MakeString(ToValidUtf8(line)));
        return array;
    };
    auto item = JsonValue::MakeObject();
    item.Set("target", JsonValue::MakeString(ToValidUtf8(target)));
    item.Set("ok", JsonValue::MakeBool(errors.empty()));
    item.Set("warnings", lines(warnings));
    item.Set("errors", lines(errors));
    items.Append(std::move(item));
}

// The UI of one scene (`sceneFile`) or of every scene of the project (empty): warnings, which do not fail
// the command, and scenes that could not be read, which do. Returns the number of unreadable scenes.
[[nodiscard]] int ValidateSceneUi(
    kb::scene::Scene& scene, bool& mounted, const std::filesystem::path& projectRoot,
    const std::filesystem::path& sceneFile, std::string_view label, CommandIo io, kb::core::JsonValue& items) {
    std::string error;
    if (!mounted && !(mounted = MountProjectAssets(scene, projectRoot, error))) {
        io.err << "FAIL " << label << ": " << error << '\n';
        AddValidated(items, label, {}, { std::string{ label } + ": " + error });
        return 1;
    }
    const kb::script::UIProjectReport report =
        kb::script::ValidateUIProject(scene.Assets().Manager().Registry().All(), sceneFile);
    for (const std::string& unreadable : report.errors) io.err << "FAIL " << unreadable << '\n';
    std::vector<std::string> warningLines;
    for (const kb::script::UIProjectWarning& warning : report.warnings) warningLines.push_back(kb::script::FormatUIWarning(warning.scene, warning.warning));
    AddValidated(items, label, warningLines, report.errors);
    io.out << "OK   " << label;
    if (!report.warnings.empty()) {
        io.out << " (" << report.warnings.size() << (report.warnings.size() == 1U ? " warning)" : " warnings)");
    }
    io.out << '\n';
    for (const kb::script::UIProjectWarning& warning : report.warnings) {
        io.out << kb::script::FormatUIWarning(warning.scene, warning.warning) << '\n';
    }
    return static_cast<int>(report.errors.size());
}

} // namespace

int RunValidateCommand(const ArgumentList& arguments, CommandIo io) {
    kb::core::JsonValue items = kb::core::JsonValue::MakeArray();
    const auto finish = [&](int failures) {
        kb::core::JsonValue data = DataObject("21kb.cli.validate/v1");
        data.Set("ok", kb::core::JsonValue::MakeBool(failures == 0));
        data.Set("results", std::move(items));
        PublishData(io, std::move(data));
        return failures == 0 ? 0 : 1;
    };
    const std::filesystem::path projectRoot = arguments.Option("--project").value_or("");
    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::scene::Scene scene;
    bool projectMounted = false;
    if (arguments.Positionals().empty()) {
        if (projectRoot.empty()) {
            io.err << "error: validate expects one or more .lua or .21kbscene files, or --project <dir> for every scene\n";
            return 1;
        }
        return finish(ValidateSceneUi(scene, projectMounted, projectRoot, {}, "project scenes", io, items));
    }
    const kb::script::ScriptRuntimeHost host{ scene };
    // With a project, the events its scripts emit are known too: a handler for one is not a misspelled hook.
    std::string mountError;
    if (!projectRoot.empty()) projectMounted = MountProjectAssets(scene, projectRoot, mountError);
    kb::script::ScriptApiCatalog catalog = projectMounted ? kb::script::ScriptApiCatalog::Build(host, scene.Assets().Manager())
                                                          : kb::script::ScriptApiCatalog::Build(host);
    if (!projectRoot.empty()) AddProjectLuaGlobals(catalog, projectRoot);
    std::uint64_t nextAssetId = 1U;
    int failures = 0;

    for (const std::string& positional : arguments.Positionals()) {
        // A /Game/ path (a script or a scene of the project) is looked for in the project's content, as in every command.
        std::string pathError;
        const std::filesystem::path path = positional.starts_with("/Game/")
            ? ResolveProjectArgument(positional, arguments.Option("--project"), pathError)
            : ResolveInputPath(positional, projectRoot);
        if (path.empty()) {
            io.err << "FAIL " << positional << ": " << pathError << '\n';
            AddValidated(items, positional, {}, { pathError });
            ++failures;
            continue;
        }
        if (path.extension() == ".21kbscene") {
            failures += ValidateSceneUi(scene, projectMounted, projectRoot, path, positional, io, items);
            continue;
        }
        std::string source;
        std::string error;
        if (!ReadTextFile(path, source, error)) {
            io.err << "FAIL " << positional << ": " << error << '\n';
            AddValidated(items, positional, {}, { error });
            ++failures;
            continue;
        }

        const kb::script::PucLuaLoadResult loaded = luaRuntime.LoadScript(
            kb::assets::AssetId{ nextAssetId++ },
            source,
            path.filename().string());
        if (!loaded.succeeded) {
            io.err << "FAIL " << positional << ": " << loaded.error << '\n';
            // A construct only one of the two script VMs has is often the reason the other one refuses the script.
            std::vector<std::string> subset;
            for (const kb::script::ScriptSubsetFinding& finding : kb::script::FindOutOfSubsetConstructs(source)) {
                io.err << "WARN " << positional << ':' << finding.line << ": " << finding.message << '\n';
                subset.push_back(positional + ':' + std::to_string(finding.line) + ": " + finding.message);
            }
            AddValidated(items, positional, subset, { loaded.error });
            ++failures;
            continue;
        }
        // The same checker the language server uses: names that do not exist, wrong arguments, hooks that are never
        // called, callbacks named for another engine and constructs outside the Lua both script VMs share.
        std::vector<std::string> errorLines;
        std::vector<std::string> warningLines;
        for (const kb::script::ScriptLuaFinding& finding : kb::script::CheckLuaScript(source, catalog)) {
            const std::string line = positional + ':' + std::to_string(finding.line) + ": " + finding.message;
            (finding.severity == kb::script::ScriptLuaFindingSeverity::Error ? errorLines : warningLines).push_back(line);
        }
        if (errorLines.empty()) {
            io.out << "OK   " << positional;
            if (!warningLines.empty()) {
                io.out << " (" << warningLines.size() << (warningLines.size() == 1U ? " warning)" : " warnings)");
            }
            io.out << '\n';
        } else {
            ++failures;
        }
        for (const std::string& line : errorLines) io.err << "FAIL " << line << '\n';
        for (const std::string& line : warningLines) io.out << "WARN " << line << '\n';
        AddValidated(items, positional, warningLines, errorLines);
    }

    return finish(failures);
}

} // namespace kb::cli
