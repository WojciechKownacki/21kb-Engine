#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/scene/SceneModuleComponents.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace kb::assets { class AssetManager; class IAssetLoader; }

namespace kb::ecs {

class World;

} // namespace kb::ecs

namespace kb::scene { class Scene; }

namespace kb::project {

struct ProjectDescriptor;

} // namespace kb::project

namespace kb::modules {

// Handed to a module during OnLoad so it can register ECS components against the
// world and inspect the active project configuration. Holds non-owning references;
// the host guarantees both outlive every module it drives.
class EngineModuleContext {
public:
    using RegisterLoaderFunction = bool (*)(kb::assets::AssetManager&, std::unique_ptr<kb::assets::IAssetLoader>, std::shared_ptr<void>);
    // The host's side of module component registration. Module code does not call into the ECS itself, since the ECS
    // state belongs to the host's copy of the engine, so the work is done by these host functions.
    struct ComponentHostCalls {
        bool (*registerComponent)(kb::scene::Scene&, const kb::scene::SceneModuleComponentDesc&, const std::type_info&, std::size_t, std::size_t, const void*);
        bool (*unregisterComponent)(kb::scene::Scene&, std::string_view);
    };
    EngineModuleContext(kb::ecs::World& world, const kb::project::ProjectDescriptor& project,
        std::shared_ptr<void> libraryLifetime = {}, RegisterLoaderFunction registerLoader = nullptr,
        const ComponentHostCalls* componentHost = nullptr) noexcept
        : world_(&world), project_(&project), libraryLifetime_(std::move(libraryLifetime)), registerLoader_(registerLoader),
          componentHost_(componentHost) {}

    // Copy the context or its registration function for use during scene attach.
    // Registered loaders/payloads own the library independently of the module instance.
    [[nodiscard]] bool RegisterAssetLoader(kb::assets::AssetManager& manager,
        std::unique_ptr<kb::assets::IAssetLoader> loader) const;

    // Registers a component type for `scene`: scenes and prefabs save it, scripts and tools see its fields, and a scene
    // that holds it while no module provides the type keeps its data. `defaults` is the value a new component gets and
    // what the Inspector marks changes against; the type's own constructor gives it by default. Call it when the scene attaches, and call
    // UnregisterSceneComponent when it detaches. Like loaders, the registration is made by host code.
    template <typename T>
    [[nodiscard]] bool RegisterSceneComponent(kb::scene::Scene& scene, const kb::scene::SceneModuleComponentDesc& desc, const T& defaults = T{}) const {
        static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>, "A module component must be plain data");
        return componentHost_ != nullptr ? componentHost_->registerComponent(scene, desc, typeid(T), sizeof(T), alignof(T), &defaults)
                                         : kb::scene::SceneModuleComponents{ scene }.Register<T>(desc, defaults);
    }
    // Removes the type from the scene; the values the objects hold stay as saved data until it is registered again.
    [[nodiscard]] bool UnregisterSceneComponent(kb::scene::Scene& scene, std::string_view name) const {
        return componentHost_ != nullptr ? componentHost_->unregisterComponent(scene, name)
                                         : kb::scene::SceneModuleComponents{ scene }.Unregister(name);
    }

    [[nodiscard]] kb::ecs::World& EcsWorld() const noexcept {
        return *world_;
    }

    [[nodiscard]] const kb::project::ProjectDescriptor& Project() const noexcept {
        return *project_;
    }

private:
    kb::ecs::World* world_;
    const kb::project::ProjectDescriptor* project_;
    std::shared_ptr<void> libraryLifetime_;
    RegisterLoaderFunction registerLoader_ = nullptr;
    const ComponentHostCalls* componentHost_ = nullptr;
};

} // namespace kb::modules
