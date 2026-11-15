#include "CliCommands.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"
#include "engine/script/ScriptLifecycle.hpp"
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

struct UnknownCall {
    std::size_t line = 0;
    std::string call;
    std::string closest;
};

// Table.Name( where Table is an engine API table without Name: at run time that is a call to nil.
// Comments and string literals are skipped; tables the engine does not define (a script's own
// modules, a plugin not loaded here) are not judged.
[[nodiscard]] std::vector<UnknownCall> FindUnknownApiCalls(std::string_view source, const kb::script::ScriptApiCatalog& catalog) {
    std::map<std::string, std::set<std::string>, std::less<>> tables;
    for (const kb::script::ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        if (!binding.tableName.empty()) tables[binding.tableName].insert(binding.luaName);
    }
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    std::vector<UnknownCall> found;
    std::size_t line = 1;
    for (std::size_t at = 0; at < source.size();) {
        const char c = source[at];
        if (c == '\n') { ++line; ++at; continue; }
        if (source.compare(at, 2, "--") == 0) {
            const bool block = source.compare(at + 2U, 2, "[[") == 0;
            const std::size_t end = block ? source.find("]]", at) : source.find('\n', at);
            if (end == std::string_view::npos) break;
            line += static_cast<std::size_t>(std::count(source.begin() + static_cast<std::ptrdiff_t>(at), source.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
            at = block ? end + 2U : end;
            continue;
        }
        if (c == '"' || c == '\'') {
            for (++at; at < source.size() && source[at] != c && source[at] != '\n'; ++at) if (source[at] == '\\') ++at;
            ++at;
            continue;
        }
        if (!word(c) || (at > 0U && (word(source[at - 1U]) || source[at - 1U] == '.' || source[at - 1U] == ':'))) { ++at; continue; }
        std::size_t end = at;
        while (end < source.size() && word(source[end])) ++end;
        const std::string_view table = source.substr(at, end - at);
        const auto known = tables.find(table);
        if (known == tables.end() || end >= source.size() || source[end] != '.') { at = end; continue; }
        std::size_t nameEnd = end + 1U;
        while (nameEnd < source.size() && word(source[nameEnd])) ++nameEnd;
        std::size_t paren = nameEnd;
        while (paren < source.size() && (source[paren] == ' ' || source[paren] == '\t')) ++paren;
        const std::string name{ source.substr(end + 1U, nameEnd - end - 1U) };
        if (paren < source.size() && source[paren] == '(' && !name.empty() && !known->second.contains(name)) {
            std::string closest;
            std::size_t best = 3;
            for (const std::string& candidate : known->second) {
                // Same letters ignoring case, or a near spelling, is worth naming.
                std::string a = candidate, b = name;
                std::ranges::transform(a, a.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
                std::ranges::transform(b, b.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
                const std::size_t distance = a == b ? 0U : (a.starts_with(b) || b.starts_with(a) ? 2U : 3U);
                if (distance < best) { best = distance; closest = candidate; }
            }
            found.push_back(UnknownCall{ line, std::string{ table } + '.' + name, closest.empty() ? closest : std::string{ table } + '.' + closest });
        }
        at = nameEnd;
    }
    return found;
}

// The UI of one scene (`sceneFile`) or of every scene of the project (empty): warnings, which do not fail
// the command, and scenes that could not be read, which do. Returns the number of unreadable scenes.
[[nodiscard]] int ValidateSceneUi(
    kb::scene::Scene& scene, bool& mounted, const std::filesystem::path& projectRoot,
    const std::filesystem::path& sceneFile, std::string_view label, CommandIo io) {
    std::string error;
    if (!mounted && !(mounted = MountProjectAssets(scene, projectRoot, error))) {
        io.err << "FAIL " << label << ": " << error << '\n';
        return 1;
    }
    const kb::script::UIProjectReport report =
        kb::script::ValidateUIProject(scene.Assets().Manager().Registry().All(), sceneFile);
    for (const std::string& unreadable : report.errors) io.err << "FAIL " << unreadable << '\n';
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
    const std::filesystem::path projectRoot = arguments.Option("--project").value_or("");
    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::scene::Scene scene;
    bool projectMounted = false;
    if (arguments.Positionals().empty()) {
        if (projectRoot.empty()) {
            io.err << "error: validate expects one or more .lua or .21kbscene files, or --project <dir> for every scene\n";
            return 1;
        }
        return ValidateSceneUi(scene, projectMounted, projectRoot, {}, "project scenes", io) == 0 ? 0 : 1;
    }
    const kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);
    std::uint64_t nextAssetId = 1U;
    int failures = 0;

    for (const std::string& positional : arguments.Positionals()) {
        const std::filesystem::path path = ResolveInputPath(positional, projectRoot);
        if (path.extension() == ".21kbscene") {
            failures += ValidateSceneUi(scene, projectMounted, projectRoot, path, positional, io);
            continue;
        }
        std::string source;
        std::string error;
        if (!ReadTextFile(path, source, error)) {
            io.err << "FAIL " << positional << ": " << error << '\n';
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
            for (const kb::script::ScriptSubsetFinding& finding : kb::script::FindOutOfSubsetConstructs(source)) {
                io.err << "WARN " << positional << ':' << finding.line << ": " << finding.message << '\n';
            }
            ++failures;
            continue;
        }
        if (const std::vector<UnknownCall> unknown = FindUnknownApiCalls(source, catalog); !unknown.empty()) {
            for (const UnknownCall& call : unknown) {
                io.err << "FAIL " << positional << ':' << call.line << ": " << call.call << " does not exist";
                if (!call.closest.empty()) io.err << "; did you mean " << call.closest << '?';
                io.err << " (kb_cli api --topic " << call.call.substr(0, call.call.find('.')) << " lists the functions)\n";
            }
            ++failures;
            continue;
        }
        // Loads fine, but a callback named for another lifecycle is never called: say so instead of a bare OK.
        const std::vector<kb::script::ForeignLifecycleName> foreign = kb::script::FindForeignLifecycleNames(source);
        // The script runs on the two script VMs; what only one of them has would fail on a platform that uses the other.
        const std::vector<kb::script::ScriptSubsetFinding> outsideSubset = kb::script::FindOutOfSubsetConstructs(source);
        const std::size_t warnings = foreign.size() + outsideSubset.size();
        io.out << "OK   " << positional;
        if (warnings != 0U) {
            io.out << " (" << warnings << (warnings == 1U ? " warning)" : " warnings)");
        }
        io.out << '\n';
        for (const kb::script::ScriptSubsetFinding& finding : outsideSubset) {
            io.out << "WARN " << positional << ':' << finding.line << ": " << finding.message << '\n';
        }
        for (const kb::script::ForeignLifecycleName& name : foreign) {
            const char* hook = kb::script::ToString(name.hook);
            io.out << "WARN " << positional << ':' << name.line << ": " << name.name
                   << "() is never called - 21kb calls " << hook << "(self, dt). Rename the function to " << hook
                   << ".\n";
        }
    }

    return failures == 0 ? 0 : 1;
}

} // namespace kb::cli
