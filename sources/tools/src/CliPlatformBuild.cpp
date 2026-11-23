#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/packaging/PackagingTargetCatalog.hpp"
#include "engine/platform/FileSystemPath.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "packaging/EditorPackageRequestBuilder.hpp"
#include "packaging/EditorProjectPackageService.hpp"
#include "rendering/BuildGamePanelModel.hpp"
#include "settings/EditorBuildGameSettingsStore.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// build --platform android|web|linux: the editor's Build Game pipeline without the editor. The request is made by the code the
// panel uses (BuildPackageRequest, the saved build settings, BuildGamePanelModel::Validate) and run by the same package service,
// so the package is the same one. What this machine lacks is said before the job starts, with what to install.

namespace kb::cli {

#if defined(_WIN32)
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kPlatformUsage =
    "usage: kb_cli build --project <dir> --platform android|web|linux [--out <folder for packages>] [--profile development|release]\n"
    "       [--texture astc|etc2 (android)] [--renderer webgl|webgpu (web)] [--build-root <engine build folder>] [--engine-root <engine folder>]\n"
    "       [--python <executable>] [--emsdk <folder> (web)] [--android-format apk|aab] [--android-keystore <file> --android-key-alias <alias>]\n"
    "       [--linux-host <host> --linux-user <user> --linux-host-key <key> --linux-identity <file> --linux-port <n>] [--verbose]\n";

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower{ text };
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

[[nodiscard]] std::optional<std::string> Environment(const char* name) {
    std::string value(32768U, '\0');
    const DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0U || length >= value.size()) return std::nullopt;
    value.resize(length);
    return value;
}

[[nodiscard]] bool IsFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

[[nodiscard]] bool IsFolder(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(path, error);
}

// The folder of the engine that holds scripts/package_game.py: an installed engine (prebuilt-engine.json) or the source tree
// the running kb_cli was built from.
struct EngineFolders {
    fs::path root;
    fs::path build;
};

[[nodiscard]] EngineFolders FindEngine() {
    for (fs::path folder = kb::platform::RunningExecutablePath().parent_path(); !folder.empty(); folder = folder.parent_path()) {
        if (IsFile(folder / "prebuilt-engine.json")) return { folder, folder };
        if (IsFile(folder / "scripts" / "package_game.py") && IsFile(folder / "CMakeLists.txt")) {
            // The build folder is the one this kb_cli sits in (its bin folder's parent).
            fs::path build = kb::platform::RunningExecutablePath().parent_path();
            if (build.filename() == "bin" && IsFile(build.parent_path() / "CMakeCache.txt")) build = build.parent_path();
            return { folder, build };
        }
        if (folder == folder.parent_path()) break;
    }
    return {};
}

// A program by name on PATH, or a path that exists.
[[nodiscard]] bool ProgramExists(const fs::path& program) {
    if (program.has_parent_path()) return IsFile(program) || IsFile(fs::path{ program }.concat(".exe"));
    const std::optional<std::string> path = Environment("PATH");
    if (!path.has_value()) return false;
    std::size_t start = 0U;
    while (start <= path->size()) {
        const std::size_t end = path->find(';', start);
        const fs::path folder{ path->substr(start, end == std::string::npos ? std::string::npos : end - start) };
        if (!folder.empty() && (IsFile(folder / program) || IsFile(folder / fs::path{ program }.concat(".exe")))) return true;
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return false;
}

// The package script builds the engine's host tools (cooker, pack validator) when the engine is a source tree. That needs the C++
// compiler's environment, which a plain terminal does not have: it is taken from the Visual Studio Build Tools' vcvars64.bat and
// set for this process (and so for the job). Returns what is wrong, or empty.
[[nodiscard]] std::string PrepareCompilerEnvironment() {
    if (ProgramExists("cl")) return {};
    std::vector<fs::path> candidates;
    if (const std::optional<std::string> tools = Environment("KB_BUILD_TOOLS"); tools.has_value()) candidates.emplace_back(fs::path{ *tools } / "VC" / "Auxiliary" / "Build" / "vcvars64.bat");
    candidates.emplace_back("C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat");
    if (const std::optional<std::string> programFiles = Environment("ProgramFiles(x86)"); programFiles.has_value()) {
        const fs::path vswhere = fs::path{ *programFiles } / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (IsFile(vswhere)) {
            const std::string command = "\"\"" + vswhere.string() + "\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath\"";
            if (FILE* pipe = _popen(command.c_str(), "r")) {
                char line[1024]{};
                if (std::fgets(line, sizeof(line), pipe) != nullptr) {
                    std::string root{ line };
                    while (!root.empty() && (root.back() == '\n' || root.back() == '\r')) root.pop_back();
                    if (!root.empty()) candidates.emplace_back(fs::path{ root } / "VC" / "Auxiliary" / "Build" / "vcvars64.bat");
                }
                _pclose(pipe);
            }
        }
    }
    for (const fs::path& vcvars : candidates) {
        if (!IsFile(vcvars)) continue;
        const std::string command = "\"\"" + vcvars.string() + "\" >nul 2>&1 && set\"";
        FILE* pipe = _popen(command.c_str(), "r");
        if (pipe == nullptr) continue;
        std::string text;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) text += buffer;
        _pclose(pipe);
        std::size_t start = 0U;
        while (start < text.size()) {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string entry = text.substr(start, end - start);
            start = end + 1U;
            while (!entry.empty() && entry.back() == '\r') entry.pop_back();
            const std::size_t equals = entry.find('=');
            if (equals == std::string::npos || equals == 0U) continue;
            SetEnvironmentVariableA(entry.substr(0U, equals).c_str(), entry.substr(equals + 1U).c_str());
        }
        if (ProgramExists("cl")) return {};
    }
    return "  - no C++ compiler was found: building the engine's tools from this source tree needs the Visual Studio Build Tools with the C++ workload (set KB_BUILD_TOOLS to their folder if they are not in C:\\BuildTools)\n";
}

// The JDK major version from the `release` file every JDK has (JAVA_VERSION="21.0.13"); 0 when it cannot be read.
[[nodiscard]] int JdkMajorVersion(const fs::path& home) {
    std::ifstream release{ home / "release" };
    for (std::string line; std::getline(release, line);) {
        const std::size_t quote = line.find('"');
        if (!line.starts_with("JAVA_VERSION=") || quote == std::string::npos) continue;
        int major = std::atoi(line.c_str() + quote + 1U);
        if (major == 1) { // "1.8.0_452" is version 8
            const std::size_t dot = line.find('.', quote);
            major = dot == std::string::npos ? 1 : std::atoi(line.c_str() + dot + 1U);
        }
        return major;
    }
    return 0;
}

[[nodiscard]] bool IsUsableJdk(const fs::path& home) {
    if (!IsFile(home / "bin" / "java.exe")) return false;
    const int major = JdkMajorVersion(home);
    return major == 0 || major >= 17;
}

// The Android build runs Gradle on a JDK 17 or newer. JAVA_HOME is used when it works; a customer's JAVA_HOME is often stale (a
// removed install) or names a JDK 8, so the places a JDK is installed are searched: Android Studio's own, and the folders the
// JDK installers and IDEs use. 21 and 17 are the versions Gradle and the Android plugin are tested with, so they win over a newer one.
// The folder found is set as JAVA_HOME for this process and the job it starts. Returns the folder, or empty when there is none.
[[nodiscard]] fs::path PrepareJdk() {
    if (const std::optional<std::string> javaHome = Environment("JAVA_HOME"); javaHome.has_value() && IsUsableJdk(fs::path{ *javaHome })) return fs::path{ *javaHome };
    std::vector<fs::path> candidates;
    const auto addChildren = [&](const std::optional<std::string>& base, const fs::path& folder) {
        if (!base.has_value()) return;
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(fs::path{ *base } / folder, error)) candidates.push_back(entry.path());
    };
    const std::optional<std::string> programFiles = Environment("ProgramFiles");
    const std::optional<std::string> local = Environment("LOCALAPPDATA");
    if (programFiles.has_value()) candidates.push_back(fs::path{ *programFiles } / "Android" / "Android Studio" / "jbr");
    if (local.has_value()) candidates.push_back(fs::path{ *local } / "Programs" / "Android Studio" / "jbr");
    addChildren(Environment("USERPROFILE"), ".jdks");
    for (const char* folder : { "Java", "Eclipse Adoptium", "Microsoft", "Zulu", "Amazon Corretto", "BellSoft", "Semeru" }) addChildren(programFiles, folder);
    if (const std::optional<std::string> path = Environment("PATH"); path.has_value()) { // a java.exe on PATH names its JDK
        std::size_t start = 0U;
        while (start <= path->size()) {
            const std::size_t end = path->find(';', start);
            const fs::path folder{ path->substr(start, end == std::string::npos ? std::string::npos : end - start) };
            if (!folder.empty() && folder.filename() == "bin") candidates.push_back(folder.parent_path());
            if (end == std::string::npos) break;
            start = end + 1U;
        }
    }
    fs::path best;
    int bestRank = 100;
    int bestMajor = 0;
    for (const fs::path& candidate : candidates) {
        if (!IsUsableJdk(candidate)) continue;
        const int major = JdkMajorVersion(candidate);
        const int rank = major == 21 ? 0 : major == 17 ? 1 : major == 0 ? 3 : major < 26 ? 2 : 4;
        if (rank < bestRank || (rank == bestRank && major > bestMajor)) {
            best = candidate;
            bestRank = rank;
            bestMajor = major;
        }
    }
    if (!best.empty()) SetEnvironmentVariableA("JAVA_HOME", best.string().c_str());
    return best;
}

// CMake runs the engine's own build for the host tools and the player. The one that configured the engine build folder is used
// (CMakeCache.txt names it), else the one that comes with the Android SDK, the Visual Studio C++ tools or CMake's installer. Its
// folder, and the Ninja beside it, are put on PATH for this process and the job. Returns the cmake found, or empty.
[[nodiscard]] fs::path PrepareCMake(const fs::path& buildRoot) {
    if (ProgramExists("cmake")) return "cmake";
    std::vector<fs::path> candidates;
    {
        std::ifstream cache{ buildRoot / "CMakeCache.txt" };
        for (std::string line; std::getline(cache, line);) {
            if (line.starts_with("CMAKE_COMMAND:INTERNAL=")) candidates.emplace_back(line.substr(line.find('=') + 1U));
        }
    }
    const auto addVersions = [&](const fs::path& folder) {
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) candidates.push_back(entry.path() / "bin" / "cmake.exe");
    };
    for (const char* name : { "ANDROID_SDK_ROOT", "ANDROID_HOME" }) {
        if (const std::optional<std::string> sdk = Environment(name); sdk.has_value()) addVersions(fs::path{ *sdk } / "cmake");
    }
    if (const std::optional<std::string> local = Environment("LOCALAPPDATA"); local.has_value()) addVersions(fs::path{ *local } / "Android" / "Sdk" / "cmake");
    const fs::path visualStudioCMake = fs::path{ "Common7" } / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe";
    if (const std::optional<std::string> tools = Environment("KB_BUILD_TOOLS"); tools.has_value()) candidates.push_back(fs::path{ *tools } / visualStudioCMake);
    candidates.push_back(fs::path{ "C:/BuildTools" } / visualStudioCMake);
    for (const char* variable : { "ProgramFiles", "ProgramFiles(x86)" }) {
        const std::optional<std::string> base = Environment(variable);
        if (!base.has_value()) continue;
        candidates.push_back(fs::path{ *base } / "CMake" / "bin" / "cmake.exe");
        std::error_code error;
        for (const fs::directory_entry& year : fs::directory_iterator(fs::path{ *base } / "Microsoft Visual Studio", error)) {
            for (const char* edition : { "BuildTools", "Community", "Professional", "Enterprise" }) candidates.push_back(year.path() / edition / visualStudioCMake);
        }
    }
    for (const fs::path& cmake : candidates) {
        if (!IsFile(cmake)) continue;
        std::string path = cmake.parent_path().string() + ';';
        const fs::path ninja = cmake.parent_path().parent_path() / "Ninja";
        if (IsFolder(ninja)) path += ninja.string() + ';';
        SetEnvironmentVariableA("PATH", (path + Environment("PATH").value_or(std::string{})).c_str());
        return cmake;
    }
    return {};
}

[[nodiscard]] std::string AndroidToolchainProblem() {
    std::optional<std::string> sdk = Environment("ANDROID_SDK_ROOT");
    if (!sdk.has_value()) sdk = Environment("ANDROID_HOME");
    fs::path sdkRoot = sdk.has_value() ? fs::path{ *sdk } : fs::path{};
    if (sdkRoot.empty()) {
        if (const std::optional<std::string> local = Environment("LOCALAPPDATA"); local.has_value()) sdkRoot = fs::path{ *local } / "Android" / "Sdk";
    }
    std::string problems;
    if (sdkRoot.empty() || !IsFolder(sdkRoot / "build-tools")) {
        problems += "  - the Android SDK was not found: install Android Studio (or the command line tools), install \"Android SDK Build-Tools\" "
                    "and \"Android SDK Platform 36\" with its SDK Manager, then set ANDROID_SDK_ROOT to the SDK folder\n";
    } else if (!IsFolder(sdkRoot / "ndk")) {
        problems += "  - the Android NDK is missing in " + sdkRoot.generic_string() + ": install \"NDK (Side by side)\" with the SDK Manager\n";
    }
    if (PrepareJdk().empty()) {
        const std::optional<std::string> javaHome = Environment("JAVA_HOME");
        problems += std::string{ "  - no JDK 17 or newer was found" } + (javaHome.has_value() ? " (JAVA_HOME names " + *javaHome + ", which is none)" : "") +
            ": install one (Temurin 21 from adoptium.net, or Android Studio, which includes one); JAVA_HOME is then found on its own\n";
    }
    return problems;
}

[[nodiscard]] std::string WebToolchainProblem(const fs::path& emsdk) {
    if (emsdk.empty() || !IsFile(emsdk / "upstream" / "emscripten" / "emcc.py")) {
        return "  - the Emscripten SDK was not found" + (emsdk.empty() ? std::string{} : " in " + emsdk.generic_string()) +
               ": install it (git clone https://github.com/emscripten-core/emsdk, then emsdk install latest and emsdk activate latest) "
               "and pass --emsdk <folder> or set EMSDK\n";
    }
    return {};
}

[[nodiscard]] std::string CommonToolchainProblem(const fs::path& python, const fs::path& script, const fs::path& buildRoot) {
    std::string problems;
    if (!ProgramExists(python)) {
        problems += "  - Python was not found (" + python.generic_string() + "): install Python 3.10 or newer from python.org, or pass --python <executable>\n";
    }
    if (!IsFile(script)) problems += "  - the packaging script is missing: " + script.generic_string() + " (pass --engine-root <engine folder>)\n";
    if (PrepareCMake(buildRoot).empty()) {
        problems += "  - CMake was not found (not on PATH, in the Android SDK's cmake folder or in the Visual Studio C++ tools): "
                    "install CMake 3.25 or newer from cmake.org, or \"CMake\" with the Android SDK Manager\n";
    }
    return problems;
}

} // namespace

int RunPlatformBuild(const ArgumentList& arguments, CommandIo io) {
    const std::string platform = Lower(arguments.Option("--platform").value_or(std::string{}));
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: build --platform needs --project <dir>\n" << kPlatformUsage;
        return 1;
    }
    std::string targetId;
    if (platform == "android") targetId = Lower(arguments.Option("--texture").value_or("astc")) == "etc2" ? "Android.ETC2.arm64" : "Android.ASTC.arm64";
    else if (platform == "web") targetId = Lower(arguments.Option("--renderer").value_or("webgl")) == "webgpu" ? "WebGPU.wasm32" : "WebGL.wasm32";
    else if (platform == "linux") targetId = "Linux.x64";
    else {
        io.err << "error: --platform takes windows, android, web or linux, got '" << platform << "'\n" << kPlatformUsage;
        return 1;
    }
    const std::string profile = Lower(arguments.Option("--profile").value_or("development"));
    if (profile != "development" && profile != "release") {
        io.err << "error: --profile takes development or release, got '" << profile << "'\n";
        return 1;
    }
    const kb::packaging::PackagingTargetSpec* target = kb::packaging::FindPackagingTarget(targetId);
    if (target == nullptr) {
        io.err << "error: this engine has no package target " << targetId << '\n';
        return 1;
    }

    std::string error;
    const fs::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const fs::path projectRoot = projectFile.parent_path();
    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: the project could not be loaded: " << loaded.error << '\n';
        return 1;
    }
    const kb::project::ProjectSettingsLoadResult stored =
        kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(projectRoot));
    const kb::project::ProjectSettings settings = stored.found && stored.Succeeded() ? stored.settings
        : kb::project::ProjectSettingsStore::FromLegacy(loaded.legacySettings, projectFile);

    // The build settings the editor's Build Game panel saved for this project, then what this command line says.
    const kb::editor::EditorBuildGameSettingsLoadResult savedBuild =
        kb::editor::EditorBuildGameSettingsStore::Load(kb::editor::EditorBuildGameSettingsStore::FilePath(projectRoot));
    if (!savedBuild.Succeeded()) {
        io.err << "error: " << savedBuild.error << '\n';
        return 1;
    }
    kb::editor::EditorBuildGameSettings local = savedBuild.settings;
    kb::editor::EditorBuildGameTargetSettings& targetSettings = local.For(target->target);
    const auto absolute = [](const std::string& text) {
        std::error_code ec;
        const fs::path path = fs::absolute(kb::assets::Utf8Path(text), ec);
        return (ec ? kb::assets::Utf8Path(text) : path).lexically_normal();
    };
    const EngineFolders engine = [&]() {
        EngineFolders found = FindEngine();
        if (const std::optional<std::string> option = arguments.Option("--engine-root"); option.has_value()) found.root = absolute(*option);
        return found;
    }();
    if (const auto option = arguments.Option("--out")) targetSettings.outputDirectory = absolute(*option);
    if (const auto option = arguments.Option("--build-root")) local.buildRoot = absolute(*option);
    if (const auto option = arguments.Option("--python")) local.builderExecutable = kb::assets::Utf8Path(*option);
    if (const auto option = arguments.Option("--emsdk")) local.emsdkRoot = absolute(*option);
    if (const auto option = arguments.Option("--android-format")) targetSettings.androidFormat = Lower(*option);
    if (const auto option = arguments.Option("--android-keystore")) targetSettings.androidKeystore = absolute(*option);
    if (const auto option = arguments.Option("--android-key-alias")) targetSettings.androidKeyAlias = *option;
    if (const auto option = arguments.Option("--linux-host")) local.linuxHost = *option;
    if (const auto option = arguments.Option("--linux-user")) local.linuxUser = *option;
    if (const auto option = arguments.Option("--linux-host-key")) local.linuxHostKey = *option;
    if (const auto option = arguments.Option("--linux-identity")) local.linuxIdentity = absolute(*option);
    if (const auto option = arguments.Option("--linux-port")) local.linuxPort = static_cast<std::uint16_t>(std::strtoul(option->c_str(), nullptr, 10));
    if (local.buildRoot.empty()) local.buildRoot = engine.build;
    if (local.emsdkRoot.empty()) {
        if (const std::optional<std::string> emsdk = Environment("EMSDK"); emsdk.has_value()) local.emsdkRoot = fs::path{ *emsdk };
    }
    const bool release = profile == "release";

    // What this machine lacks, said together, before anything starts.
    std::string problems;
    if (engine.root.empty()) {
        problems += "  - the engine folder (the one holding scripts/package_game.py) was not found next to kb_cli: pass --engine-root <folder>\n";
    } else {
        problems += CommonToolchainProblem(local.builderExecutable, engine.root / "scripts" / "package_game.py", local.buildRoot);
    }
    if (!engine.root.empty() && !IsFile(engine.root / "prebuilt-engine.json")) problems += PrepareCompilerEnvironment();
    if (target->needsAndroidMetadata) problems += AndroidToolchainProblem();
    if (target->family == kb::packaging::PackagingTargetFamily::Web) problems += WebToolchainProblem(local.emsdkRoot);
    if (target->target == kb::packaging::PackagingTarget::LinuxX64 && (local.linuxHost.empty() || local.linuxUser.empty() || local.linuxHostKey.empty())) {
        problems += "  - a Linux package is built on a Linux machine over SSH: give --linux-host, --linux-user and --linux-host-key (the host's public key), "
                    "and --linux-identity <private key file>; the machine needs the engine, CMake and a C++ compiler\n";
    }
    if (!problems.empty()) {
        io.err << "error: cannot build for " << platform << " on this machine:\n" << problems;
        return 1;
    }
    if (targetSettings.outputDirectory.empty()) {
        io.err << "error: build --platform needs --out <folder> for the package (outside the project), or an output folder saved in the editor's Build Game panel\n";
        return 1;
    }
    std::string androidStorePassword = Environment("KB_ANDROID_STORE_PASSWORD").value_or(std::string{});
    std::string androidKeyPassword = Environment("KB_ANDROID_KEY_PASSWORD").value_or(std::string{});
    const kb::editor::BuildGameValidation validation = kb::editor::BuildGamePanelModel::Validate(
        target->target, settings, local, release, false, !androidStorePassword.empty(), !androidKeyPassword.empty(), false);
    if (!validation.canBuild) {
        io.err << "error: " << validation.reason << (release && target->needsAndroidMetadata
            ? " (a release Android package reads the keystore passwords from KB_ANDROID_STORE_PASSWORD and KB_ANDROID_KEY_PASSWORD)" : "") << '\n';
        return 1;
    }

    kb::editor::EditorPackageRequest request;
    if (!kb::editor::BuildPackageRequest({ .project = settings, .local = local, .target = *target, .release = release, .projectFile = projectFile,
            .projectRoot = projectRoot, .engineRoot = engine.root, .androidStorePassword = androidStorePassword,
            .androidKeyPassword = androidKeyPassword, .windowsCertificatePassword = {} }, request, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::editor::EditorProjectPackageService service;
    if (!service.Start(std::move(request), error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "building " << platform << " (" << targetId << ", " << profile << "); this takes minutes the first time\n";
    io.out.flush();
    const bool verbose = arguments.Flag("--verbose");
    std::size_t shown = 0U;
    std::string lastStatus;
    for (;;) {
        const kb::editor::EditorPackageSnapshot snapshot = service.Snapshot();
        // Warnings and errors always; the script's long progress text with --verbose.
        for (; shown < snapshot.diagnostics.size(); ++shown) {
            const kb::editor::EditorPackageDiagnostic& diagnostic = snapshot.diagnostics[shown];
            if (diagnostic.severity == kb::editor::EditorPackageDiagnosticSeverity::Info && !verbose) continue;
            io.out << (diagnostic.severity == kb::editor::EditorPackageDiagnosticSeverity::Error ? "error: "
                : diagnostic.severity == kb::editor::EditorPackageDiagnosticSeverity::Warning ? "warning: " : "") << diagnostic.message << '\n';
        }
        if (snapshot.state != kb::editor::EditorPackageJobState::Running) {
            io.out.flush();
            if (snapshot.state == kb::editor::EditorPackageJobState::Succeeded) {
                io.out << "built " << platform << " package: " << snapshot.resultDirectory.generic_string() << '\n';
                return 0;
            }
            io.err << "error: the " << platform << " build " << (snapshot.state == kb::editor::EditorPackageJobState::Cancelled ? "was cancelled" : "failed")
                   << (snapshot.status.empty() ? std::string{} : ": " + snapshot.status) << '\n';
            return 1;
        }
        if (snapshot.status != lastStatus) {
            lastStatus = snapshot.status;
            if (!lastStatus.empty()) io.out << lastStatus << '\n';
            io.out.flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
    }
}

#else

int RunPlatformBuild(const ArgumentList&, CommandIo io) {
    io.err << "error: build --platform runs the Windows editor's package pipeline; on this system use scripts/package_game.py\n";
    return 1;
}

#endif

} // namespace kb::cli
