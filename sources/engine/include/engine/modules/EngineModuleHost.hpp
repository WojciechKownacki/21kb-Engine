#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/project/ProjectDescriptor.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::ecs {

class World;

} // namespace kb::ecs

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::modules {

// Owns the set of engine modules and drives their lifecycle. Which registered
// candidates become active is decided from the project descriptor
// (ProjectPluginReference entries plus the disableEnginePluginsByDefault default).
// Load order is resolved by loading phase first, then by dependency edges.
//
// This is the single place that replaces hand-wired subsystem setup: the
// application / runtime layer owns one host per project, registers the engine's
// modules, calls Load() once against the ECS world, and AttachScene() for each
// scene it activates.
class EngineModuleHost {
public:
    explicit EngineModuleHost(kb::project::ProjectDescriptor project);
    ~EngineModuleHost();

    EngineModuleHost(const EngineModuleHost&) = delete;
    EngineModuleHost& operator=(const EngineModuleHost&) = delete;
    EngineModuleHost(EngineModuleHost&&) = delete;
    EngineModuleHost& operator=(EngineModuleHost&&) = delete;

    // Register a candidate module. Must be called before Load(); ignored afterwards.
    void Add(std::unique_ptr<IEngineModule> module);

    // Resolve the active set and load order, then OnLoad + OnEnable each active
    // module exactly once. Idempotent: a second call is a no-op.
    void Load(kb::ecs::World& world);

    // Scene-scoped wiring for every active module, in resolved load order.
    void AttachScene(kb::scene::Scene& scene);
    // Reverse load order.
    void DetachScene(kb::scene::Scene& scene);

    // OnDisable + OnUnload every active module in reverse load order. Idempotent.
    void Unload();

    [[nodiscard]] bool IsActive(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t ActiveCount() const noexcept;

    // Human-readable notes accumulated during Load() (missing dependencies,
    // dependency cycles). Empty on a clean resolve.
    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept;

private:
    [[nodiscard]] bool IsEnabledByProject(const EngineModuleMetadata& metadata) const;
    void LoadProjectPluginModules();

    kb::project::ProjectDescriptor project_;
    std::vector<std::unique_ptr<IEngineModule>> candidates_;
    std::vector<IEngineModule*> active_; // resolved load order, non-owning
    std::vector<std::string> diagnostics_;
    bool loaded_ = false;
};

} // namespace kb::modules
