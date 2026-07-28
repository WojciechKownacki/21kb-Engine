#pragma once

#include "engine/ecs/WorldConfig.hpp"
#include "engine/scene/SceneMode.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::input {

class InputSubsystem;
struct LocalUserId;

} // namespace kb::input

namespace kb::modules {

class EngineModuleHost;
class IEngineModule;

} // namespace kb::modules

namespace kb::project {

struct ProjectDescriptor;

} // namespace kb::project

namespace kb::save {

class SaveGame;

} // namespace kb::save

namespace kb::scene {

class SceneAssets;
class SceneAnimatorQueries;
class SceneAnimators;
class SceneComponentQueries;
class SceneComponents;
class SceneEntities;
class SceneEntityQueries;
class SceneHierarchyAccess;
class SceneHierarchyQueries;
class SceneHistory;
class SceneLoadedContent;
class SceneLoadedContentQueries;
class SceneMaterialInstanceQueries;
class SceneMaterialInstances;
class SceneParticleSystemQueries;
class SceneParticleSystems;
class ScenePrefabs;
class SceneRuntime;
class SceneRuntimeQueries;
class SceneAccess;
class SceneState;
class SceneTasks;
class SceneTimers;
class SceneTransformQueries;
class SceneTransforms;

class Scene {
public:
    Scene();
    explicit Scene(kb::ecs::WorldConfig worldConfig);
    explicit Scene(SceneMode mode);
    // Construct with an explicit project descriptor so the engine module host can
    // honour its enabled/disabled module set. The default constructor delegates here
    // with a default descriptor (every built-in engine module enabled).
    explicit Scene(kb::project::ProjectDescriptor descriptor);
    Scene(kb::project::ProjectDescriptor descriptor, SceneMode mode);
    explicit Scene(
        kb::project::ProjectDescriptor descriptor,
        std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules,
        SceneMode mode = SceneMode::Runtime);
    explicit Scene(
        kb::project::ProjectDescriptor descriptor,
        std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules,
        kb::ecs::WorldConfig worldConfig,
        SceneMode mode = SceneMode::Runtime);
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] SceneEntities Entities() noexcept;
    [[nodiscard]] SceneEntityQueries Entities() const noexcept;
    [[nodiscard]] SceneTransforms Transforms() noexcept;
    [[nodiscard]] SceneTransformQueries Transforms() const noexcept;
    [[nodiscard]] SceneComponents Components() noexcept;
    [[nodiscard]] SceneComponentQueries Components() const noexcept;
    [[nodiscard]] SceneHierarchyAccess Hierarchy() noexcept;
    [[nodiscard]] SceneHierarchyQueries Hierarchy() const noexcept;
    [[nodiscard]] SceneHistory History() noexcept;
    [[nodiscard]] SceneAssets Assets() noexcept;
    [[nodiscard]] SceneAssets Assets() const noexcept;
    [[nodiscard]] ScenePrefabs Prefabs() noexcept;
    [[nodiscard]] ScenePrefabs Prefabs() const noexcept;
    [[nodiscard]] SceneRuntime Runtime() noexcept;
    [[nodiscard]] SceneRuntimeQueries Runtime() const noexcept;
    [[nodiscard]] SceneLoadedContent LoadedContent() noexcept;
    [[nodiscard]] SceneLoadedContentQueries LoadedContent() const noexcept;
    [[nodiscard]] SceneTimers Timers() noexcept;
    [[nodiscard]] SceneTasks Tasks() noexcept;
    [[nodiscard]] SceneMaterialInstances MaterialInstances() noexcept;
    [[nodiscard]] SceneMaterialInstanceQueries MaterialInstances() const noexcept;
    [[nodiscard]] SceneParticleSystems Particles() noexcept;
    [[nodiscard]] SceneParticleSystemQueries Particles() const noexcept;
    [[nodiscard]] SceneAnimators Animators() noexcept;
    [[nodiscard]] SceneAnimatorQueries Animators() const noexcept;
    // LIB-162: the scene's ambient SaveGame buffer the script Save.* surface
    // reads/mutates and serializes to disk.
    [[nodiscard]] kb::save::SaveGame& AmbientSave() noexcept;
    [[nodiscard]] const kb::save::SaveGame& AmbientSave() const noexcept;
    // LIB-163: a SEPARATE ambient buffer for user settings — distinct from
    // AmbientSave so game progress and preferences never mix, and serialized
    // under the UserSettings save domain (a settings file can never be loaded
    // as a save game or vice versa).
    [[nodiscard]] kb::save::SaveGame& AmbientSettings() noexcept;
    [[nodiscard]] const kb::save::SaveGame& AmbientSettings() const noexcept;
    void ReloadModules();
    // Read-only module lifecycle state for production hosts (players/tools) that
    // must reject a configured plugin failure instead of silently continuing
    // without the requested backend.
    [[nodiscard]] bool IsModuleActive(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t ActiveModuleCount() const noexcept;
    [[nodiscard]] std::span<const std::string> ModuleDiagnostics() const noexcept;
    [[nodiscard]] kb::input::InputSubsystem& Input() noexcept;
    [[nodiscard]] const kb::input::InputSubsystem& Input() const noexcept;
    // Independent input state for a specific local user (LIB-115). Lazily creates
    // and resolver-wires the user's InputSubsystem on first access; kPrimaryLocalUser
    // always returns the same subsystem as the no-argument Input() above.
    [[nodiscard]] kb::input::InputSubsystem& Input(kb::input::LocalUserId user) noexcept;
    // Read-only lookup that does NOT create a subsystem: nullptr if `user` has
    // never been accessed via the non-const Input(LocalUserId) overload.
    [[nodiscard]] const kb::input::InputSubsystem* TryGetInput(kb::input::LocalUserId user) const noexcept;
    // Evaluates the primary local user's InputSubsystem, then every other local
    // user's that has been created, all reading the primary's shared physical
    // device state (see LocalUserId's doc comment for why device state is shared).
    void EvaluateAllLocalUserInput(float deltaSeconds);
    // Clears mapping contexts on the primary AND every created secondary local
    // user's InputSubsystem.
    void ClearAllLocalUserInputMappingContexts() noexcept;
    [[nodiscard]] std::uint64_t Id() const noexcept;
    [[nodiscard]] SceneMode Mode() const noexcept;
    [[nodiscard]] bool IsPrefabPrivate() const noexcept;

private:
    friend class SceneAccess;

    std::unique_ptr<SceneState> state_;
    std::unique_ptr<kb::modules::EngineModuleHost> moduleHost_;
    std::uint64_t id_ = 0;
};

} // namespace kb::scene
