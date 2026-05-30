#pragma once

#include <memory>

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

    void Clear() noexcept;

private:
    std::unique_ptr<ComponentRegistry> components_;
    std::unique_ptr<ComponentReflectionRegistry> componentReflections_;
    std::unique_ptr<TagTypeRegistry> tags_;
    std::unique_ptr<RelationTypeRegistry> relations_;
    std::unique_ptr<WorldEntityCatalog> entities_;
};

} // namespace kb::ecs
