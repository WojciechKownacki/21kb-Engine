#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::scene {

struct ScenePrefabAssetReadResult {
    ScenePrefabAssetKind kind = ScenePrefabAssetKind::Template;
    std::string guid;
    std::string name;
    std::string baseGuid;
    ScenePrefab prefab;
    std::vector<ScenePrefabPropertyOverride> overrides;
    // LIB-092: added-child subtrees parsed from a variant asset (empty for
    // templates and for variants written before the feature existed).
    std::vector<ScenePrefabVariantAddedSubtree> addedChildren;
    bool missingNodeStableIds = false;
    bool missingOverrideNodeIds = false;
};

class ScenePrefabAssetReader {
public:
    ScenePrefabAssetReader() = delete;

    [[nodiscard]] static bool Read(const std::filesystem::path& path, ScenePrefabAssetReadResult& output);

    // Reads ONLY a prefab asset's identity header (format line, kind, guid) and
    // stops before its node list. Asset dependency discovery has to map a scene's
    // nested-prefab guid reference onto the prefab asset that declares that guid,
    // and it must not pay a full prefab parse for every candidate file to do it.
    // Returns false - leaving `guid` untouched - for an unreadable file, for a
    // malformed header, and for a legacy "21kb.prefab.v1" file, which carries no
    // guid at all (such a prefab is registered under a freshly generated guid on
    // load, so it can never be the target of a persisted nested reference).
    [[nodiscard]] static bool ReadGuid(const std::filesystem::path& path, std::string& guid);
};

} // namespace kb::scene
