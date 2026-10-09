#include "scene/EditorScriptAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

#include "project/EditorProjectPaths.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::editor {
namespace {

constexpr std::string_view kLuaExtension = ".lua";

// Starter template so a freshly created script already ticks in play mode and
// proves scripting works by printing to the editor Console once on the first tick.
constexpr std::string_view kLuaTemplate =
    "-- Lua behaviour script. Runs every frame in play mode.\n"
    "-- 'self' is the entity this script is attached to; 'dt' is the frame delta.\n"
    "-- Log(\"text\") prints to the editor Console.\n"
    "\n"
    "local started = false\n"
    "\n"
    "function Tick(self, dt)\n"
    "    if not started then\n"
    "        started = true\n"
    "        Log(\"Hello from Lua! Scripting works.\")\n"
    "    end\n"
    "end\n";

[[nodiscard]] std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName) {
    std::filesystem::path candidate = folder / (std::string{ baseName } + std::string{ kLuaExtension });
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = folder / (std::string{ baseName } + std::to_string(suffix) + std::string{ kLuaExtension });
        ++suffix;
    }
    return candidate;
}

// An engine a C++ script can build against: its headers, and Release libraries laid out as a build tree
// lays them out. A multi-config tree keeps each library in a Release folder; a single-config tree does not.
struct NativeSdk {
    std::filesystem::path root;
    std::filesystem::path buildTree;
    std::filesystem::path configFolder;
    // A single-config tree also names the tools it was built with, so a script builds with the same
    // compiler and does not need a Visual Studio installation: CMake, Ninja, the MSVC compiler, and the
    // script that sets up that compiler's environment, with the toolset version to ask it for.
    std::filesystem::path cmake;
    std::filesystem::path ninja;
    std::filesystem::path compiler;
    std::filesystem::path compilerEnvironment;
    std::string toolsetVersion;

    [[nodiscard]] std::array<std::filesystem::path, 6> Libraries() const {
        return {
            (buildTree / "engine" / configFolder / "kb_engine.lib").lexically_normal(),
            (buildTree / "third_party/flecs" / configFolder / "flecs_static.lib").lexically_normal(),
            (buildTree / configFolder / "kb_lua.lib").lexically_normal(),
            (buildTree / configFolder / "kb_ufbx.lib").lexically_normal(),
            (buildTree / configFolder / "kb_monocypher.lib").lexically_normal(),
            (buildTree / configFolder / "kb_zstd.lib").lexically_normal(),
        };
    }
};

[[nodiscard]] bool HasNativeSdk(const NativeSdk& sdk) {
    if (!std::filesystem::is_regular_file(sdk.root / "sources/engine/include/engine/script/NativeScriptPlugin.hpp")) {
        return false;
    }
    for (const std::filesystem::path& library : sdk.Libraries()) {
        if (!std::filesystem::is_regular_file(library)) {
            return false;
        }
    }
    return true;
}

// An engine checkout at `root` with its Release libraries in the multi-config tree `root/build`.
[[nodiscard]] NativeSdk CheckoutSdk(const std::filesystem::path& root) {
    return NativeSdk{ .root = root, .buildTree = root / "build", .configFolder = "Release" };
}

// The vcvars64.bat of the MSVC installation `compiler` (.../VC/Tools/MSVC/<version>/bin/Hostx64/x64/cl.exe)
// belongs to, and the toolset version (major.minor) that compiler is.
void FindCompilerEnvironment(NativeSdk& sdk) {
    for (std::filesystem::path folder = sdk.compiler.parent_path(); folder.has_relative_path(); folder = folder.parent_path()) {
        if (folder.parent_path().filename() == "MSVC") {
            const std::string version = folder.filename().string();
            const std::size_t minorEnd = version.find('.', version.find('.') + 1U);
            sdk.toolsetVersion = version.substr(0U, minorEnd);
        }
        if (const std::filesystem::path environment = folder / "Auxiliary/Build/vcvars64.bat"; std::filesystem::is_regular_file(environment)) {
            sdk.compilerEnvironment = environment;
            return;
        }
    }
}

// The single-config Release build tree the running editor came from, which holds the libraries it links.
[[nodiscard]] std::optional<NativeSdk> EditorBuildTreeSdk() {
#if defined(_WIN32)
    std::wstring module(1024U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0U || length >= module.size()) {
        return std::nullopt;
    }
    module.resize(length);
    const std::filesystem::path buildTree = std::filesystem::path{ module }.parent_path().parent_path();
    std::ifstream cache{ buildTree / "CMakeCache.txt" };
    std::string line;
    NativeSdk sdk{ .buildTree = buildTree };
    bool release = false;
    bool ninjaGenerator = false;
    while (std::getline(cache, line)) {
        const std::string value = line.substr(line.find('=') + 1U);
        if (line.starts_with("CMAKE_HOME_DIRECTORY:INTERNAL=")) {
            sdk.root = std::filesystem::path{ value };
        } else if (line.starts_with("CMAKE_BUILD_TYPE:")) {
            release = value == "Release" || value == "RelWithDebInfo";
        } else if (line.starts_with("CMAKE_GENERATOR:INTERNAL=")) {
            ninjaGenerator = value == "Ninja";
        } else if (line.starts_with("CMAKE_COMMAND:INTERNAL=")) {
            sdk.cmake = std::filesystem::path{ value };
        } else if (line.starts_with("CMAKE_MAKE_PROGRAM:")) {
            sdk.ninja = std::filesystem::path{ value };
        } else if (line.starts_with("CMAKE_CXX_COMPILER:")) {
            sdk.compiler = std::filesystem::path{ value };
        }
    }
    if (sdk.root.empty() || !release) {
        return std::nullopt;
    }
    if (ninjaGenerator && std::filesystem::is_regular_file(sdk.cmake) && std::filesystem::is_regular_file(sdk.ninja) &&
        sdk.compiler.filename() == "cl.exe" && std::filesystem::is_regular_file(sdk.compiler)) {
        FindCompilerEnvironment(sdk);
    }
    if (sdk.compilerEnvironment.empty()) {
        sdk.cmake.clear();
        sdk.ninja.clear();
        sdk.compiler.clear();
    }
    return sdk;
#endif
    return std::nullopt;
}

[[nodiscard]] std::optional<NativeSdk> FindNativeSdk() {
    char* configuredValue = nullptr;
    std::size_t configuredLength = 0;
    if (_dupenv_s(&configuredValue, &configuredLength, "KB_ENGINE_SDK_ROOT") != 0) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> configured{ configuredValue, &std::free };
    if (configured && *configured != '\0') {
        const NativeSdk sdk = CheckoutSdk(std::filesystem::path{ configured.get() });
        return HasNativeSdk(sdk) ? std::optional<NativeSdk>{ sdk } : std::nullopt;
    }
    for (std::filesystem::path probe = std::filesystem::current_path(); !probe.empty(); probe = probe.parent_path()) {
        if (HasNativeSdk(CheckoutSdk(probe))) {
            return CheckoutSdk(probe);
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }
    if (const std::optional<NativeSdk> sdk = EditorBuildTreeSdk(); sdk.has_value() && HasNativeSdk(*sdk)) {
        return sdk;
    }
    return std::nullopt;
}

[[nodiscard]] std::string NativeSource(std::string_view name) {
    std::string source =
        "#include \"engine/script/NativeScriptPlugin.hpp\"\n"
        "#include \"engine/script/ScriptFunctionRegistry.hpp\"\n"
        "#include \"engine/script/ScriptValue.hpp\"\n\n"
        "#include <array>\n#include <string>\n\n"
        "extern \"C\" void ecs_os_set_api_defaults(void);\n\n"
        "class @NAME@ final {\n"
        "public:\n"
        "    static void Ready(kb::script::ScriptExecutionContext* context) {\n"
        "        if (context == nullptr) { return; }\n"
        "        const std::array arguments{\n"
        "            kb::script::ScriptFunctionArgument{ \"message\", kb::script::ScriptValue{ std::string{ \"@NAME@ ready\" } } }\n"
        "        };\n"
        "        static_cast<void>(context->CallFunction(\"Log\", arguments));\n"
        "    }\n"
        "};\n\n"
        "KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api) {\n"
        "    ecs_os_set_api_defaults();\n"
        "    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&\n"
        "        api->registerLifecycle != nullptr &&\n"
        "        api->registerLifecycle(api->user, \"project.@NAME@\", kb::script::ScriptLifecycleEvent::Ready, &@NAME@::Ready);\n"
        "}\n";
    for (std::size_t position = 0; (position = source.find("@NAME@", position)) != std::string::npos; position += name.size()) {
        source.replace(position, 6, name);
    }
    return source;
}

[[nodiscard]] std::string Quoted(const std::filesystem::path& path) {
    return "\"" + std::filesystem::path{ path }.make_preferred().string() + "\"";
}

// Run by the engine through the command shell from the project root.
[[nodiscard]] std::string NativeBuildCommand(std::string_view name, const NativeSdk& sdk) {
    const std::string target{ name };
    const std::string source = "\"Source/NativeScripts/" + target + "\"";
    const std::string binary = "\"Saved/NativeScripts/" + target + "\"";
    if (sdk.compilerEnvironment.empty()) {
        return "cmake -S " + source + " -B " + binary + " -A x64 && cmake --build " + binary + " --config Release --target " + target;
    }
    const std::string cmake = Quoted(sdk.cmake);
    const std::string toolset = sdk.toolsetVersion.empty() ? std::string{} : " -vcvars_ver=" + sdk.toolsetVersion;
    return "call " + Quoted(sdk.compilerEnvironment) + toolset + " >nul && " +
        cmake + " -S " + source + " -B " + binary + " -G Ninja -DCMAKE_BUILD_TYPE=Release " +
        Quoted("-DCMAKE_MAKE_PROGRAM=" + sdk.ninja.generic_string()) + " " + Quoted("-DCMAKE_CXX_COMPILER=" + sdk.compiler.generic_string()) +
        " && " + cmake + " --build " + binary + " --target " + target;
}

[[nodiscard]] std::string NativeCmake(std::string_view name, const NativeSdk& sdk) {
    const std::array<std::filesystem::path, 6> libraries = sdk.Libraries();
    const std::string target{ name };
    // KB_ENGINE_SDK_ROOT, when set at configure time, names an engine checkout with a multi-config build.
    return "cmake_minimum_required(VERSION 3.25)\n"
        "project(" + target + " LANGUAGES CXX)\n"
        "if(DEFINED ENV{KB_ENGINE_SDK_ROOT})\n"
        "    set(KB_NATIVE_SDK \"$ENV{KB_ENGINE_SDK_ROOT}\")\n"
        "    set(KB_NATIVE_LIBRARIES\n"
        "        \"${KB_NATIVE_SDK}/build/engine/Release/kb_engine.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/third_party/flecs/Release/flecs_static.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_lua.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_ufbx.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_monocypher.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_zstd.lib\")\n"
        "else()\n"
        "    set(KB_NATIVE_SDK \"" + sdk.root.generic_string() + "\")\n"
        "    set(KB_NATIVE_LIBRARIES\n"
        "        \"" + libraries[0].generic_string() + "\"\n"
        "        \"" + libraries[1].generic_string() + "\"\n"
        "        \"" + libraries[2].generic_string() + "\"\n"
        "        \"" + libraries[3].generic_string() + "\"\n"
        "        \"" + libraries[4].generic_string() + "\"\n"
        "        \"" + libraries[5].generic_string() + "\")\n"
        "endif()\n"
        "foreach(library IN LISTS KB_NATIVE_LIBRARIES)\n"
        "    if(NOT EXISTS \"${library}\")\n"
        "        message(FATAL_ERROR \"C++ script SDK library is unavailable: ${library}\")\n"
        "    endif()\n"
        "endforeach()\n"
        "add_library(" + target + " SHARED " + target + ".cpp)\n"
        "target_compile_features(" + target + " PRIVATE cxx_std_20)\n"
        "target_include_directories(" + target + " PRIVATE \"${KB_NATIVE_SDK}/sources/engine/include\")\n"
        "target_link_libraries(" + target + " PRIVATE ${KB_NATIVE_LIBRARIES} user32 xinput bcrypt)\n"
        "set_target_properties(" + target + " PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE \"${CMAKE_CURRENT_LIST_DIR}/../../../Binaries/NativeScripts\")\n";
}

} // namespace

EditorScriptAssetGateway::EditorScriptAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept
    : scene_(scene)
    , browser_(browser) {}

std::optional<std::filesystem::path> EditorScriptAssetGateway::ResolveFolder(const std::filesystem::path& virtualFolder) const {
    if (virtualFolder.empty()) {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> probe = scene_.Assets().Manager().Mounts().Resolve(virtualFolder / "probe");
    return probe.has_value() ? std::optional<std::filesystem::path>{ probe->parent_path() } : std::nullopt;
}

void EditorScriptAssetGateway::DiscoverAndSelect(const std::filesystem::path& path) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(scene_.Assets().Discover());
    if (const std::optional<std::filesystem::path> created = manager.Mounts().ToVirtual(path)) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*created); metadata != nullptr) {
            static_cast<void>(browser_.SelectAsset(metadata->id, manager));
        }
    }
}

std::optional<std::filesystem::path> EditorScriptAssetGateway::CreateLuaScript(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        return std::nullopt;
    }
    const std::filesystem::path path = UniqueFilePath(*folder, "NewScript");
    if (!WriteSource(path, kLuaTemplate)) {
        return std::nullopt;
    }
    DiscoverAndSelect(path);
    return path;
}

std::optional<std::filesystem::path> EditorScriptAssetGateway::CreateNativeScript(
    const std::filesystem::path& virtualFolder, std::string& error) {
    const std::optional<std::filesystem::path> folder = ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        error = "C++ script destination is not a mounted asset folder: " + virtualFolder.generic_string();
        return std::nullopt;
    }
    const std::optional<NativeSdk> sdk = FindNativeSdk();
    if (!sdk.has_value()) {
        error = "C++ script SDK was not found. Set KB_ENGINE_SDK_ROOT to an engine build with Release libraries.";
        return std::nullopt;
    }

    const std::filesystem::path projectRoot = EditorProjectPaths::ProjectRoot();
    std::string name = "NewScript";
    for (unsigned suffix = 1; std::filesystem::exists(*folder / (name + ".native")) ||
             std::filesystem::exists(projectRoot / "Source/NativeScripts" / name) ||
             std::filesystem::exists(projectRoot / "Binaries/NativeScripts" / (name + ".dll")); ++suffix) {
        name = "NewScript" + std::to_string(suffix);
    }
    const std::filesystem::path sourceDir = projectRoot / "Source/NativeScripts" / name;
    const std::filesystem::path sourcePath = sourceDir / (name + ".cpp");
    const std::filesystem::path cmakePath = sourceDir / "CMakeLists.txt";
    const std::filesystem::path descriptorPath = *folder / (name + ".native");
    std::error_code fileError;
    std::filesystem::create_directories(sourceDir, fileError);
    if (fileError) {
        error = "C++ script source folder could not be created: " + fileError.message();
        return std::nullopt;
    }

    const std::filesystem::path relativeSource = sourcePath.lexically_relative(*folder);
    const std::filesystem::path relativeModule = (projectRoot / "Binaries/NativeScripts" / (name + ".dll")).lexically_relative(*folder);
    const std::filesystem::path relativeRoot = projectRoot.lexically_relative(*folder);
    if (relativeSource.empty() || relativeModule.empty() || relativeRoot.empty()) {
        error = "C++ script paths could not be resolved within the project.";
    } else {
        const std::string descriptor =
            "name = " + name + "\n"
            "symbol = project." + name + "\n"
            "source = " + relativeSource.generic_string() + "\n"
            "module = " + relativeModule.generic_string() + "\n"
            "entry = kb_register_native_scripts\n"
            "build_working_directory = " + relativeRoot.generic_string() + "\n"
            "build = " + NativeBuildCommand(name, *sdk) + "\n";
        if (WriteSource(sourcePath, NativeSource(name)) &&
            WriteSource(cmakePath, NativeCmake(name, *sdk)) &&
            WriteSource(descriptorPath, descriptor)) {
            DiscoverAndSelect(descriptorPath);
            return descriptorPath;
        }
        error = "C++ script files could not be written in: " + sourceDir.generic_string();
    }
    std::filesystem::remove(descriptorPath, fileError);
    std::filesystem::remove(sourcePath, fileError);
    std::filesystem::remove(cmakePath, fileError);
    std::filesystem::remove(sourceDir, fileError);
    return std::nullopt;
}

std::string EditorScriptAssetGateway::ReadSource(const std::filesystem::path& path) {
    return ScriptSourceFile::Read(path);
}

bool EditorScriptAssetGateway::WriteSource(const std::filesystem::path& path, std::string_view text) {
    return ScriptSourceFile::Write(path, text);
}

} // namespace kb::editor
