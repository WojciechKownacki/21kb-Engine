#include "scene/EditorScriptAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

#include "project/EditorProjectPaths.hpp"

#include <cstdlib>
#include <memory>
#include <system_error>

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

[[nodiscard]] bool HasNativeSdk(const std::filesystem::path& root) {
    return std::filesystem::is_regular_file(root / "sources/engine/include/engine/script/NativeScriptPlugin.hpp") &&
        std::filesystem::is_regular_file(root / "build/engine/Release/kb_engine.lib") &&
        std::filesystem::is_regular_file(root / "build/third_party/flecs/Release/flecs_static.lib") &&
        std::filesystem::is_regular_file(root / "build/Release/kb_lua.lib") &&
        std::filesystem::is_regular_file(root / "build/Release/kb_ufbx.lib");
}

[[nodiscard]] std::optional<std::filesystem::path> FindNativeSdk() {
    char* configuredValue = nullptr;
    std::size_t configuredLength = 0;
    if (_dupenv_s(&configuredValue, &configuredLength, "KB_ENGINE_SDK_ROOT") != 0) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> configured{ configuredValue, &std::free };
    if (configured && *configured != '\0') {
        const std::filesystem::path root{ configured.get() };
        return HasNativeSdk(root) ? std::optional<std::filesystem::path>{ root } : std::nullopt;
    }
    for (std::filesystem::path probe = std::filesystem::current_path(); !probe.empty(); probe = probe.parent_path()) {
        if (HasNativeSdk(probe)) {
            return probe;
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string NativeSource(std::string_view name) {
    std::string source =
        "#include \"engine/script/NativeScriptPlugin.hpp\"\n"
        "#include \"engine/script/ScriptFunctionRegistry.hpp\"\n"
        "#include \"engine/script/ScriptValue.hpp\"\n\n"
        "#include <array>\n#include <string>\n\n"
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
        "    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&\n"
        "        api->registerLifecycle != nullptr &&\n"
        "        api->registerLifecycle(api->user, \"project.@NAME@\", kb::script::ScriptLifecycleEvent::Ready, &@NAME@::Ready);\n"
        "}\n";
    for (std::size_t position = 0; (position = source.find("@NAME@", position)) != std::string::npos; position += name.size()) {
        source.replace(position, 6, name);
    }
    return source;
}

[[nodiscard]] std::string NativeCmake(std::string_view name, const std::filesystem::path& sdk) {
    const std::string root = sdk.generic_string();
    const std::string target{ name };
    return "cmake_minimum_required(VERSION 3.25)\n"
        "project(" + target + " LANGUAGES CXX)\n"
        "if(DEFINED ENV{KB_ENGINE_SDK_ROOT})\n"
        "    set(KB_NATIVE_SDK \"$ENV{KB_ENGINE_SDK_ROOT}\")\n"
        "else()\n"
        "    set(KB_NATIVE_SDK \"" + root + "\")\n"
        "endif()\n"
        "if(NOT EXISTS \"${KB_NATIVE_SDK}/build/engine/Release/kb_engine.lib\")\n"
        "    message(FATAL_ERROR \"C++ script SDK libraries are unavailable at ${KB_NATIVE_SDK}\")\n"
        "endif()\n"
        "add_library(" + target + " SHARED " + target + ".cpp)\n"
        "target_compile_features(" + target + " PRIVATE cxx_std_20)\n"
        "target_include_directories(" + target + " PRIVATE \"${KB_NATIVE_SDK}/sources/engine/include\")\n"
        "target_link_libraries(" + target + " PRIVATE\n"
        "    \"${KB_NATIVE_SDK}/build/engine/Release/kb_engine.lib\"\n"
        "    \"${KB_NATIVE_SDK}/build/third_party/flecs/Release/flecs_static.lib\"\n"
        "    \"${KB_NATIVE_SDK}/build/Release/kb_lua.lib\"\n"
        "    \"${KB_NATIVE_SDK}/build/Release/kb_ufbx.lib\"\n"
        "    user32 xinput)\n"
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
    const std::optional<std::filesystem::path> sdk = FindNativeSdk();
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
            "build = cmake -S \"Source/NativeScripts/" + name + "\" -B \"Saved/NativeScripts/" + name +
                "\" -A x64 && cmake --build \"Saved/NativeScripts/" + name + "\" --config Release --target " + name + "\n";
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
