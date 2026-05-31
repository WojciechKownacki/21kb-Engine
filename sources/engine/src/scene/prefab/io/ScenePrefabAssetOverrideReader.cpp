#include "scene/prefab/io/ScenePrefabAssetOverrideReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetKeyValueReader.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] bool ReadOverrideFields(std::istream& input, ScenePrefabPropertyOverride& property) {
    std::string line;
    ScenePrefabAssetFieldMap fields;
    while (ScenePrefabAssetFieldParser::ReadLine(input, line)) {
        if (line == ScenePrefabAssetFormat::EndOverrideMarker) {
            std::size_t nodeIndex = 0;
            std::size_t flag = 0;
            const auto node = fields.find(std::string{ ScenePrefabAssetFormat::OverrideNodeKey });
            const auto propertyPath = fields.find(std::string{ ScenePrefabAssetFormat::OverridePropertyPathKey });
            const auto value = fields.find(std::string{ ScenePrefabAssetFormat::OverrideValueKey });
            const auto flagValue = fields.find(std::string{ ScenePrefabAssetFormat::OverrideFlagKey });
            if (node == fields.end()
                || propertyPath == fields.end()
                || value == fields.end()
                || flagValue == fields.end()
                || !ScenePrefabAssetFieldParser::ParseNumber(node->second, nodeIndex)
                || !ScenePrefabAssetFieldParser::ParseNumber(flagValue->second, flag)
                || !ScenePrefabAssetKeyValueReader::ReadEscaped(propertyPath->second, property.propertyPath)
                || !ScenePrefabAssetKeyValueReader::ReadEscaped(value->second, property.value)) {
                return false;
            }

            property.nodeIndex = static_cast<std::uint32_t>(nodeIndex);
            property.flag = static_cast<ScenePrefabOverrideFlag>(static_cast<std::uint32_t>(flag));
            return true;
        }

        std::string key;
        std::string value;
        if (!ScenePrefabAssetFieldParser::SplitKeyValue(line, key, value) || fields.contains(key)) {
            return false;
        }
        fields.emplace(std::move(key), std::move(value));
    }
    return false;
}

} // namespace

bool ScenePrefabAssetOverrideReader::Read(std::istream& input, std::size_t overrideCount, std::vector<ScenePrefabPropertyOverride>& output) {
    output.clear();
    output.reserve(overrideCount);
    std::string line;
    for (std::size_t index = 0; index < overrideCount; ++index) {
        if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || line != ScenePrefabAssetFormat::OverrideMarker) {
            return false;
        }
        ScenePrefabPropertyOverride property;
        if (!ReadOverrideFields(input, property)) {
            return false;
        }
        output.push_back(std::move(property));
    }
    return true;
}

} // namespace kb::scene
