#include "ecs/world/WorldRegistrySet.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/reflection/ComponentReflectionRegistry.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "ecs/type/TagTypeRegistry.hpp"
#include "ecs/world/WorldEntityCatalog.hpp"

namespace kb::ecs {

WorldRegistrySet::WorldRegistrySet()
    : components_(std::make_unique<ComponentRegistry>())
    , componentReflections_(std::make_unique<ComponentReflectionRegistry>())
    , tags_(std::make_unique<TagTypeRegistry>())
    , relations_(std::make_unique<RelationTypeRegistry>())
    , entities_(std::make_unique<WorldEntityCatalog>()) {}

WorldRegistrySet::~WorldRegistrySet() = default;
WorldRegistrySet::WorldRegistrySet(WorldRegistrySet&&) noexcept = default;
WorldRegistrySet& WorldRegistrySet::operator=(WorldRegistrySet&&) noexcept = default;

ComponentRegistry& WorldRegistrySet::Components() noexcept {
    return *components_;
}

const ComponentRegistry& WorldRegistrySet::Components() const noexcept {
    return *components_;
}

ComponentReflectionRegistry& WorldRegistrySet::ComponentReflections() noexcept {
    return *componentReflections_;
}

const ComponentReflectionRegistry& WorldRegistrySet::ComponentReflections() const noexcept {
    return *componentReflections_;
}

TagTypeRegistry& WorldRegistrySet::Tags() noexcept {
    return *tags_;
}

const TagTypeRegistry& WorldRegistrySet::Tags() const noexcept {
    return *tags_;
}

RelationTypeRegistry& WorldRegistrySet::Relations() noexcept {
    return *relations_;
}

const RelationTypeRegistry& WorldRegistrySet::Relations() const noexcept {
    return *relations_;
}

WorldEntityCatalog& WorldRegistrySet::Entities() noexcept {
    return *entities_;
}

const WorldEntityCatalog& WorldRegistrySet::Entities() const noexcept {
    return *entities_;
}

void WorldRegistrySet::Clear() noexcept {
    components_->Clear();
    componentReflections_->Clear();
    tags_->Clear();
    relations_->Clear();
    entities_->Clear();
}

} // namespace kb::ecs
