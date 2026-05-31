#include "scene/prefab/io/ScenePrefabAssetVariantReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetKeyValueReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetOverrideReader.hpp"

namespace kb::scene {

bool ScenePrefabAssetVariantReader::ReadV2(std::istream& input, ScenePrefabAssetReadResult& result) {
    result.kind = ScenePrefabAssetKind::Variant;
    std::string value;
    std::string overrideCountValue;
    std::size_t overrideCount = 0;
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::BaseGuidKey, value)
        || !ScenePrefabAssetKeyValueReader::ReadEscaped(value, result.baseGuid)
        || !ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::OverridesKey, overrideCountValue)
        || !ScenePrefabAssetFieldParser::ParseNumber(overrideCountValue, overrideCount)) {
        return false;
    }

    return ScenePrefabAssetOverrideReader::Read(input, overrideCount, result.overrides);
}

} // namespace kb::scene
