#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/snapshot/WorldSnapshotBuilder.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <span>

namespace kb::ecs {

WorldSnapshot World::CaptureSnapshot() const {
    if (registries_ == nullptr) {
        return {};
    }
    return WorldSnapshotBuilder::Capture(
        world_,
        registries_->Components().Types(),
        +[](Entity::IdType entityIdWithoutGeneration, void* context) -> Entity {
            const auto* world = static_cast<const World*>(context);
            return world != nullptr ? world->ResolveAliveEntity(entityIdWithoutGeneration) : Entity{};
        },
        const_cast<World*>(this));
}

ChunkedWorldSnapshot World::CaptureChunkedSnapshot() const {
    if (registries_ == nullptr) {
        return {};
    }

    ChunkedWorldSnapshot snapshot;
    if (nativeStorage_ == nullptr) {
        const std::span<const ComponentTypeInfo> componentTypes = registries_->Components().Types();
        snapshot.componentTypes.assign(componentTypes.begin(), componentTypes.end());
        return snapshot;
    }

    nativeStorage_->CaptureChunkedSnapshot(registries_->Components().Types(), snapshot);
    return snapshot;
}

ChunkedWorldDeltaSnapshot World::CaptureChunkedDeltaSnapshot(const ChunkedWorldSnapshot& baseline) const {
    if (registries_ == nullptr) {
        return {};
    }

    ChunkedWorldDeltaSnapshot delta;
    if (nativeStorage_ == nullptr) {
        const std::span<const ComponentTypeInfo> componentTypes = registries_->Components().Types();
        delta.componentTypes.assign(componentTypes.begin(), componentTypes.end());
        return delta;
    }

    nativeStorage_->CaptureChunkedDeltaSnapshot(registries_->Components().Types(), baseline, delta);
    return delta;
}

bool World::StreamChunkedSnapshot(
    ChunkedWorldSnapshotHeaderVisitor headerVisitor,
    ChunkedWorldSnapshotChunkVisitor chunkVisitor,
    void* context) const {
    if (registries_ == nullptr || headerVisitor == nullptr || chunkVisitor == nullptr) {
        return false;
    }

    const std::span<const ComponentTypeInfo> componentTypes = registries_->Components().Types();
    ChunkedWorldSnapshotHeader header;
    header.componentTypes.assign(componentTypes.begin(), componentTypes.end());
    if (nativeStorage_ != nullptr) {
        header.entityCount = nativeStorage_->Stats().liveEntities;
    }

    if (!headerVisitor(header, context)) {
        return false;
    }

    return nativeStorage_ != nullptr && nativeStorage_->StreamChunkedSnapshot(componentTypes, chunkVisitor, context);
}

} // namespace kb::ecs
