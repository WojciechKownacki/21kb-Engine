#include "CliCommands.hpp"

#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <fstream>

namespace kb::cli {

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

    const CatalogBuildResult built = BuildCatalog(project);
    if (!built.succeeded) {
        io.err << "error: " << built.error << '\n';
        return 1;
    }

    const kb::script::ScriptAgentProjectFilesResult written =
        kb::script::ScriptAgentProjectFiles::Write(*project, built.catalog);
    if (!written.succeeded) {
        io.err << "error: " << written.error << '\n';
        return 1;
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

} // namespace kb::cli
