#pragma once

#include <string>
#include <string_view>

namespace kb::script {

struct LuaScriptAsset {
    std::string source;
};

struct NativeBehaviourDescriptor {
    std::string name;
    std::string symbol;
};

namespace ScriptAssetTypes {

inline constexpr std::string_view LuaScript = "LuaScript";
inline constexpr std::string_view NativeBehaviour = "NativeBehaviour";

} // namespace ScriptAssetTypes

} // namespace kb::script
