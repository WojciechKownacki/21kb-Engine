#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Physics2D.*: velocity, forces, ray and overlap queries and character movement of the 2D physics (Box2D) of a scene.
class ScriptPhysics2DApi final {
public:
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
