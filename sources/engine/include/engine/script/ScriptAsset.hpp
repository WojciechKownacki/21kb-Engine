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

struct NativeBehaviourDescriptor {
    std::string name;
    std::string symbol;
    std::filesystem::path modulePath;
    std::string entryPoint;
    NativeScriptBuildDesc build;
    bool shadowCopy = true;
    std::vector<ScriptApiNameEntry> apiDeclarations;
};

namespace ScriptAssetTypes {

inline constexpr std::string_view LuaScript = "LuaScript";
inline constexpr std::string_view NativeBehaviour = "NativeBehaviour";

} // namespace ScriptAssetTypes

} // namespace kb::script
