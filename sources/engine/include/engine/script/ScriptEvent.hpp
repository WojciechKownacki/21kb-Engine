#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptValue.hpp"

#include <string>
#include <vector>

namespace kb::script {

struct ScriptEventArgument {
    std::string name;
    ScriptValue value;
};

struct ScriptEvent {
    std::string name;
    kb::scene::SceneEntity sender{};
    kb::assets::AssetId senderAsset{};
    std::vector<ScriptEventArgument> arguments;
};

} // namespace kb::script
