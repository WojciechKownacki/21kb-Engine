#pragma once

#include "engine/assets/AssetRegistry.hpp"

#include <memory>

namespace kb::assets {

// Owner-thread acquisition; workers receive only an immutable, generation-bound
// view. The canonical registry remains the sole mutable asset catalogue.
class AssetRegistrySnapshotCache {
public:
    [[nodiscard]] std::shared_ptr<const AssetRegistry> Acquire(const AssetRegistry& registry) {
        if (!snapshot_ || snapshot_->Generation() != registry.Generation()) {
            snapshot_ = std::make_shared<const AssetRegistry>(registry);
        }
        return snapshot_;
    }

private:
    std::shared_ptr<const AssetRegistry> snapshot_;
};

} // namespace kb::assets
