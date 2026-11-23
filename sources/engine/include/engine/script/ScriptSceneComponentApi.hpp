#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptValue.hpp"

#include <span>
#include <string>
#include <string_view>

namespace kb::script {

struct ScriptSceneComponentPropertyDesc {
    std::string_view name;
    ScriptValueType type = ScriptValueType::Void;
    bool writable = true;
    // An Int enum's value names in value order; tools accept a name in place of the number.
    std::span<const std::string_view> enumNames{};
};

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
    [[nodiscard]] static std::span<const ScriptSceneComponentPropertyDesc> ComponentProperties(std::string_view componentName) noexcept;
    // The value a property has in a new component of a module's type, from the defaults it registered.
    [[nodiscard]] static ScriptSceneComponentPropertyResult DefaultProperty(std::string_view componentName, std::string_view propertyName);
    [[nodiscard]] static bool HasComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName) noexcept;
    // Empty when `componentName` is a component scripts can use and `propertyName` one of its properties. Otherwise a sentence
    // that says which of the two is wrong, with the nearest valid name, for the error a script gets from a call that did nothing.
    [[nodiscard]] static std::string UnknownNameMessage(std::string_view componentName, std::string_view propertyName);
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
    // The Behaviour component of one of the scripts an entity runs: the one an index names (0 is the first). Component "Behaviour"
    // alone is the first script's; a script that asks for its own through `self` gets its place here.
    [[nodiscard]] static ScriptSceneComponentPropertyResult GetBehaviourProperty(
        kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::size_t index, std::string_view propertyName);
    [[nodiscard]] static ScriptSceneComponentMutationResult SetBehaviourProperty(
        kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::size_t index, std::string_view propertyName, const ScriptValue& value);
};

} // namespace kb::script
