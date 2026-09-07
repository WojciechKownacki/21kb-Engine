#pragma once

#include "engine/script/ScriptSceneComponentApi.hpp"

#include <span>
#include <string_view>

namespace kb::script {

class ScriptUIComponentApi final {
  public:
    ScriptUIComponentApi() = delete;

    [[nodiscard]] static std::span<const std::string_view> ComponentNames() noexcept;
    [[nodiscard]] static bool IsComponent(std::string_view componentName) noexcept;
    [[nodiscard]] static std::span<const ScriptSceneComponentPropertyDesc>
    ComponentProperties(std::string_view componentName) noexcept;
    [[nodiscard]] static bool HasComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
                                           std::string_view componentName) noexcept;
    [[nodiscard]] static ScriptSceneComponentPropertyResult GetProperty(kb::scene::Scene& scene,
                                                                        kb::scene::SceneEntity entity,
                                                                        std::string_view componentName,
                                                                        std::string_view propertyName);
    [[nodiscard]] static ScriptSceneComponentMutationResult
    SetProperty(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName,
                std::string_view propertyName, const ScriptValue& value);
};

} // namespace kb::script
