#pragma once

#include <optional>
#include <string>

struct lua_State;

namespace kb::script {

// Installs the stable Lua Task table. Wait functions start the matching
// SceneTask and keep the current Lua generator suspended until that task
// reaches a terminal state.
class PucLuaTaskApi final {
public:
    [[nodiscard]] static std::optional<std::string> Attach(lua_State* state, int environmentIndex);
};

} // namespace kb::script
