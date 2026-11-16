#include "scene/prefab/ScenePrefabModuleComponentOverrides.hpp"

#include "engine/scene/SceneModuleComponents.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

constexpr std::string_view kPrefix = "moduleComponents.";

struct Target {
    std::string_view name;
    std::string_view field; // empty for the component itself
};

[[nodiscard]] Target Split(std::string_view propertyPath) noexcept {
    const std::string_view rest = propertyPath.substr(kPrefix.size());
    const std::size_t slash = rest.find('/');
    return slash == std::string_view::npos ? Target{ rest, {} } : Target{ rest.substr(0U, slash), rest.substr(slash + 1U) };
}

[[nodiscard]] std::string Path(std::string_view name, std::string_view field = {}) {
    return std::string{ kPrefix } + std::string{ name } + (field.empty() ? std::string{} : '/' + std::string{ field });
}

[[nodiscard]] const SceneModuleComponentData* Find(const std::vector<SceneModuleComponentData>& components, std::string_view name) noexcept {
    const auto found = std::ranges::find(components, name, &SceneModuleComponentData::name);
    return found == components.end() ? nullptr : &*found;
}

void Put(std::vector<SceneModuleComponentData>& components, SceneModuleComponentData data) {
    std::erase_if(components, [&data](const SceneModuleComponentData& existing) { return existing.name == data.name; });
    components.push_back(std::move(data));
    std::ranges::sort(components, {}, &SceneModuleComponentData::name);
}

} // namespace

bool ScenePrefabModuleComponentOverrides::Handles(std::string_view propertyPath) noexcept {
    return propertyPath.substr(0U, kPrefix.size()) == kPrefix;
}

std::vector<std::string> ScenePrefabModuleComponentOverrides::Differing(Scene& scene, SceneEntity entity, const ScenePrefabNodeComponents& expected) {
    const std::vector<SceneModuleComponentData> actual = SceneModuleComponents{ scene }.Capture(entity);
    std::vector<std::string> names;
    for (const SceneModuleComponentData& data : actual) {
        const SceneModuleComponentData* prefab = Find(expected.moduleComponents, data.name);
        if (prefab == nullptr || prefab->bytes != data.bytes) names.push_back(data.name);
    }
    for (const SceneModuleComponentData& data : expected.moduleComponents) {
        if (Find(actual, data.name) == nullptr) names.push_back(data.name);
    }
    return names;
}

void ScenePrefabModuleComponentOverrides::Append(Scene& scene, SceneEntity entity, const ScenePrefabNodeComponents& expected,
    ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const std::vector<SceneModuleComponentData> actual = SceneModuleComponents{ scene }.Capture(entity);
    for (const std::string& name : Differing(scene, entity, expected)) {
        const SceneModuleComponentData* live = Find(actual, name);
        const SceneModuleComponentData* prefab = Find(expected.moduleComponents, name);
        if (live == nullptr) {
            ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, Path(name), "null", ScenePrefabOverrideFlag::None);
            continue;
        }
        const std::optional<std::vector<std::string>> fields =
            prefab == nullptr ? std::nullopt : SceneModuleComponents::DifferingFields(*live, *prefab);
        if (!fields.has_value()) {
            ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, Path(name),
                ScenePrefabAssetEscaper::EncodeHex(live->bytes), ScenePrefabOverrideFlag::None);
            continue;
        }
        for (const std::string& field : *fields) {
            const std::optional<std::vector<std::uint8_t>> value = SceneModuleComponents::FieldValue(*live, field);
            if (value.has_value()) {
                ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, Path(name, field),
                    ScenePrefabAssetEscaper::EncodeHex(*value), ScenePrefabOverrideFlag::None);
            }
        }
    }
}

bool ScenePrefabModuleComponentOverrides::ApplyToNode(ScenePrefabNodeDesc& node, const ScenePrefabPropertyOverride& property) {
    const Target target = Split(property.propertyPath);
    if (target.name.empty()) {
        return false;
    }
    std::vector<SceneModuleComponentData>& components = node.components.moduleComponents;
    if (target.field.empty()) {
        if (property.value == "null") {
            std::erase_if(components, [&target](const SceneModuleComponentData& data) { return data.name == target.name; });
            return true;
        }
        std::optional<std::vector<std::uint8_t>> bytes = ScenePrefabAssetEscaper::DecodeHex(property.value);
        if (!bytes.has_value()) {
            return false;
        }
        Put(components, SceneModuleComponentData{ std::string{ target.name }, std::move(*bytes) });
        return true;
    }
    const std::optional<std::vector<std::uint8_t>> value = ScenePrefabAssetEscaper::DecodeHex(property.value);
    if (!value.has_value()) {
        return false;
    }
    const auto found = std::ranges::find(components, target.name, &SceneModuleComponentData::name);
    if (found != components.end()) {
        static_cast<void>(SceneModuleComponents::SetFieldValue(*found, target.field, *value)); // a field the prefab dropped is stale
    }
    return true;
}

void ScenePrefabModuleComponentOverrides::Revert(Scene& scene, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath) {
    const Target target = Split(propertyPath);
    SceneModuleComponents{ scene }.RestoreComponent(object.Entity(), target.name, Find(node.components.moduleComponents, target.name));
}

void ScenePrefabModuleComponentOverrides::CopyToNode(Scene& scene, SceneObject object, ScenePrefabNodeDesc& node, std::string_view propertyPath) {
    const Target target = Split(propertyPath);
    const std::vector<SceneModuleComponentData> actual = SceneModuleComponents{ scene }.Capture(object.Entity());
    if (const SceneModuleComponentData* live = Find(actual, target.name)) {
        Put(node.components.moduleComponents, *live);
    } else {
        std::erase_if(node.components.moduleComponents, [&target](const SceneModuleComponentData& data) { return data.name == target.name; });
    }
}

bool ScenePrefabModuleComponentOverrides::Value(Scene& scene, SceneObject object, std::string_view propertyPath, std::string& value) {
    const Target target = Split(propertyPath);
    const std::vector<SceneModuleComponentData> actual = SceneModuleComponents{ scene }.Capture(object.Entity());
    const SceneModuleComponentData* live = Find(actual, target.name);
    if (live == nullptr) {
        value = "null";
        return target.field.empty();
    }
    if (target.field.empty()) {
        value = ScenePrefabAssetEscaper::EncodeHex(live->bytes);
        return true;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = SceneModuleComponents::FieldValue(*live, target.field);
    if (!bytes.has_value()) {
        return false;
    }
    value = ScenePrefabAssetEscaper::EncodeHex(*bytes);
    return true;
}

} // namespace kb::scene
