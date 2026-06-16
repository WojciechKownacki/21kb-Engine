#include "scene/prefab/io/ScenePrefabAssetNestedOverrideParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace kb::scene {
namespace {

[[nodiscard]] bool ParseNestedOverride(const ScenePrefabAssetFieldMap& fields, std::size_t index, ScenePrefabPropertyOverride& property) {
    const std::string prefix = std::string{ ScenePrefabAssetFormat::NestedOverridePrefix } + std::to_string(index) + ".";
    const auto node = fields.find(prefix + std::string{ ScenePrefabAssetFormat::OverrideNodeKey });
    const auto nodeId = fields.find(prefix + std::string{ ScenePrefabAssetFormat::OverrideNodeIdKey });
    const auto propertyPath = fields.find(prefix + std::string{ ScenePrefabAssetFormat::OverridePropertyPathKey });
    const auto value = fields.find(prefix + std::string{ ScenePrefabAssetFormat::OverrideValueKey });
    const auto flag = fields.find(prefix + std::string{ ScenePrefabAssetFormat::OverrideFlagKey });
    std::size_t nodeIndex = 0;
    std::uint64_t stableNodeId = 0;
    std::size_t flagValue = 0;
    if (node == fields.end()
        || propertyPath == fields.end()
        || value == fields.end()
        || flag == fields.end()
        || !ScenePrefabAssetFieldParser::ParseNumber(node->second, nodeIndex)
        || !ScenePrefabAssetFieldParser::ParseNumber(flag->second, flagValue)) {
        return false;
    }
    if (nodeId != fields.end() && !ScenePrefabAssetFieldParser::ParseNumber(nodeId->second, stableNodeId)) {
        return false;
    }

    std::optional<std::string> unescapedPath = ScenePrefabAssetEscaper::Unescape(propertyPath->second);
    std::optional<std::string> unescapedValue = ScenePrefabAssetEscaper::Unescape(value->second);
    if (!unescapedPath.has_value() || !unescapedValue.has_value()) {
        return false;
    }

    property = ScenePrefabPropertyOverride{
        .nodeIndex = static_cast<std::uint32_t>(nodeIndex),
        .nodeId = stableNodeId,
        .propertyPath = std::move(*unescapedPath),
        .value = std::move(*unescapedValue),
        .flag = static_cast<ScenePrefabOverrideFlag>(static_cast<std::uint32_t>(flagValue)),
    };
    return true;
}

} // namespace

bool ScenePrefabAssetNestedOverrideParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node) {
    const auto count = fields.find(std::string{ ScenePrefabAssetFormat::NestedOverrideCountKey });
    if (count == fields.end()) {
        node.nestedPrefabOverrides.clear();
        return true;
    }

    std::size_t overrideCount = 0;
    if (!ScenePrefabAssetFieldParser::ParseNumber(count->second, overrideCount)) {
        return false;
    }

    node.nestedPrefabOverrides.clear();
    node.nestedPrefabOverrides.reserve(overrideCount);
    for (std::size_t index = 0; index < overrideCount; ++index) {
        ScenePrefabPropertyOverride property;
        if (!ParseNestedOverride(fields, index, property)) {
            return false;
        }
        node.nestedPrefabOverrides.push_back(std::move(property));
    }
    return true;
}

} // namespace kb::scene
