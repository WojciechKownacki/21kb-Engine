#include "CliCommands.hpp"

#include "PackagedRuntimeModuleContract.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/platform/FileSystemPath.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/script/NativeScriptBuildPipeline.hpp"

#include <charconv>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace kb::cli {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kPackName = "Game.kbpack";
constexpr std::string_view kWindowsTarget = "Windows.x64";
constexpr std::string_view kProfileSuffix = ".frames.csv";

[[nodiscard]] std::string Utf8Text(const fs::path& path) {
    const auto bytes = path.u8string();
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

[[nodiscard]] fs::path AbsolutePath(const std::string& text) {
    fs::path path = fs::absolute(kb::assets::Utf8Path(text)).lexically_normal();
    return path.has_filename() ? path : path.parent_path();
}

// One word of a shell command, quoted so a path with spaces reaches the tool as written. cmd.exe
// expands %NAME% even inside quotes and cannot escape a quote inside them, so those are refused.
[[nodiscard]] std::optional<std::string> ShellWord(const std::string& text) {
#if defined(_WIN32)
    if (text.find_first_of("\"%") != std::string::npos) {
        return std::nullopt;
    }
    return '"' + text + '"';
#else
    std::string quoted = "'";
    for (const char character : text) {
        quoted += character == '\'' ? std::string{ "'\\''" } : std::string{ character };
    }
    return quoted + '\'';
#endif
}

// An engine tool installed beside kb_cli (a build tree or an installation keeps them together).
[[nodiscard]] fs::path EngineTool(std::string_view name, CommandIo io) {
    const fs::path self = kb::platform::RunningExecutablePath();
    const fs::path tool = self.parent_path() / (std::string{ name } + self.extension().string());
    std::error_code error;
    if (self.empty() || !fs::is_regular_file(tool, error)) {
        io.err << "error: " << name << " was not found beside kb_cli (" << Utf8Text(tool) << "); build it first\n";
        return {};
    }
    return tool;
}

// Runs the tool and waits for it. Its output goes to this process's stderr, as a native script
// build's does, so a JSON or MCP caller's stdout stays clean.
[[nodiscard]] bool RunEngineTool(const fs::path& tool, const std::vector<std::string>& words, CommandIo io) {
    std::string command;
    for (const std::string& word : words) {
        const std::optional<std::string> quoted = ShellWord(word);
        if (!quoted.has_value()) {
            io.err << "error: an argument contains a character the shell cannot pass safely: " << word << '\n';
            return false;
        }
        command += (command.empty() ? "" : " ") + *quoted;
    }
    const kb::script::NativeScriptBuildResult result =
        kb::script::NativeScriptBuildPipeline::Build({ .enabled = true, .command = command });
    if (result.exitCode != 0) {
        io.err << "error: " << Utf8Text(tool.filename()) << " failed with exit code " << result.exitCode << '\n';
    } else if (!result.errors.empty()) {
        io.err << "error: " << result.errors.front() << '\n';
    }
    return result.Succeeded();
}

// The libraries the packaged player loads from its own folder: the engine modules the project
// enables, named by the contract the cooker validates against, and the audio runtime the audio
// module loads beside itself. The project's own libraries are staged by the cooker.
[[nodiscard]] bool FindPlayerLibraries(
    const kb::project::ProjectDescriptor& descriptor, std::vector<fs::path>& libraries, std::string& error) {
    for (const kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        const kb::game::PackagedRuntimeModuleDesc* const module =
            plugin.enabled ? kb::game::FindPackagedRuntimeModule(plugin.name) : nullptr;
        if (module == nullptr) {
            continue;
        }
        const fs::path library = kb::modules::EngineModuleLoader::ResolveModulePath(module->windowsBinaryName);
        std::error_code status;
        if (!fs::is_regular_file(library, status)) {
            error = std::string{ module->windowsBinaryName } + " of module " + plugin.name +
                " was not found beside kb_cli or in its build tree";
            return false;
        }
        libraries.push_back(library);
        if (module->kind == kb::game::PackagedRuntimeModuleKind::AudioMiniaudio) {
            const fs::path runtime = library.parent_path() / "phonon.dll";
            if (!fs::is_regular_file(runtime, status)) {
                error = "phonon.dll of module " + plugin.name + " was not found beside " + Utf8Text(library);
                return false;
            }
            libraries.push_back(runtime);
        }
    }
    return true;
}

[[nodiscard]] bool ParseCount(const std::string& text, unsigned minimum, unsigned maximum, unsigned& value) {
    const auto [end, parseError] = std::from_chars(text.data(), text.data() + text.size(), value);
    return parseError == std::errc{} && end == text.data() + text.size() && value >= minimum && value <= maximum;
}

} // namespace

int RunBuildCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> out = arguments.Option("--out");
    if (!project.has_value() || !out.has_value()) {
        io.err << "error: build needs --project <dir> and --out <dir>\n";
        return 1;
    }
    std::string error;
    const fs::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: the project could not be loaded: " << loaded.error << '\n';
        return 1;
    }
    unsigned level = 0U;
    if (const std::optional<std::string> text = arguments.Option("--pack-compression-level");
        text.has_value() && !ParseCount(*text, 0U, 19U, level)) {
        io.err << "error: --pack-compression-level expects 0-19\n";
        return 1;
    }
    const fs::path outputDirectory = AbsolutePath(*out);
    std::error_code status;
    if (fs::exists(outputDirectory, status) &&
        (!fs::is_directory(outputDirectory, status) || !fs::is_empty(outputDirectory, status))) {
        io.err << "error: " << Utf8Text(outputDirectory) << " exists and is not empty; build into a new folder\n";
        return 1;
    }
    std::vector<fs::path> libraries;
    if (!FindPlayerLibraries(loaded.descriptor, libraries, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const fs::path cooker = EngineTool("kb_cooker", io);
    const fs::path player = EngineTool("kb_game", io);
    if (cooker.empty() || player.empty()) {
        return 1;
    }

    // The cooker is the one place that cooks; the staging folder for the project's own libraries
    // must not exist yet, and the cooker creates it only when the project has some.
    std::vector<std::string> words{ Utf8Text(cooker), "--project", Utf8Text(projectFile), "--target",
        std::string{ kWindowsTarget }, "--output", Utf8Text(outputDirectory / kPackName), "--runtime-modules-output",
        Utf8Text(outputDirectory / "RuntimeModules") };
    for (const char* option : { "--cache", "--engine-root", "--shaderc" }) {
        if (const std::optional<std::string> value = arguments.Option(option); value.has_value()) {
            words.insert(words.end(), { option, Utf8Text(AbsolutePath(*value)) });
        }
    }
    if (arguments.Option("--pack-compression-level").has_value()) {
        words.insert(words.end(), { "--pack-compression-level", std::to_string(level) });
    }
    fs::create_directories(outputDirectory, status);
    if (status || !RunEngineTool(cooker, words, io)) {
        io.err << "error: the cook did not finish; " << Utf8Text(outputDirectory) << " was left for inspection\n";
        return 1;
    }
    libraries.push_back(player);
    for (const fs::path& library : libraries) {
        fs::copy_file(library, outputDirectory / library.filename(), fs::copy_options::none, status);
        if (status) {
            io.err << "error: " << Utf8Text(library) << " could not be copied: " << status.message() << '\n';
            return 1;
        }
    }
    io.out << "built " << Utf8Text(outputDirectory) << ": " << kPackName << ", " << Utf8Text(player.filename()) << " and "
           << libraries.size() - 1U << " libraries\n";
    return 0;
}

int RunCaptureCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> build = arguments.Option("--build");
    const std::optional<std::string> out = arguments.Option("--out");
    if (project.has_value() == build.has_value() || !out.has_value()) {
        io.err << "error: capture needs --out <file.png> and either --project <dir> or --build <dir>, not both\n";
        return 1;
    }
    unsigned frames = 60U;
    unsigned width = 0U;
    unsigned height = 0U;
    const std::optional<std::string> frameText = arguments.Option("--frames");
    const std::optional<std::string> widthText = arguments.Option("--width");
    const std::optional<std::string> heightText = arguments.Option("--height");
    if ((frameText.has_value() && !ParseCount(*frameText, 1U, 120000U, frames)) ||
        (widthText.has_value() && !ParseCount(*widthText, 64U, 8192U, width)) ||
        (heightText.has_value() && !ParseCount(*heightText, 64U, 8192U, height))) {
        io.err << "error: --frames expects 1-120000, --width and --height 64-8192\n";
        return 1;
    }
    // A built player refuses development options, so the engine's own player renders the package.
    std::string target = project.has_value() ? Utf8Text(AbsolutePath(*project)) : std::string{};
    std::error_code status;
    if (build.has_value()) {
        const fs::path pack = AbsolutePath(*build) / kPackName;
        if (!fs::is_regular_file(pack, status)) {
            io.err << "error: " << Utf8Text(pack) << " does not exist; run kb_cli build first\n";
            return 1;
        }
        target = Utf8Text(pack);
    }
    const fs::path picture = AbsolutePath(*out);
    // The player records frame statistics to run on its fixed 1/60 s step, which makes the same
    // frame count give the same picture; they are not the product here and are removed again.
    const fs::path statistics = fs::path{ picture }.concat(kProfileSuffix);
    if (fs::exists(picture, status) || fs::exists(statistics, status)) {
        io.err << "error: " << Utf8Text(picture) << " already exists; capture writes new files only\n";
        return 1;
    }
    const fs::path player = EngineTool("kb_game", io);
    if (player.empty()) {
        return 1;
    }
    fs::create_directories(picture.parent_path(), status);
    const std::string count = std::to_string(frames);
    std::vector<std::string> words{ Utf8Text(player), "--project=" + target, "--headless", "--frames=" + count,
        "--screenshot-file=" + Utf8Text(picture), "--screenshot-frame=" + count, "--profile-fixed-step",
        "--profile-file=" + Utf8Text(statistics) };
    if (const std::optional<std::string> scene = arguments.Option("--scene"); scene.has_value()) {
        words.push_back("--scene=" + *scene);
    }
    if (width != 0U) {
        words.push_back("--width=" + std::to_string(width));
    }
    if (height != 0U) {
        words.push_back("--height=" + std::to_string(height));
    }
    const bool rendered = RunEngineTool(player, words, io);
    fs::remove(statistics, status);
    if (!rendered) {
        return 1;
    }
    io.out << "captured frame " << frames << " to " << Utf8Text(picture) << '\n';
    return 0;
}

} // namespace kb::cli
