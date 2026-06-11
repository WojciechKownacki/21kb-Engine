#pragma once

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
    EngineModuleContext(kb::ecs::World& world, const kb::project::ProjectDescriptor& project) noexcept
        : world_(&world), project_(&project) {}

    [[nodiscard]] kb::ecs::World& EcsWorld() const noexcept {
        return *world_;
    }

    [[nodiscard]] const kb::project::ProjectDescriptor& Project() const noexcept {
        return *project_;
    }

private:
    kb::ecs::World* world_;
    const kb::project::ProjectDescriptor* project_;
};

} // namespace kb::modules
