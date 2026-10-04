#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace kb::ecs {

class ComponentRegistry;
class ComponentReflectionRegistry;
class RelationTypeRegistry;
class TagTypeRegistry;
class WorldEntityCatalog;

class WorldRegistrySet {
public:
    WorldRegistrySet();
    ~WorldRegistrySet();

    WorldRegistrySet(const WorldRegistrySet&) = delete;
    WorldRegistrySet& operator=(const WorldRegistrySet&) = delete;
    WorldRegistrySet(WorldRegistrySet&&) noexcept;
    WorldRegistrySet& operator=(WorldRegistrySet&&) noexcept;

    [[nodiscard]] ComponentRegistry& Components() noexcept;
    [[nodiscard]] const ComponentRegistry& Components() const noexcept;
    [[nodiscard]] ComponentReflectionRegistry& ComponentReflections() noexcept;
    [[nodiscard]] const ComponentReflectionRegistry& ComponentReflections() const noexcept;
    [[nodiscard]] TagTypeRegistry& Tags() noexcept;
    [[nodiscard]] const TagTypeRegistry& Tags() const noexcept;
    [[nodiscard]] RelationTypeRegistry& Relations() noexcept;
    [[nodiscard]] const RelationTypeRegistry& Relations() const noexcept;
    [[nodiscard]] WorldEntityCatalog& Entities() noexcept;
    [[nodiscard]] const WorldEntityCatalog& Entities() const noexcept;

    [[nodiscard]] bool HasComponentObserver(ComponentId componentId) const noexcept;
    void RetainComponentObserver(ComponentId componentId);
    void ReleaseComponentObserver(ComponentId componentId) noexcept;

    void Clear() noexcept;

private:
    std::unique_ptr<ComponentRegistry> components_;
    std::unique_ptr<ComponentReflectionRegistry> componentReflections_;
    std::unique_ptr<TagTypeRegistry> tags_;
    std::unique_ptr<RelationTypeRegistry> relations_;
    std::unique_ptr<WorldEntityCatalog> entities_;
    // Owned behind World::registries_ so World's public object layout stays
    // unchanged and observer contexts remain valid when a World is moved.
    std::unordered_map<ComponentId, std::size_t> componentObservers_;
};

} // namespace kb::ecs
