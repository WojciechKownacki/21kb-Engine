#pragma once

#include <cstdint>

namespace kb::scene {
class Scene;
}

namespace kb::script {

class ScriptRuntimeHost;

// What Physics.Raycast reports for one ray; all fields are zero for a ray that hit nothing.
struct ScriptRaycastHit {
    bool hit = false;
    std::uint64_t entity = 0U;
    float distance = 0.0F;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float normalX = 0.0F;
    float normalY = 0.0F;
    float normalZ = 0.0F;
};

class ScriptPhysicsApi final {
public:
    ScriptPhysicsApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);

    // The ray cast behind Physics.Raycast, shared by the registered function and the Lua fast path. Returns null, or the
    // message of the error when the origin, the direction or the distance is not valid (nothing is cast then).
    [[nodiscard]] static const char* Raycast(kb::scene::Scene& scene, float originX, float originY, float originZ, float directionX,
        float directionY, float directionZ, float distance, std::uint32_t layerMask, ScriptRaycastHit& result);
    // A layer mask given as a script integer: a negative one selects no layer.
    [[nodiscard]] static std::uint32_t LayerMaskFromInt(int value) noexcept;
};

} // namespace kb::script
