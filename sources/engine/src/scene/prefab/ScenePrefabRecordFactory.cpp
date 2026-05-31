#include "scene/prefab/ScenePrefabRecordFactory.hpp"

#include "scene/prefab/ScenePrefabGuid.hpp"
#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <utility>

namespace kb::scene {

std::optional<ScenePrefabRecord> ScenePrefabRecordFactory::CreateTemplate(std::string name, ScenePrefab prefab, std::uint64_t localId) {
    if (!ScenePrefabValidator::IsValid(prefab) || prefab.Empty()) {
        return std::nullopt;
    }

    const std::uint64_t contentHash = ScenePrefabHasher::Hash(prefab);
    return ScenePrefabRecord{
        .kind = ScenePrefabRecordKind::Template,
        .guid = ScenePrefabGuid::Create(name, prefab, localId),
        .name = std::move(name),
        .prefab = std::move(prefab),
        .contentHash = contentHash,
    };
}

std::optional<ScenePrefabRecord> ScenePrefabRecordFactory::CreateLoadedTemplate(std::string guid, std::string name, ScenePrefab prefab) {
    if (guid.empty() || !ScenePrefabValidator::IsValid(prefab) || prefab.Empty()) {
        return std::nullopt;
    }

    return ScenePrefabRecord{
        .kind = ScenePrefabRecordKind::Template,
        .guid = std::move(guid),
        .name = std::move(name),
        .prefab = std::move(prefab),
        .contentHash = ScenePrefabHasher::Hash(prefab),
    };
}

ScenePrefabRecord ScenePrefabRecordFactory::CreateVariant(std::string name, ScenePrefabHandle basePrefab, const ScenePrefabRecord& baseRecord, std::uint64_t localId, std::vector<ScenePrefabPropertyOverride> overrides) {
    return ScenePrefabRecord{
        .kind = ScenePrefabRecordKind::Variant,
        .guid = ScenePrefabGuid::Create(name, baseRecord.prefab, localId),
        .name = std::move(name),
        .basePrefab = basePrefab,
        .basePrefabGuid = baseRecord.guid,
        .variantOverrides = std::move(overrides),
    };
}

ScenePrefabRecord ScenePrefabRecordFactory::CreateLoadedVariant(std::string guid, std::string name, ScenePrefabHandle basePrefab, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides) {
    return ScenePrefabRecord{
        .kind = ScenePrefabRecordKind::Variant,
        .guid = std::move(guid),
        .name = std::move(name),
        .basePrefab = basePrefab,
        .basePrefabGuid = std::move(basePrefabGuid),
        .variantOverrides = std::move(overrides),
    };
}

} // namespace kb::scene
