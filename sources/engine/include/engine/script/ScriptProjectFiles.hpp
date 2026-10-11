#pragma once

#include "engine/script/NativeScriptBuildPipeline.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::script {

struct NativeBehaviourDescriptor;

// What a new script is written in. Lua: one .lua file. C++: a .native descriptor among the
// project's assets, with its source and CMake project in Source/NativeScripts/<Name> and the DLL it
// builds in Binaries/NativeScripts.
enum class ScriptLanguage { Lua, Cpp };

struct CreatedScript {
    // The asset a scene attaches: the .lua file, or the C++ script's .native descriptor.
    std::filesystem::path asset;
    // The file the user edits: the .lua file again, or the C++ source.
    std::filesystem::path source;
    // Why nothing was written, or empty.
    std::string error;
};

// New scripts of a project, one set of rules for the editor's Create menu and kb_cli script-new:
// the templates, the names, where the files go, and that nothing existing is overwritten.
class ScriptProjectFiles final {
public:
    ScriptProjectFiles() = delete;

    // Why `name` cannot name a new script, or empty. Every name is a file name (AssetFileNameError);
    // a C++ name is also the class, the CMake target and the DLL, so it is a C++ identifier in
    // ASCII letters, digits and '_' that is not a C++ keyword.
    [[nodiscard]] static std::string NameError(ScriptLanguage language, std::string_view name);
    // NewScript, NewScript1, NewScript2...: the first name no file of the new script would collide with.
    [[nodiscard]] static std::string FreeName(ScriptLanguage language, const std::filesystem::path& projectRoot,
        const std::filesystem::path& folder);
    // Writes the script `name` into `folder` (an Assets folder on disk, made when missing) of the
    // project at `projectRoot`. A name some file of the new script already has is refused: nothing
    // is overwritten, and a C++ script that cannot be finished leaves none of its files behind.
    [[nodiscard]] static CreatedScript Create(ScriptLanguage language, const std::filesystem::path& projectRoot,
        const std::filesystem::path& folder, std::string_view name);
    // The build of the C++ script whose descriptor is `descriptorFile`, run from the folder its
    // build_working_directory names (relative to the descriptor), else from the project root - the
    // same place the editor's Play and the cooker build it from.
    [[nodiscard]] static NativeScriptBuildDesc BuildOf(const NativeBehaviourDescriptor& descriptor,
        const std::filesystem::path& descriptorFile, const std::filesystem::path& projectRoot);
    // The DLL that build writes, as the descriptor names it relative to its own folder.
    [[nodiscard]] static std::filesystem::path ModuleOf(const NativeBehaviourDescriptor& descriptor,
        const std::filesystem::path& descriptorFile);
};

} // namespace kb::script
