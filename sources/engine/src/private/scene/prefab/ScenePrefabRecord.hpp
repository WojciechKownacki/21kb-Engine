#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabBakedData.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

enum class ScenePrefabRecordKind {
    Template,
    Variant,
};

// LIB-092: a whole entity SUBTREE a variant adds on top of its base prefab
// (the "AddedChild" override — a child the user attached to an instance that
// the base template does not have). Unlike a ScenePrefabPropertyOverride,
// which only edits an existing node's leaf value, this carries the added
// content itself (captured via ScenePrefabCaptureService), so it can be
// re-created for every future instance and survive a save/load round trip —
// which the property-override list alone could never express. `hostNodeId`
// is the stable id of the base node the subtree nests under;
// `subtree.Nodes()[0]` is the added root (parentNode == NoParent locally).
struct ScenePrefabVariantAddedSubtree {
    std::uint64_t hostNodeId = 0;
    ScenePrefab subtree;
};

struct ScenePrefabRecord {
    ScenePrefabRecordKind kind = ScenePrefabRecordKind::Template;
    std::string guid;
    // The asset file this record was loaded from, resolved to one canonical
    // spelling; empty for a prefab registered in memory. It is what tells a
    // genuine identity collision (two DIFFERENT ".kbprefab" files declaring one
    // guid, which copying a prefab file produces) apart from the same file being
    // read again after its content changed - only the former retires the guid.
    std::string sourcePath;
    std::string name;
    ScenePrefab prefab;
    ScenePrefabHandle basePrefab{};
    std::string basePrefabGuid;
    std::vector<ScenePrefabPropertyOverride> variantOverrides;
    // LIB-092: entity subtrees this variant adds on top of its base (see
    // ScenePrefabVariantAddedSubtree). Re-appended to `prefab` every time the
    // variant is materialized (ScenePrefabVariantMaterializer), so a variant
    // instance genuinely reproduces the added children in memory and from disk.
    std::vector<ScenePrefabVariantAddedSubtree> variantAddedChildren;
    std::uint64_t contentHash = 0;
    ScenePrefabBakedData bakedPrefab;
    std::uint64_t bakedContentHash = 0;
};

} // namespace kb::scene
