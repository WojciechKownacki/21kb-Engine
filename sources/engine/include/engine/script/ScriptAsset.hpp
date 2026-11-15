#pragma once

#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/NativeScriptBuildPipeline.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

struct LuaScriptAsset {
    std::string source;
    std::vector<std::string> imports;
    std::vector<ScriptApiPin> exposedVariables;
    std::vector<ScriptValue> exposedVariableDefaults;
    std::vector<std::uint8_t> exposedVariableHasDefault;
};

// What the Inspector shows with a field: `expose = speed Float = 3 unit=m/s min=0 max=20 tip="Metres per second"`.
// A range clamps what is typed and fills the row like a slider; the unit follows the value; the tip is the hover help.
struct NativeFieldMetadata {
    std::string unit;
    std::string tooltip;
    bool hasMinimum = false;
    bool hasMaximum = false;
    double minimum = 0.0;
    double maximum = 0.0;
};

struct NativeBehaviourDescriptor {
    std::string name;
    std::string symbol;
    std::filesystem::path modulePath;
    // Key of a module linked into the host instead of a library (Android, WebAssembly).
    std::string linkedModule;
    std::filesystem::path sourcePath;
    std::string entryPoint;
    NativeScriptBuildDesc build;
    bool shadowCopy = true;
    std::vector<ScriptApiNameEntry> apiDeclarations;
    // Fields the script shows in the Inspector, one `expose = name Type = default` line each. They are
    // the same per-object values a Lua script's `@expose` variables are (BehaviourVariableOverride).
    std::vector<ScriptApiPin> exposedVariables;
    std::vector<ScriptValue> exposedVariableDefaults;
    std::vector<NativeFieldMetadata> exposedVariableMetadata;
};

namespace ScriptAssetTypes {

inline constexpr std::string_view LuaScript = "LuaScript";
inline constexpr std::string_view NativeBehaviour = "NativeBehaviour";

} // namespace ScriptAssetTypes

} // namespace kb::script
