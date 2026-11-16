#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "engine/scene/SceneObject.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// Prefab overrides of the components that modules register (SceneModuleComponents). They are a list of property
// overrides rather than flag bits, so any number of module components can be overridden:
//   moduleComponents.<name>          the instance has a component the prefab lacks (value: its saved bytes in hex) or
//                                    lacks one the prefab has (value: "null")
//   moduleComponents.<name>/<field>  a field of a component both have (value: the field's saved bytes in hex)
// The name never holds a slash, so the path splits at the first one.
class ScenePrefabModuleComponentOverrides final {
public:
    ScenePrefabModuleComponentOverrides() = delete;

    [[nodiscard]] static bool Handles(std::string_view propertyPath) noexcept;
    // The names of the module components of `entity` that differ from the prefab node.
    [[nodiscard]] static std::vector<std::string> Differing(Scene& scene, SceneEntity entity, const ScenePrefabNodeComponents& expected);
    // Adds one property override per difference.
    static void Append(Scene& scene, SceneEntity entity, const ScenePrefabNodeComponents& expected,
        ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object);
    // Applies a stored override to a node. An override of a component or field the node no longer has is stale and
    // changes nothing.
    [[nodiscard]] static bool ApplyToNode(ScenePrefabNodeDesc& node, const ScenePrefabPropertyOverride& property);
    // Puts the component the path names back to what the node holds.
    static void Revert(Scene& scene, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath);
    // Copies the component the path names from the object into the node.
    static void CopyToNode(Scene& scene, SceneObject object, ScenePrefabNodeDesc& node, std::string_view propertyPath);
    // The value an override of the path would record for the object; false when the object has no such component.
    [[nodiscard]] static bool Value(Scene& scene, SceneObject object, std::string_view propertyPath, std::string& value);
};

} // namespace kb::scene
