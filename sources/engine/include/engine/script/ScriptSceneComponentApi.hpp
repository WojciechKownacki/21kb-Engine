#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptValue.hpp"

#include <span>
#include <string>
#include <string_view>

namespace kb::script {

struct ScriptSceneComponentPropertyResult {
    bool succeeded = false;
    ScriptValue value{};
    std::string error;
};

struct ScriptSceneComponentMutationResult {
    bool succeeded = false;
    std::string error;
};

class ScriptSceneComponentApi final {
public:
    ScriptSceneComponentApi() = delete;

    [[nodiscard]] static std::span<const std::string_view> ComponentNames() noexcept;
    [[nodiscard]] static bool HasComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName) noexcept;
    [[nodiscard]] static ScriptSceneComponentPropertyResult GetProperty(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::string_view componentName,
        std::string_view propertyName);
    [[nodiscard]] static ScriptSceneComponentMutationResult SetProperty(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::string_view componentName,
        std::string_view propertyName,
        const ScriptValue& value);
};

} // namespace kb::script
