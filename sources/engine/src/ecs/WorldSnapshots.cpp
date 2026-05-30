#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/snapshot/WorldSnapshotBuilder.hpp"

namespace kb::ecs {

WorldSnapshot World::CaptureSnapshot() const {
    if (components_ == nullptr) {
        return {};
    }
    return WorldSnapshotBuilder::Capture(world_, components_->Types());
}

} // namespace kb::ecs
