#pragma once

#include <memory>
#include <utility>

namespace kb::assets { class AssetManager; class IAssetLoader; }

namespace kb::ecs {

class World;

} // namespace kb::ecs

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
    EngineModuleContext(kb::ecs::World& world, const kb::project::ProjectDescriptor& project,
        std::shared_ptr<void> libraryLifetime = {}, RegisterLoaderFunction registerLoader = nullptr) noexcept
        : world_(&world), project_(&project), libraryLifetime_(std::move(libraryLifetime)), registerLoader_(registerLoader) {}

    // Copy the context or its registration function for use during scene attach.
    // Registered loaders/payloads own the library independently of the module instance.
    [[nodiscard]] bool RegisterAssetLoader(kb::assets::AssetManager& manager,
        std::unique_ptr<kb::assets::IAssetLoader> loader) const;

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
};

} // namespace kb::modules
