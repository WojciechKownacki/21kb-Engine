#pragma once

#include "engine/scene/ScenePrefabOverrides.hpp"

#include <vector>

namespace kb::scene {

class ScenePrefabVariantOverrideList {
public:
    ScenePrefabVariantOverrideList() = delete;

    [[nodiscard]] static bool Upsert(std::vector<ScenePrefabPropertyOverride> source, ScenePrefabPropertyOverride property, std::vector<ScenePrefabPropertyOverride>& output);

    // LIB-161: fold an arbitrary override list into its canonical form —
    // dropping empty-propertyPath entries and collapsing duplicates for the
    // same (nodeId if non-zero else nodeIndex, propertyPath) key to a single
    // last-write-wins entry, preserving first-appearance position for the
    // survivors. Applying the Upsert key/precedence rule to the WHOLE list so
    // a variant's stored override vector is canonical from the moment it is
    // registered — the same rule instance-apply Upsert already enforces
    // incrementally. Without this, a duplicate left in the stored list would
    // let a later single-property Upsert (which replaces only the first
    // match) leave a stale duplicate that wins on the next re-materialization.
    [[nodiscard]] static std::vector<ScenePrefabPropertyOverride> Normalize(std::vector<ScenePrefabPropertyOverride> overrides);
};

} // namespace kb::scene
