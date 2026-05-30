#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/snapshot/WorldSnapshotBuilder.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

namespace kb::ecs {

WorldSnapshot World::CaptureSnapshot() const {
    if (registries_ == nullptr) {
        return {};
    }
    return WorldSnapshotBuilder::Capture(world_, registries_->Components().Types());
}

} // namespace kb::ecs
