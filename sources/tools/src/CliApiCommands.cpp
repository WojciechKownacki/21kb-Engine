#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"

#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/library/EngineLibraryManifestComparison.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptValue.hpp"

#include <fstream>
#include <iterator>
#include <string>

namespace kb::cli {
using kb::core::JsonValue;

namespace {

struct CatalogBuildResult {
    bool succeeded = false;
    kb::script::ScriptApiCatalog catalog;
    std::string error;
};

[[nodiscard]] CatalogBuildResult BuildCatalog(const std::optional<std::string>& projectOption) {
    CatalogBuildResult result;
    kb::scene::Scene scene;

    if (projectOption.has_value()) {
        if (!MountProjectAssets(scene, *projectOption, result.error)) {
            return result;
        }
        kb::script::ScriptRuntimeHost host{ scene };
        if (!host.Succeeded()) {
            result.error = "script runtime host reported diagnostics";
            return result;
        }
        result.catalog = kb::script::ScriptApiCatalog::Build(host, scene.Assets().Manager());
    } else {
        kb::script::ScriptRuntimeHost host{ scene };
        if (!host.Succeeded()) {
            result.error = "script runtime host reported diagnostics";
            return result;
        }
        result.catalog = kb::script::ScriptApiCatalog::Build(host);
    }

    result.succeeded = true;
    return result;
}

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, std::string_view content, std::string& error) {
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        error = "could not create directory: " + path.parent_path().string();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        error = "could not open file for writing: " + path.string();
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output.good()) {
        error = "could not write file: " + path.string();
        return false;
    }
    return true;
}

// LIB-024: reconstruct the subset of ScriptApiCatalog that
// kb::library::CompareApiCatalogs actually inspects (lifecycle events,
// functions with their input/output pins, components with their
// properties) from the JSON kb_cli api emits. Everything CompareApiCatalogs
// ignores (luaBindings, projectEntries, the "generator" tag) is skipped —
// reconstructing it would be dead work the comparison never reads. Returns
// false with a populated error only for structurally invalid JSON (not for
// a missing optional array — an older baseline with no "components" key is
// treated as "no components", the honest reading).
[[nodiscard]] bool ParsePins(const JsonValue& pinsArray, std::vector<kb::script::ScriptApiPin>& out, std::string& error) {
    if (pinsArray.GetKind() != JsonValue::Kind::Array) {
        error = "expected a JSON array of pins";
        return false;
    }
    out.clear();
    out.reserve(pinsArray.Size());
    for (std::size_t index = 0; index < pinsArray.Size(); ++index) {
        const JsonValue* pin = pinsArray.At(index);
        if (pin == nullptr || pin->GetKind() != JsonValue::Kind::Object) {
            error = "pin entry is not a JSON object";
            return false;
        }
        const JsonValue* name = pin->Find("name");
        const JsonValue* type = pin->Find("type");
        const JsonValue* required = pin->Find("required");
        if (name == nullptr || type == nullptr || required == nullptr) {
            error = "pin entry is missing name, type, or required";
            return false;
        }
        kb::script::ScriptValueType parsedType{};
        if (!kb::script::TryParse(type->AsString(), parsedType)) {
            error = "pin '" + name->AsString() + "' has an unknown type '" + type->AsString() + "'";
            return false;
        }
        out.push_back(kb::script::ScriptApiPin{ .name = name->AsString(), .type = parsedType, .required = required->AsBool() });
    }
    return true;
}

[[nodiscard]] bool ReconstructCatalogFromJson(std::string_view json, kb::script::ScriptApiCatalog& out, std::string& error) {
    JsonValue root;
    if (!JsonValue::Parse(json, root, error)) {
        return false;
    }
    if (root.GetKind() != JsonValue::Kind::Object) {
        error = "baseline API catalog is not a JSON object";
        return false;
    }

    if (const JsonValue* lifecycle = root.Find("lifecycleEvents"); lifecycle != nullptr && lifecycle->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < lifecycle->Size(); ++index) {
            const JsonValue* event = lifecycle->At(index);
            if (event != nullptr && event->GetKind() == JsonValue::Kind::String) {
                out.lifecycleEvents.push_back(event->AsString());
            }
        }
    }

    if (const JsonValue* functions = root.Find("functions"); functions != nullptr && functions->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < functions->Size(); ++index) {
            const JsonValue* function = functions->At(index);
            if (function == nullptr || function->GetKind() != JsonValue::Kind::Object) {
                error = "function entry is not a JSON object";
                return false;
            }
            const JsonValue* name = function->Find("name");
            const JsonValue* inputs = function->Find("inputs");
            const JsonValue* outputs = function->Find("outputs");
            if (name == nullptr || inputs == nullptr || outputs == nullptr) {
                error = "function entry is missing name, inputs, or outputs";
                return false;
            }
            kb::script::ScriptApiCatalogFunction reconstructed;
            reconstructed.name = name->AsString();
            if (!ParsePins(*inputs, reconstructed.inputs, error) || !ParsePins(*outputs, reconstructed.outputs, error)) {
                return false;
            }
            out.functions.push_back(std::move(reconstructed));
        }
    }

    if (const JsonValue* components = root.Find("components"); components != nullptr && components->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < components->Size(); ++index) {
            const JsonValue* component = components->At(index);
            if (component == nullptr || component->GetKind() != JsonValue::Kind::Object) {
                error = "component entry is not a JSON object";
                return false;
            }
            const JsonValue* name = component->Find("name");
            const JsonValue* properties = component->Find("properties");
            if (name == nullptr) {
                error = "component entry is missing name";
                return false;
            }
            kb::script::ScriptApiCatalogComponent reconstructed;
            reconstructed.name = name->AsString();
            if (properties != nullptr && properties->GetKind() == JsonValue::Kind::Array) {
                for (std::size_t propertyIndex = 0; propertyIndex < properties->Size(); ++propertyIndex) {
                    const JsonValue* property = properties->At(propertyIndex);
                    if (property == nullptr || property->GetKind() != JsonValue::Kind::Object) {
                        error = "property entry is not a JSON object";
                        return false;
                    }
                    const JsonValue* propertyName = property->Find("name");
                    const JsonValue* propertyType = property->Find("type");
                    const JsonValue* writable = property->Find("writable");
                    if (propertyName == nullptr || propertyType == nullptr || writable == nullptr) {
                        error = "property entry is missing name, type, or writable";
                        return false;
                    }
                    kb::script::ScriptValueType parsedType{};
                    if (!kb::script::TryParse(propertyType->AsString(), parsedType)) {
                        error = "property '" + propertyName->AsString() + "' has an unknown type '" + propertyType->AsString() + "'";
                        return false;
                    }
                    reconstructed.properties.push_back(kb::script::ScriptApiCatalogProperty{
                        .name = propertyName->AsString(),
                        .type = parsedType,
                        .writable = writable->AsBool(),
                    });
                }
            }
            out.components.push_back(std::move(reconstructed));
        }
    }

    return true;
}

[[nodiscard]] bool ReadFileText(const std::filesystem::path& path, std::string& content, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "could not open file for reading: " + path.string();
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

int RunApiCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> printFormat = arguments.Option("--print");

    const CatalogBuildResult built = BuildCatalog(project);
    if (!built.succeeded) {
        io.err << "error: " << built.error << '\n';
        return 1;
    }

    if (printFormat.has_value()) {
        if (*printFormat == "markdown" || *printFormat == "md") {
            io.out << kb::script::ScriptApiExport::ToMarkdown(built.catalog);
        } else if (*printFormat == "json") {
            io.out << kb::script::ScriptApiExport::ToJson(built.catalog) << '\n';
        } else if (*printFormat == "lua") {
            io.out << kb::script::ScriptApiExport::ToLuaStubs(built.catalog);
        } else {
            io.err << "error: unknown --print format '" << *printFormat << "' (expected markdown, json, or lua)\n";
            return 1;
        }
        return 0;
    }

    std::filesystem::path outputRoot;
    if (const std::optional<std::string> output = arguments.Option("--out"); output.has_value()) {
        outputRoot = *output;
    } else if (project.has_value()) {
        outputRoot = std::filesystem::path{ *project } / ".kb" / "api";
    } else {
        io.err << "error: pass --project <dir>, --out <dir>, or --print <format>\n";
        return 1;
    }

    const std::pair<const char*, std::string> files[] = {
        { "kb.lua", kb::script::ScriptApiExport::ToLuaStubs(built.catalog) },
        { "script_api.md", kb::script::ScriptApiExport::ToMarkdown(built.catalog) },
        { "script_api.json", kb::script::ScriptApiExport::ToJson(built.catalog) },
        { "manifest.json", kb::library::ToJson(kb::library::BuildApiManifest(built.catalog)) },
    };
    for (const auto& [name, content] : files) {
        const std::filesystem::path path = outputRoot / name;
        std::string error;
        if (!WriteTextFile(path, content, error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        io.out << "wrote " << path.generic_string() << '\n';
    }
    return 0;
}

int RunInitAgentCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: init-agent requires --project <dir>\n";
        return 1;
    }

    CatalogBuildResult built = BuildCatalog(project);
    if (!built.succeeded) {
        io.err << "error: " << built.error << '\n';
        return 1;
    }

    kb::script::ScriptAgentProjectFilesResult written =
        kb::script::ScriptAgentProjectFiles::Write(*project, built.catalog);
    if (!written.succeeded) {
        io.err << "error: " << written.error << '\n';
        return 1;
    }

    // LIB-013: Write() can itself create a new project asset (currently
    // only Assets/Logic/PlayerController.lua, write-once, on a project's
    // FIRST init-agent run) - the catalog just used to write kb.lua/
    // script_api.md/script_api.json above was necessarily built BEFORE
    // that file existed, so it does not yet reflect it as a discoverable
    // project entry. Rebuilding and writing once more (now that the file
    // is on disk) keeps kb.lua/script_api.md/script_api.json/manifest.json
    // consistent with the project's actual final state, instead of
    // silently drifting on the very next, otherwise-unchanged run.
    //
    // Deliberately NOT reassigning `written` to this second call's result:
    // on a brand-new project this second Write() finds AGENTS.md/
    // .luarc.json/PlayerController.lua already on disk (this SAME
    // invocation's first pass just created them) and would report them as
    // "kept (already exists)" - true of the second pass in isolation, but
    // a misleading regression for the user-facing report of THIS
    // invocation as a whole, which really did write them. `written` (first
    // pass) stays the source of truth for what to print; only the second
    // pass's file-writing side effect (fresher kb.lua/script_api.md/
    // script_api.json content, matching the rebuilt catalog) matters here.
    if (written.wroteProjectAsset) {
        built = BuildCatalog(project);
        if (!built.succeeded) {
            io.err << "error: " << built.error << '\n';
            return 1;
        }
        const kb::script::ScriptAgentProjectFilesResult rewritten =
            kb::script::ScriptAgentProjectFiles::Write(*project, built.catalog);
        if (!rewritten.succeeded) {
            io.err << "error: " << rewritten.error << '\n';
            return 1;
        }
    }

    for (const std::filesystem::path& path : written.writtenFiles) {
        io.out << "wrote " << path.generic_string() << '\n';
    }
    for (const std::filesystem::path& path : written.skippedFiles) {
        io.out << "kept " << path.generic_string() << " (already exists)\n";
    }

    // LIB-023: pair the manifest with a hash of the exact catalog it was
    // generated from, so a later build can detect whether the API surface
    // changed without diffing the full JSON/Markdown text.
    const std::filesystem::path manifestPath = std::filesystem::path{ *project } / ".kb" / "api" / "manifest.json";
    std::string manifestError;
    if (!WriteTextFile(manifestPath, kb::library::ToJson(kb::library::BuildApiManifest(built.catalog)), manifestError)) {
        io.err << "error: " << manifestError << '\n';
        return 1;
    }
    io.out << "wrote " << manifestPath.generic_string() << '\n';

    io.out << "project is ready for AI coding agents; see AGENTS.md\n";
    return 0;
}

// LIB-024: compare the API surface the current build registers against a
// committed baseline, classify every difference, and fail (exit 1) on a
// breaking change. This is the CI compatibility gate: the comparison engine
// (kb::library::CompareApiCatalogs) was already complete and tested; what
// was blocked was the baseline-storage decision. Decision made here: the
// baseline is a committed reference JSON in the repo
// (others/api_baseline/script_api.json), produced by the exact same
// project-agnostic path (kb_cli api, no --project) whose live equivalent
// this command rebuilds — so the two are directly comparable. An
// intentional API change is recorded by re-running with --update-baseline,
// which rewrites the baseline file and commits it as part of the same PR
// that changed the API, making the change explicit and reviewable rather
// than silent.
int RunApiCheckCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> baseline = arguments.Option("--baseline");
    if (!baseline.has_value()) {
        io.err << "error: api-check requires --baseline <path>\n";
        return 1;
    }
    const std::optional<std::string> project = arguments.Option("--project");

    const CatalogBuildResult built = BuildCatalog(project);
    if (!built.succeeded) {
        io.err << "error: " << built.error << '\n';
        return 1;
    }

    const std::filesystem::path baselinePath{ *baseline };

    // --update-baseline: intentionally overwrite the baseline with the
    // current surface instead of comparing. Used when an API change is
    // deliberate — the rewritten baseline is committed alongside it.
    if (arguments.Flag("--update-baseline")) {
        std::string writeError;
        if (!WriteTextFile(baselinePath, kb::script::ScriptApiExport::ToJson(built.catalog), writeError)) {
            io.err << "error: " << writeError << '\n';
            return 1;
        }
        io.out << "updated baseline " << baselinePath.generic_string() << '\n';
        return 0;
    }

    std::string baselineJson;
    std::string readError;
    if (!ReadFileText(baselinePath, baselineJson, readError)) {
        io.err << "error: " << readError << '\n';
        io.err << "hint: create it with `kb_cli api-check --baseline " << baselinePath.generic_string() << " --update-baseline`\n";
        return 1;
    }

    kb::script::ScriptApiCatalog baselineCatalog;
    std::string parseError;
    if (!ReconstructCatalogFromJson(baselineJson, baselineCatalog, parseError)) {
        io.err << "error: could not parse baseline API catalog: " << parseError << '\n';
        return 1;
    }

    const kb::library::ApiCompatibilityReport report = kb::library::CompareApiCatalogs(baselineCatalog, built.catalog);
    std::size_t breakingCount = 0;
    for (const kb::library::ApiChange& change : report.changes) {
        const bool breaking = change.severity == kb::library::ApiChangeSeverity::Breaking;
        if (breaking) {
            ++breakingCount;
        }
        std::ostream& stream = breaking ? io.err : io.out;
        stream << (breaking ? "BREAKING: " : "additive: ") << change.description << '\n';
    }

    if (report.HasBreakingChanges()) {
        io.err << "error: API surface has " << breakingCount << " breaking change(s) against the baseline; "
               << "if this is intentional, re-run with --update-baseline and commit the updated baseline\n";
        return 1;
    }

    io.out << "API surface is compatible with the baseline (" << report.changes.size() << " additive change(s))\n";
    return 0;
}

} // namespace kb::cli
