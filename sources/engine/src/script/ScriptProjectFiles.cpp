#include "engine/script/ScriptProjectFiles.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/platform/FileSystemPath.hpp"
#include "engine/script/ScriptAsset.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <system_error>

namespace kb::script {
namespace {

// Starter template: the two hooks every behaviour starts from, named as the engine calls them and
// as the init-agent documentation describes them, so the script proves scripting works on Play.
constexpr std::string_view kLuaTemplate =
    "-- Lua behaviour script. 21kb calls these functions on the object it is attached to:\n"
    "--   Ready(self, dt)  once, when the object starts in Play (Unity: Start, Godot: _ready)\n"
    "--   Tick(self, dt)   every frame in Play (Unity: Update, Godot: _process); dt is in seconds\n"
    "-- Other hooks: Created, Activated, FixedTick, LateTick, Deactivated, Destroyed.\n"
    "-- Log(\"text\") prints to the editor Console.\n"
    "\n"
    "function Ready(self, dt)\n"
    "    Log(\"Hello from Lua! Scripting works.\")\n"
    "end\n"
    "\n"
    "function Tick(self, dt)\n"
    "end\n";

constexpr std::string_view kDefaultName = "NewScript";

// The words C++20 reserves: a class or a CMake target cannot be called that.
constexpr std::array<std::string_view, 92> kCppKeywords{
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
    "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
    "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
    "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast",
    "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
};

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

// A path the system cannot look at counts as free: writing there then fails with its own reason.
[[nodiscard]] bool Exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

[[nodiscard]] bool WriteFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

[[nodiscard]] std::string EnvironmentValue(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    const std::unique_ptr<char, decltype(&std::free)> owned{ value, &std::free };
    return std::string{ owned.get() };
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{ value };
#endif
}

// The files a C++ script `name` consists of.
struct NativeScriptFiles {
    std::filesystem::path descriptor;
    std::filesystem::path sourceFolder;
    std::filesystem::path source;
    std::filesystem::path cmake;
    std::filesystem::path module;
};

[[nodiscard]] NativeScriptFiles NativeFiles(const std::filesystem::path& projectRoot, const std::filesystem::path& folder,
    std::string_view name) {
    const std::string target{ name };
    const std::filesystem::path sourceFolder = projectRoot / "Source/NativeScripts" / target;
    return NativeScriptFiles{
        .descriptor = folder / (target + ".native"),
        .sourceFolder = sourceFolder,
        .source = sourceFolder / (target + ".cpp"),
        .cmake = sourceFolder / "CMakeLists.txt",
        .module = projectRoot / "Binaries/NativeScripts" / (target + ".dll"),
    };
}

// Why a script `name` would overwrite something, or empty.
[[nodiscard]] std::string Collision(ScriptLanguage language, const std::filesystem::path& projectRoot,
    const std::filesystem::path& folder, std::string_view name) {
    if (language == ScriptLanguage::Lua) {
        const std::filesystem::path file = folder / kb::assets::Utf8Path(std::string{ name } + ".lua");
        return Exists(file) ? "a script named " + std::string{ name } + ".lua already exists in that folder" : std::string{};
    }
    const NativeScriptFiles files = NativeFiles(projectRoot, folder, name);
    for (const std::filesystem::path& taken : { files.descriptor, files.sourceFolder, files.module }) {
        if (Exists(taken)) {
            return "a C++ script named " + std::string{ name } + " already exists: " +
                Utf8Text(taken.lexically_relative(projectRoot));
        }
    }
    return {};
}

// An engine a C++ script can build against: its headers, and Release libraries laid out as a build tree
// lays them out. A multi-config tree keeps each library in a Release folder; a single-config tree does not.
struct NativeSdk {
    std::filesystem::path root;
    std::filesystem::path buildTree;
    std::filesystem::path configFolder;
    std::string generator;
    // A single-config tree also names the tools it was built with, so a script builds with the same
    // compiler and does not need a Visual Studio installation: CMake, Ninja, the MSVC compiler, and the
    // script that sets up that compiler's environment, with the toolset version to ask it for.
    std::filesystem::path cmake;
    std::filesystem::path ninja;
    std::filesystem::path compiler;
    std::filesystem::path compilerEnvironment;
    std::string toolsetVersion;

    [[nodiscard]] std::array<std::filesystem::path, 10> Libraries() const {
        return {
            (buildTree / "engine" / configFolder / "kb_engine.lib").lexically_normal(),
            (buildTree / "third_party/flecs" / configFolder / "flecs_static.lib").lexically_normal(),
            (buildTree / configFolder / "kb_lua.lib").lexically_normal(),
            (buildTree / configFolder / "kb_ufbx.lib").lexically_normal(),
            (buildTree / configFolder / "kb_monocypher.lib").lexically_normal(),
            (buildTree / configFolder / "kb_zstd.lib").lexically_normal(),
            (buildTree / configFolder / "kb_recast.lib").lexically_normal(),
            (buildTree / configFolder / "kb_ui_text_shaping.lib").lexically_normal(),
            (buildTree / configFolder / "kb_harfbuzz.lib").lexically_normal(),
            (buildTree / configFolder / "kb_sheenbidi.lib").lexically_normal(),
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

// Find the build cache above the executable, including a multi-config output directory.
[[nodiscard]] std::optional<NativeSdk> RunningBuildTreeSdk() {
    const std::filesystem::path executable = kb::platform::RunningExecutablePath();
    if (executable.empty()) {
        return std::nullopt;
    }
    std::filesystem::path buildTree = executable.parent_path();
    while (!std::filesystem::is_regular_file(buildTree / "CMakeCache.txt")) {
        const std::filesystem::path parent = buildTree.parent_path();
        if (parent.empty() || parent == buildTree) return std::nullopt;
        buildTree = parent;
    }
    std::ifstream cache{ buildTree / "CMakeCache.txt" };
    std::string line;
    NativeSdk sdk{ .buildTree = buildTree };
    bool release = false;
    bool ninjaGenerator = false;
    std::string configurations;
    while (std::getline(cache, line)) {
        const std::string value = line.substr(line.find('=') + 1U);
        if (line.starts_with("CMAKE_HOME_DIRECTORY:INTERNAL=")) {
            sdk.root = std::filesystem::path{ value };
        } else if (line.starts_with("CMAKE_BUILD_TYPE:")) {
            release = value == "Release" || value == "RelWithDebInfo";
        } else if (line.starts_with("CMAKE_CONFIGURATION_TYPES:")) {
            configurations = ';' + value + ';';
        } else if (line.starts_with("CMAKE_GENERATOR:INTERNAL=")) {
            sdk.generator = value;
            ninjaGenerator = value == "Ninja" || value == "Ninja Multi-Config";
        } else if (line.starts_with("CMAKE_COMMAND:INTERNAL=")) {
            sdk.cmake = std::filesystem::path{ value };
        } else if (line.starts_with("CMAKE_MAKE_PROGRAM:")) {
            sdk.ninja = std::filesystem::path{ value };
        } else if (line.starts_with("CMAKE_CXX_COMPILER:")) {
            sdk.compiler = std::filesystem::path{ value };
        }
    }
    if (!configurations.empty()) {
        const std::string configuration = executable.parent_path().filename().string();
        release = (configuration == "Release" || configuration == "RelWithDebInfo") &&
            configurations.find(';' + configuration + ';') != std::string::npos;
        if (release) sdk.configFolder = configuration;
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
}

[[nodiscard]] std::optional<NativeSdk> FindNativeSdk() {
    if (const std::string configured = EnvironmentValue("KB_ENGINE_SDK_ROOT"); !configured.empty()) {
        const NativeSdk sdk = CheckoutSdk(std::filesystem::path{ configured });
        return HasNativeSdk(sdk) ? std::optional<NativeSdk>{ sdk } : std::nullopt;
    }
    std::error_code error;
    for (std::filesystem::path probe = std::filesystem::current_path(error); !probe.empty(); probe = probe.parent_path()) {
        if (HasNativeSdk(CheckoutSdk(probe))) {
            return CheckoutSdk(probe);
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }
    if (const std::optional<NativeSdk> sdk = RunningBuildTreeSdk(); sdk.has_value() && HasNativeSdk(*sdk)) {
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
        const std::string generator = sdk.generator.starts_with("Visual Studio ")
            ? " -G \"" + sdk.generator + "\"" : std::string{};
        return "cmake -S " + source + " -B " + binary + generator + " -A x64 && cmake --build " + binary + " --config Release --target " + target;
    }
    const std::string cmake = Quoted(sdk.cmake);
    const std::string toolset = sdk.toolsetVersion.empty() ? std::string{} : " -vcvars_ver=" + sdk.toolsetVersion;
    return "call " + Quoted(sdk.compilerEnvironment) + toolset + " >nul && " +
        cmake + " -S " + source + " -B " + binary + " -G Ninja -DCMAKE_BUILD_TYPE=Release " +
        Quoted("-DCMAKE_MAKE_PROGRAM=" + sdk.ninja.generic_string()) + " " + Quoted("-DCMAKE_CXX_COMPILER=" + sdk.compiler.generic_string()) +
        " && " + cmake + " --build " + binary + " --target " + target;
}

[[nodiscard]] std::string NativeCmake(std::string_view name, const NativeSdk& sdk) {
    const auto libraries = sdk.Libraries();
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
        "        \"${KB_NATIVE_SDK}/build/Release/kb_zstd.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_recast.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_ui_text_shaping.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_harfbuzz.lib\"\n"
        "        \"${KB_NATIVE_SDK}/build/Release/kb_sheenbidi.lib\")\n"
        "else()\n"
        "    set(KB_NATIVE_SDK \"" + sdk.root.generic_string() + "\")\n"
        "    set(KB_NATIVE_LIBRARIES\n"
        "        \"" + libraries[0].generic_string() + "\"\n"
        "        \"" + libraries[1].generic_string() + "\"\n"
        "        \"" + libraries[2].generic_string() + "\"\n"
        "        \"" + libraries[3].generic_string() + "\"\n"
        "        \"" + libraries[4].generic_string() + "\"\n"
        "        \"" + libraries[5].generic_string() + "\"\n"
        "        \"" + libraries[6].generic_string() + "\"\n"
        "        \"" + libraries[7].generic_string() + "\"\n"
        "        \"" + libraries[8].generic_string() + "\"\n"
        "        \"" + libraries[9].generic_string() + "\")\n"
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

[[nodiscard]] CreatedScript CreateLua(const std::filesystem::path& folder, std::string_view name) {
    const std::filesystem::path file = folder / kb::assets::Utf8Path(std::string{ name } + ".lua");
    if (!WriteFile(file, kLuaTemplate)) {
        return { {}, {}, "the file " + std::string{ name } + ".lua could not be written in " + Utf8Text(folder) };
    }
    return { file, file, {} };
}

[[nodiscard]] CreatedScript CreateCpp(const std::filesystem::path& projectRoot, const std::filesystem::path& folder,
    std::string_view name) {
    const std::optional<NativeSdk> sdk = FindNativeSdk();
    if (!sdk.has_value()) {
        return { {}, {}, "the C++ script SDK was not found. Set KB_ENGINE_SDK_ROOT to an engine build with Release libraries." };
    }
    const NativeScriptFiles files = NativeFiles(projectRoot, folder, name);
    const std::filesystem::path relativeSource = files.source.lexically_relative(folder);
    const std::filesystem::path relativeModule = files.module.lexically_relative(folder);
    const std::filesystem::path relativeRoot = projectRoot.lexically_relative(folder);
    if (relativeSource.empty() || relativeModule.empty() || relativeRoot.empty()) {
        return { {}, {}, "the C++ script's paths could not be resolved within the project" };
    }
    std::error_code error;
    std::filesystem::create_directories(files.sourceFolder, error);
    if (error) {
        return { {}, {}, "the C++ script's source folder could not be created: " + error.message() };
    }
    const std::string target{ name };
    const std::string descriptor =
        "name = " + target + "\n"
        "symbol = project." + target + "\n"
        "source = " + relativeSource.generic_string() + "\n"
        "module = " + relativeModule.generic_string() + "\n"
        "entry = kb_register_native_scripts\n"
        "build_working_directory = " + relativeRoot.generic_string() + "\n"
        "build = " + NativeBuildCommand(name, *sdk) + "\n";
    if (WriteFile(files.source, NativeSource(name)) && WriteFile(files.cmake, NativeCmake(name, *sdk)) &&
        WriteFile(files.descriptor, descriptor)) {
        return { files.descriptor, files.source, {} };
    }
    // The name was free, so every file here is this call's own.
    std::filesystem::remove(files.descriptor, error);
    std::filesystem::remove_all(files.sourceFolder, error);
    return { {}, {}, "the C++ script's files could not be written in " + Utf8Text(files.sourceFolder) };
}

} // namespace

std::string ScriptProjectFiles::NameError(ScriptLanguage language, std::string_view name) {
    if (std::string problem = kb::assets::AssetFileNameError(name); !problem.empty()) {
        return problem;
    }
    if (language == ScriptLanguage::Lua) {
        return {};
    }
    const auto letter = [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_';
    };
    const auto digit = [](char character) { return character >= '0' && character <= '9'; };
    if (!letter(name.front()) || !std::all_of(name.begin(), name.end(), [&](char character) {
            return letter(character) || digit(character);
        })) {
        return "A C++ script's name is its class name: English letters, digits and _, not starting with a digit.";
    }
    if (std::find(kCppKeywords.begin(), kCppKeywords.end(), name) != kCppKeywords.end()) {
        return "'" + std::string{ name } + "' is a C++ keyword and can't name a class.";
    }
    return {};
}

std::string ScriptProjectFiles::FreeName(ScriptLanguage language, const std::filesystem::path& projectRoot,
    const std::filesystem::path& folder) {
    std::string name{ kDefaultName };
    for (unsigned suffix = 1; !Collision(language, projectRoot, folder, name).empty(); ++suffix) {
        name = std::string{ kDefaultName } + std::to_string(suffix);
    }
    return name;
}

CreatedScript ScriptProjectFiles::Create(ScriptLanguage language, const std::filesystem::path& projectRoot,
    const std::filesystem::path& folder, std::string_view name) {
    if (std::string problem = NameError(language, name); !problem.empty()) {
        return { {}, {}, std::move(problem) };
    }
    if (std::string taken = Collision(language, projectRoot, folder, name); !taken.empty()) {
        return { {}, {}, std::move(taken) + ". Choose another name." };
    }
    // A folder the project names but the disk does not have yet (a fresh project's Scripts) is made.
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        return { {}, {}, "the folder " + Utf8Text(folder) + " could not be created: " + error.message() };
    }
    return language == ScriptLanguage::Lua ? CreateLua(folder, name) : CreateCpp(projectRoot, folder, name);
}

NativeScriptBuildDesc ScriptProjectFiles::BuildOf(const NativeBehaviourDescriptor& descriptor,
    const std::filesystem::path& descriptorFile, const std::filesystem::path& projectRoot) {
    NativeScriptBuildDesc build = descriptor.build;
    if (build.workingDirectory.empty()) {
        build.workingDirectory = projectRoot;
    } else if (build.workingDirectory.is_relative()) {
        build.workingDirectory = (descriptorFile.parent_path() / build.workingDirectory).lexically_normal();
    }
    return build;
}

std::filesystem::path ScriptProjectFiles::ModuleOf(const NativeBehaviourDescriptor& descriptor,
    const std::filesystem::path& descriptorFile) {
    if (descriptor.modulePath.empty() || descriptor.modulePath.is_absolute()) {
        return descriptor.modulePath;
    }
    return (descriptorFile.parent_path() / descriptor.modulePath).lexically_normal();
}

} // namespace kb::script
