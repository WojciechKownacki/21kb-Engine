#pragma once

#include "engine/ecs/ComponentReflection.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneModuleComponentData.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace kb::ecs { class World; }

namespace kb::scene {

class Scene;

// One field of a module component: where it sits in the component, and how the Inspector and scripts name it.
struct SceneModuleComponentField {
    std::string_view name;
    kb::ecs::ComponentFieldType type = kb::ecs::ComponentFieldType::Float32;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string_view label; // Inspector row; the name when empty
    std::string_view tooltip;
};

#define KB_SCENE_MODULE_FIELD(ComponentType, member, fieldType, label, tooltip) \
    ::kb::scene::SceneModuleComponentField{ #member, fieldType, offsetof(ComponentType, member), \
        sizeof(((ComponentType*)0)->member), label, tooltip }

struct SceneModuleComponentDesc {
    std::string_view name; // what scenes, prefabs, scripts and tools call the component
    std::string_view label; // Inspector title; the name when empty
    std::string_view category; // Inspector grouping
    std::span<const SceneModuleComponentField> fields;
};

// One script-visible value of a module component. A vector field gives one property per lane ("offset.x").
struct SceneModuleComponentProperty {
    std::string name;
    std::string label;
    std::string tooltip;
    kb::ecs::ComponentFieldType type = kb::ecs::ComponentFieldType::Float32; // the scalar the property reads
    std::size_t offset = 0;

    [[nodiscard]] bool operator==(const SceneModuleComponentProperty&) const noexcept = default;
};

// What a module component looks like to tools. A description never changes after it is registered and stays valid
// until the process ends, so Inspector and script views can point into it.
struct SceneModuleComponentInfo {
    std::string name;
    std::string label;
    std::string category;
    std::vector<SceneModuleComponentProperty> properties;

    [[nodiscard]] bool operator==(const SceneModuleComponentInfo&) const noexcept = default;
};

// Descriptions of every module component registered so far in this process, by name; the latest registration wins.
class SceneModuleComponentCatalog final {
public:
    SceneModuleComponentCatalog() = delete;

    [[nodiscard]] static const SceneModuleComponentInfo* Find(std::string_view name) noexcept;
    // Every registered name, by name, with its latest description.
    [[nodiscard]] static std::vector<const SceneModuleComponentInfo*> All();
    // Changes whenever a registration adds or changes a description, so a list built from All() knows it is stale.
    [[nodiscard]] static std::size_t Generation() noexcept;
    // Keeps the description of a registration; an identical one is not stored twice.
    [[nodiscard]] static const SceneModuleComponentInfo& Add(SceneModuleComponentInfo info);
};

// The module components of one scene. A module registers its component types when it attaches to a scene and
// unregisters them when it detaches. The component itself is an ordinary ECS component, so queries find it; this
// adds what the ECS does not know: that scenes and prefabs save it, and what to do with its data while no module
// provides the type.
//
// Data without a type is kept, not dropped: a saved component whose module is not loaded is held as opaque bytes
// beside its object and written back unchanged; unregistering a type (a module unload or reload) moves the live
// values there the same way; registering the type again turns them back into live components by field name. A value
// that no longer fits the registered fields stays opaque and is reported by Diagnostics().
class SceneModuleComponents {
public:
    explicit SceneModuleComponents(Scene& scene) noexcept;

    // Registers `T` as a module component of this scene: the ECS learns the type and its fields, and saved values
    // waiting for the name become live components. A name a built-in component uses is refused. T must be trivially
    // copyable, like every ECS component. A module registers through EngineModuleContext::RegisterSceneComponent,
    // which reaches Activate on the host side: module code does not call into the ECS itself.
    template <typename T>
    [[nodiscard]] bool Register(const SceneModuleComponentDesc& desc) {
        static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>, "A module component must be plain data");
        return Activate(desc, typeid(T), sizeof(T), alignof(T));
    }
    [[nodiscard]] bool Activate(const SceneModuleComponentDesc& desc, const std::type_info& type, std::size_t size, std::size_t alignment);
    // Moves the component's values on every object to opaque bytes and removes the type from this scene.
    [[nodiscard]] bool Unregister(std::string_view name);
    [[nodiscard]] bool IsRegistered(std::string_view name) const;

    // The module components on an object, by name: live ones encoded, then the opaque ones.
    [[nodiscard]] std::vector<SceneModuleComponentData> Capture(SceneEntity entity) const;
    // Makes the object's module components exactly `components`.
    void Restore(SceneEntity entity, std::span<const SceneModuleComponentData> components);

    // Adds the component with every field zero, as Add Component does. False when the type is not registered in this
    // scene or the object already has it.
    [[nodiscard]] bool Add(SceneEntity entity, std::string_view name);
    [[nodiscard]] bool Remove(SceneEntity entity, std::string_view name);
    [[nodiscard]] bool Has(SceneEntity entity, std::string_view name) const;
    // The component's value, or null when the object has none; the component keeps its address until it is removed.
    [[nodiscard]] const void* Data(SceneEntity entity, std::string_view name) const;
    [[nodiscard]] void* MutableData(SceneEntity entity, std::string_view name);
    // Tells the ECS that a value written through MutableData changed.
    void MarkModified(SceneEntity entity, std::string_view name);

    [[nodiscard]] std::span<const std::string> Diagnostics() const noexcept;

private:
    [[nodiscard]] static std::vector<kb::ecs::ComponentFieldDesc> ReflectionFields(const SceneModuleComponentDesc& desc);

    Scene& scene_;
};

} // namespace kb::scene
