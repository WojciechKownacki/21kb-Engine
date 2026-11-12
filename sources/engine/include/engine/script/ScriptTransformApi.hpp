#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <optional>

namespace kb::scene {
class Scene;
}

namespace kb::script {

class ScriptRuntimeHost;

class ScriptTransformApi final {
public:
    ScriptTransformApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);

    // The local-position operations behind Transform.GetPosition, SetPosition and Translate. The registered functions
    // and the Lua bindings of those three both call them, so a script reaches the scene without a function-registry
    // call (argument vectors, validation, result vectors) for each use. False when the entity does not exist.
    [[nodiscard]] static bool GetLocalPosition(kb::scene::Scene& scene, kb::scene::SceneEntity entity, float& x, float& y, float& z);
    // An axis left empty keeps its value.
    static bool SetLocalPosition(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::optional<float> x, std::optional<float> y, std::optional<float> z);
    static bool TranslateLocalPosition(kb::scene::Scene& scene, kb::scene::SceneEntity entity, float x, float y, float z);
};

} // namespace kb::script
