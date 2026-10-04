#include "ecs/world/WorldRegistrySet.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/reflection/ComponentReflectionRegistry.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "ecs/type/TagTypeRegistry.hpp"
#include "ecs/world/WorldEntityCatalog.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <flecs.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

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

bool WorldRegistrySet::HasComponentObserver(ComponentId componentId) const noexcept {
    return componentObservers_.find(componentId) != componentObservers_.end();
}

void WorldRegistrySet::RetainComponentObserver(ComponentId componentId) {
    ++componentObservers_[componentId];
}

void WorldRegistrySet::ReleaseComponentObserver(ComponentId componentId) noexcept {
    const auto found = componentObservers_.find(componentId);
    if (found != componentObservers_.end() && --found->second == 0U) {
        componentObservers_.erase(found);
    }
}

void WorldRegistrySet::InitializeMirroredEntityCleanup(ecs_world_t* world, const NativeArchetypeStorage* nativeStorage) {
    if (world == nullptr || cleanupObserverId_ != 0U) return;
    nativeStorage_ = nativeStorage;
    // General entity allocation leaves the public low component-ID cursor
    // untouched. Initialize before any native entity can occupy these indices.
    cleanupEventId_ = ecs_new(world);
    if (cleanupEventId_ < FLECS_HI_COMPONENT_ID) {
        throw std::runtime_error("Failed to allocate mirrored entity cleanup event");
    }
    ecs_component_desc_t component{};
    component.entity = cleanupEventId_;
    component.type.size = static_cast<ecs_size_t>(sizeof(Entity::IdType));
    component.type.alignment = static_cast<ecs_size_t>(alignof(Entity::IdType));
    if (ecs_component_init(world, &component) == 0U) {
        throw std::runtime_error("Failed to register mirrored entity cleanup event");
    }
    cleanupHelperId_ = ecs_new(world);
    cleanupObserverId_ = ecs_new(world);
    if (cleanupHelperId_ == 0U || cleanupObserverId_ == 0U) {
        throw std::runtime_error("Failed to allocate mirrored entity cleanup receiver");
    }
    ecs_add_id(world, cleanupHelperId_, cleanupEventId_);
    ecs_observer_desc_t observer{};
    observer.entity = cleanupObserverId_;
    observer.query.terms[0].id = cleanupEventId_;
    observer.events[0] = cleanupEventId_;
    observer.callback = &WorldRegistrySet::DispatchMirroredEntityCleanup;
    observer.ctx = this;
    if (ecs_observer_init(world, &observer) == 0U) {
        throw std::runtime_error("Failed to initialize mirrored entity cleanup receiver");
    }
}

Entity::IdType WorldRegistrySet::CreateObserverEntity(ecs_world_t* world) const {
    if (world == nullptr) return 0U;
    Entity::IdType id = static_cast<Entity::IdType>(ecs_get_max_id(world)) + 1U;
    id = std::max(id, static_cast<Entity::IdType>(FLECS_HI_COMPONENT_ID));
    for (;;) {
        if (id > static_cast<Entity::IdType>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("Backend observer entity capacity exceeded");
        }
        if (ecs_get_alive(world, id) == 0U
            && (nativeStorage_ == nullptr || !nativeStorage_->ResolveAliveEntity(id).IsValid())) {
            ecs_make_alive(world, id);
            return id;
        }
        ++id;
    }
}

void WorldRegistrySet::DispatchMirroredEntityCleanup(ecs_iter_t* iterator) noexcept {
    if (iterator == nullptr || iterator->ctx == nullptr || iterator->param == nullptr) return;
    auto& registry = *static_cast<WorldRegistrySet*>(iterator->ctx);
    const Entity entity{ *static_cast<const Entity::IdType*>(iterator->param) };
    // A reentrant restore of the exact logical ID owns its new binding. A
    // normal new generation is also protected by the directory identity check.
    if (registry.nativeStorage_ == nullptr || !registry.nativeStorage_->IsAlive(entity)) {
        registry.ForgetMirroredEntity(entity);
    }
}

void WorldRegistrySet::BindMirroredEntities(ecs_world_t* world, std::span<const Entity> entities) {
    if (world == nullptr || entities.empty()) return;

    constexpr std::size_t kMaxDenseGap = 4096U;
    std::size_t denseSize = mirroredEntityIds_.size();
    std::size_t sparseCount = 0U;
    for (Entity entity : entities) {
        if (!entity.IsValid()) throw std::invalid_argument("Cannot bind an invalid mirrored entity");
        const Entity::IdType backendId = ecs_strip_generation(entity.Id());
        if (ecs_get_alive(world, backendId) != 0U) {
            throw std::invalid_argument("Cannot bind a mirrored entity over an occupied backend index");
        }
        const std::uint32_t index = GeneratedEntityIndex(entity);
        if (index != kInvalidGeneratedEntityIndex
            && static_cast<std::size_t>(index) <= denseSize + kMaxDenseGap) {
            denseSize = std::max(denseSize, static_cast<std::size_t>(index) + 1U);
        } else {
            ++sparseCount;
        }
    }

    // All throwing preparation precedes changing an existing owner. Empty
    // sparse slots left by a failed preparation are not ownership certificates.
    mirroredEntityIds_.resize(denseSize, 0U);
    sparseMirroredEntityIds_.reserve(sparseMirroredEntityIds_.size() + sparseCount);
    for (Entity entity : entities) {
        const std::uint32_t index = GeneratedEntityIndex(entity);
        if (index >= mirroredEntityIds_.size()) {
            sparseMirroredEntityIds_.try_emplace(ecs_strip_generation(entity.Id()), 0U);
        }
    }
    for (Entity entity : entities) {
        const std::uint32_t index = GeneratedEntityIndex(entity);
        const Entity::IdType backendId = ecs_strip_generation(entity.Id());
        if (index < mirroredEntityIds_.size()) {
            mirroredEntityIds_[index] = entity.Id();
            sparseMirroredEntityIds_.erase(backendId);
        } else {
            sparseMirroredEntityIds_.find(backendId)->second = entity.Id();
        }
    }
}

Entity::IdType WorldRegistrySet::MirroredEntityId(Entity::IdType backendEntityId) const noexcept {
    // Raw backend lifetimes can have a different generation from a retired
    // mirror. Never translate merely by the shared low 32 bits.
    if (backendEntityId == ecs_strip_generation(backendEntityId)) {
        const std::uint32_t index = GeneratedEntityIndex(Entity{ backendEntityId });
        if (index < mirroredEntityIds_.size() && mirroredEntityIds_[index] != 0U) {
            return mirroredEntityIds_[index];
        }
        const auto found = sparseMirroredEntityIds_.find(backendEntityId);
        if (found != sparseMirroredEntityIds_.end() && found->second != 0U) {
            return found->second;
        }
    }
    return 0U;
}

Entity WorldRegistrySet::ResolveObserverEntity(Entity::IdType backendEntityId) const noexcept {
    const Entity::IdType logicalId = MirroredEntityId(backendEntityId);
    return Entity{ logicalId != 0U ? logicalId : backendEntityId };
}

bool WorldRegistrySet::OwnsMirroredEntity(Entity entity) const noexcept {
    return entity.IsValid() && MirroredEntityId(ecs_strip_generation(entity.Id())) == entity.Id();
}

void WorldRegistrySet::CompleteMirroredEntityDeletion(ecs_world_t* world, Entity entity) noexcept {
    if (world == nullptr || !entity.IsValid()) return;
    const Entity::IdType backendId = ecs_strip_generation(entity.Id());
    if (MirroredEntityId(backendId) != entity.Id()) return;
    if (ecs_get_alive(world, backendId) != backendId) {
        if (nativeStorage_ == nullptr || !nativeStorage_->IsAlive(entity)) ForgetMirroredEntity(entity);
        return;
    }
    // The backend row is still alive because deletion was deferred. Target
    // the persistent helper, so the event survives deletion of the owner.
    // Flecs copies this eight-byte payload into its deferred command storage.
    ecs_id_t id = cleanupEventId_;
    ecs_type_t ids{ &id, 1 };
    const Entity::IdType logicalId = entity.Id();
    ecs_event_desc_t event{};
    event.event = cleanupEventId_;
    event.ids = &ids;
    event.entity = cleanupHelperId_;
    event.const_param = &logicalId;
    ecs_enqueue(world, &event);
}

void WorldRegistrySet::ForgetMirroredEntity(Entity entity) noexcept {
    const Entity::IdType backendId = ecs_strip_generation(entity.Id());
    const std::uint32_t index = GeneratedEntityIndex(entity);
    if (index < mirroredEntityIds_.size() && mirroredEntityIds_[index] == entity.Id()) {
        mirroredEntityIds_[index] = 0U;
    }
    const auto found = sparseMirroredEntityIds_.find(backendId);
    if (found != sparseMirroredEntityIds_.end() && found->second == entity.Id()) {
        sparseMirroredEntityIds_.erase(found);
    }
}

void WorldRegistrySet::Clear() noexcept {
    componentObservers_.clear();
    mirroredEntityIds_.clear();
    sparseMirroredEntityIds_.clear();
    nativeStorage_ = nullptr;
    cleanupEventId_ = 0U;
    cleanupHelperId_ = 0U;
    cleanupObserverId_ = 0U;
    components_->Clear();
    componentReflections_->Clear();
    tags_->Clear();
    relations_->Clear();
    entities_->Clear();
}

} // namespace kb::ecs
