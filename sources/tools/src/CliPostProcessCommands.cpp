#include "CliCommands.hpp"
#include "CliCommon.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"

#include <optional>
#include <string>
#include <vector>

// post-process-new / post-process-set: the scene's image settings (tonemapping, exposure, bloom,
// anti-aliasing) as a profile asset, as a Unity Volume Profile or a Godot Environment resource;
// scene-post-process gives a scene its profile.

namespace kb::cli {
namespace {

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string FieldList() {
    std::string list;
    for (const std::string& name : kb::render::PostProcessProfileAssetLoader::FieldNames()) {
        list += (list.empty() ? "" : ", ") + name;
    }
    return list;
}

// Every "field=value" applied to `settings`; the reason one was refused, or empty.
[[nodiscard]] std::string ApplySets(kb::render::ScenePostProcessSettings& settings, const std::vector<std::string>& specs) {
    for (const std::string& spec : specs) {
        const std::size_t equals = spec.find('=');
        if (equals == std::string::npos || equals == 0U) {
            return "--set takes field=value (e.g. --set tonemapEnabled=0), got '" + spec + "'";
        }
        const std::string_view key = std::string_view{ spec }.substr(0U, equals);
        const std::string_view value = std::string_view{ spec }.substr(equals + 1U);
        if (!kb::render::PostProcessProfileAssetLoader::SetField(settings, key, value)) {
            return "the profile refused '" + spec + "' (unknown field or a value it does not take); fields: " + FieldList();
        }
    }
    return {};
}

[[nodiscard]] std::filesystem::path ProjectRoot(const ArgumentList& arguments, std::string_view command, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: " << command << " requires --project <dir>\n";
        return {};
    }
    std::string error;
    const std::filesystem::path projectFile =
        kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return {};
    }
    return projectFile.parent_path();
}

// --profile as a virtual path (/Game/...) or a path inside the project.
[[nodiscard]] std::optional<std::pair<std::filesystem::path, std::string>> ProfileFile(
    const std::filesystem::path& projectRoot, const std::string& text, CommandIo io) {
    std::filesystem::path relative = kb::assets::Utf8Path(text);
    const std::string generic = Utf8Text(relative);
    if (generic.starts_with("/Game/")) {
        relative = std::filesystem::path{ "Assets" } / kb::assets::Utf8Path(generic.substr(6U));
    }
    const std::filesystem::path file = (projectRoot / relative).lexically_normal();
    const std::filesystem::path inside = file.lexically_relative(projectRoot);
    if (inside.empty() || *inside.begin() != "Assets" ||
        file.extension() != kb::render::kPostProcessProfileAssetExtension || !std::filesystem::is_regular_file(file)) {
        io.err << "error: the project has no post-process profile '" << text << "' (create one with kb_cli post-process-new)\n";
        return std::nullopt;
    }
    return std::make_pair(file, "/Game/" + Utf8Text(std::filesystem::path{ inside }.lexically_relative("Assets")));
}

void PrintProfile(const kb::render::ScenePostProcessSettings& settings, const std::string& virtualPath, CommandIo io) {
    io.out << virtualPath << '\n' << kb::render::PostProcessProfileAssetLoader::Describe(settings);
}

} // namespace

int RunPostProcessNewCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = ProjectRoot(arguments, "post-process-new", io);
    const std::optional<std::string> name = arguments.Option("--name");
    if (projectRoot.empty() || !name.has_value()) {
        if (!projectRoot.empty()) io.err << "error: post-process-new requires --project <dir> --name <Name>\n";
        return 1;
    }
    if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
        io.err << "error: --name '" << *name << "': " << problem << '\n';
        return 1;
    }
    const kb::assets::ProjectAssetFolder folder =
        kb::assets::ResolveProjectAssetFolder(projectRoot, arguments.Option("--folder").value_or("Assets/PostProcess"));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return 1;
    }
    const std::string fileName = *name + kb::render::kPostProcessProfileAssetExtension;
    const std::filesystem::path file = folder.folder / kb::assets::Utf8Path(fileName);
    const std::string virtualPath = folder.virtualPath + "/" + fileName;
    if (std::filesystem::exists(file)) {
        io.err << "error: post-process profile already exists: " << virtualPath << " (change it with kb_cli post-process-set)\n";
        return 1;
    }
    kb::render::ScenePostProcessSettings settings{};
    if (const std::string problem = ApplySets(settings, arguments.Options("--set")); !problem.empty()) {
        io.err << "error: " << problem << '\n';
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(folder.folder, error);
    if (!kb::render::PostProcessProfileAssetLoader::SaveProfile(file, settings)) {
        io.err << "error: the post-process profile could not be written: " << Utf8Text(file) << '\n';
        return 1;
    }
    PrintProfile(settings, virtualPath, io);
    io.out << "Give it to a scene: kb_cli scene-post-process --profile " << virtualPath << '\n';
    return 0;
}

int RunPostProcessSetCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = ProjectRoot(arguments, "post-process-set", io);
    const std::optional<std::string> profileText = arguments.Option("--profile");
    const std::vector<std::string> sets = arguments.Options("--set");
    if (projectRoot.empty() || !profileText.has_value() || sets.empty()) {
        if (!projectRoot.empty()) {
            io.err << "error: post-process-set requires --project <dir> --profile /Game/<path>.kbppfx --set field=value\n";
        }
        return 1;
    }
    const auto located = ProfileFile(projectRoot, *profileText, io);
    if (!located.has_value()) return 1;
    std::optional<kb::render::ScenePostProcessSettings> settings =
        kb::render::PostProcessProfileAssetLoader::LoadProfile(located->first);
    if (!settings.has_value()) {
        io.err << "error: the post-process profile could not be read: " << located->second << '\n';
        return 1;
    }
    if (const std::string problem = ApplySets(*settings, sets); !problem.empty()) {
        io.err << "error: " << problem << '\n';
        return 1;
    }
    if (!kb::render::PostProcessProfileAssetLoader::SaveProfile(located->first, *settings)) {
        io.err << "error: the post-process profile could not be written: " << located->second << '\n';
        return 1;
    }
    PrintProfile(*settings, located->second, io);
    return 0;
}

} // namespace kb::cli
