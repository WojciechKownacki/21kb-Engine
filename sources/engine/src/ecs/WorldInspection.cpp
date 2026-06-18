#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/inspection/EditorWorldInspector.hpp"
#include "ecs/inspection/EntityInspector.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

namespace kb::ecs {

namespace {

[[nodiscard]] double Percent(std::size_t numerator, std::size_t denominator) noexcept {
    return denominator == 0U ? 0.0 : (static_cast<double>(numerator) * 100.0) / static_cast<double>(denominator);
}

[[nodiscard]] EntityComponentInspection InspectComponent(const ComponentRegistry& components, ComponentId componentId) {
    const ComponentTypeInfo* typeInfo = components.FindInfo(componentId);
    if (typeInfo == nullptr) {
        return EntityComponentInspection{
            .id = componentId,
        };
    }

    return EntityComponentInspection{
        .id = typeInfo->id,
        .name = typeInfo->name,
        .size = typeInfo->size,
        .alignment = typeInfo->alignment,
    };
}

} // namespace

EntityInspection World::InspectEntity(Entity entity) const {
    if (registries_ == nullptr) {
        return {};
    }
    return EntityInspector::Inspect(world_, entity, Parent(entity), registries_->Components().Types());
}

std::vector<WorldArchetypeInspection> World::InspectArchetypes() const {
    if (registries_ == nullptr) {
        return {};
    }

    const NativeEcsStorageStats storageStats = NativeStorageStats();
    std::vector<WorldArchetypeInspection> inspections;
    inspections.reserve(storageStats.archetypeCounters.size());

    const ComponentRegistry& components = registries_->Components();
    for (const NativeEcsArchetypeMemoryCounters& archetypeStats : storageStats.archetypeCounters) {
        WorldArchetypeInspection& inspection = inspections.emplace_back();
        inspection.archetypeIndex = archetypeStats.archetypeIndex;
        inspection.liveEntities = archetypeStats.liveEntities;
        inspection.chunks = archetypeStats.chunks;
        inspection.capacity = archetypeStats.capacity;
        inspection.payloadBytes = archetypeStats.payloadBytes;
        inspection.usedBytes = archetypeStats.usedBytes;
        inspection.wastedBytes = archetypeStats.wastedBytes;
        inspection.version = archetypeStats.version;
        inspection.occupancyPercent = Percent(archetypeStats.liveEntities, archetypeStats.capacity);
        inspection.wastedPercent = Percent(archetypeStats.wastedBytes, archetypeStats.payloadBytes);

        inspection.components.reserve(archetypeStats.componentIds.size());
        for (const ComponentId componentId : archetypeStats.componentIds) {
            inspection.components.push_back(InspectComponent(components, componentId));
        }

        inspection.chunkInspections.reserve(archetypeStats.chunkCounters.size());
        for (const NativeEcsChunkMemoryCounters& chunkStats : archetypeStats.chunkCounters) {
            inspection.chunkInspections.push_back(WorldArchetypeChunkInspection{
                .chunkIndex = chunkStats.chunkIndex,
                .liveEntities = chunkStats.liveEntities,
                .capacity = chunkStats.capacity,
                .payloadBytes = chunkStats.payloadBytes,
                .usedBytes = chunkStats.usedBytes,
                .wastedBytes = chunkStats.wastedBytes,
                .occupancyPercent = Percent(chunkStats.liveEntities, chunkStats.capacity),
                .wastedPercent = Percent(chunkStats.wastedBytes, chunkStats.payloadBytes),
            });
        }
    }

    return inspections;
}

bool World::CaptureEditorWorld(EditorWorldInspection& output) const {
    if (registries_ == nullptr) {
        output.entities.clear();
        return false;
    }
    return EditorWorldInspector::Inspect(*this, output);
}

} // namespace kb::ecs
