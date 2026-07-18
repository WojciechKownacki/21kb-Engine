#include "scene/prefab/io/ScenePrefabAssetVariantReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetKeyValueReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeListReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetOverrideReader.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace kb::scene {
namespace {

// LIB-092: parse one addedchild block — its host stable id, a node count, that
// many node/endnode blocks (via the shared node-list reader, so the subtree
// decodes byte-identically to any other node list), then the endaddedchild
// terminator. Any structural deviation is a hard parse error, exactly as the
// override reader treats a malformed override block.
[[nodiscard]] bool ReadAddedChild(std::istream& input, ScenePrefabVariantAddedSubtree& added) {
    std::string hostValue;
    std::string nodesValue;
    std::size_t nodeCount = 0;
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::AddedChildHostNodeIdKey, hostValue)
        || !ScenePrefabAssetFieldParser::ParseNumber(hostValue, added.hostNodeId)
        || !ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::NodesKey, nodesValue)
        || !ScenePrefabAssetFieldParser::ParseNumber(nodesValue, nodeCount)
        || !ScenePrefabAssetNodeListReader::Read(input, nodeCount, added.subtree)) {
        return false;
    }

    std::string terminator;
    return ScenePrefabAssetFieldParser::ReadLine(input, terminator) && terminator == ScenePrefabAssetFormat::EndAddedChildMarker;
}

// LIB-092: the added-children section is optional and always last in a variant
// file. Older files simply hit EOF here, which reads as "no added children"
// (returns true) rather than a failure. When the section is present, every
// addedchild block must parse or the whole load fails.
[[nodiscard]] bool ReadAddedChildren(std::istream& input, std::vector<ScenePrefabVariantAddedSubtree>& output) {
    std::string countValue;
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::AddedChildrenKey, countValue)) {
        return true;
    }

    std::size_t count = 0;
    if (!ScenePrefabAssetFieldParser::ParseNumber(countValue, count)) {
        return false;
    }
    output.reserve(count);
    std::string marker;
    for (std::size_t index = 0; index < count; ++index) {
        if (!ScenePrefabAssetFieldParser::ReadLine(input, marker) || marker != ScenePrefabAssetFormat::AddedChildMarker) {
            return false;
        }
        ScenePrefabVariantAddedSubtree added;
        if (!ReadAddedChild(input, added)) {
            return false;
        }
        output.push_back(std::move(added));
    }
    return true;
}

} // namespace

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

    if (!ScenePrefabAssetOverrideReader::Read(input, overrideCount, result.overrides, &result.missingOverrideNodeIds)) {
        return false;
    }

    return ReadAddedChildren(input, result.addedChildren);
}

} // namespace kb::scene
