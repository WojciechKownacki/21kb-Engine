#include "script/ScriptUIComponentApi.hpp"

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] const kb::scene::UIComponentDescriptor* ResolveComponent(std::string_view name) noexcept {
    return kb::scene::FindUIComponentDescriptor(name);
}

[[nodiscard]] ScriptValueType ScriptType(kb::scene::UIComponentPropertyType type) noexcept {
    using enum kb::scene::UIComponentPropertyType;
    switch (type) {
    case Bool:
        return ScriptValueType::Bool;
    case Int:
        return ScriptValueType::Int;
    case UInt32:
        return ScriptValueType::UInt32;
    case Float:
        return ScriptValueType::Float;
    case String:
        return ScriptValueType::String;
    case Entity:
        return ScriptValueType::Entity;
    case Asset:
        return ScriptValueType::Hash;
    }
    return ScriptValueType::Void;
}

[[nodiscard]] ScriptValue ToScriptValue(const kb::scene::UIComponentPropertyValue& value,
                                        kb::scene::UIComponentPropertyType type) {
    switch (type) {
    case kb::scene::UIComponentPropertyType::Bool:
        return ScriptValue{std::get<bool>(value)};
    case kb::scene::UIComponentPropertyType::Int:
        return ScriptValue{static_cast<int>(std::get<std::int32_t>(value))};
    case kb::scene::UIComponentPropertyType::UInt32:
        return ScriptValue{std::get<std::uint32_t>(value)};
    case kb::scene::UIComponentPropertyType::Float:
        return ScriptValue{std::get<float>(value)};
    case kb::scene::UIComponentPropertyType::String:
        return ScriptValue{std::get<std::string>(value)};
    case kb::scene::UIComponentPropertyType::Entity:
        return ScriptValue{std::get<std::uint64_t>(value), ScriptValueType::Entity};
    case kb::scene::UIComponentPropertyType::Asset:
        return ScriptValue{std::get<std::uint64_t>(value), ScriptValueType::Hash};
    }
    return {};
}

[[nodiscard]] bool FromScriptValue(const ScriptValue& source, kb::scene::UIComponentPropertyType type,
                                   kb::scene::UIComponentPropertyValue& output) {
    switch (type) {
    case kb::scene::UIComponentPropertyType::Bool:
        if (source.Type() != ScriptValueType::Bool)
            return false;
        output = source.AsBool();
        return true;
    case kb::scene::UIComponentPropertyType::Int:
        if (source.Type() != ScriptValueType::Int)
            return false;
        output = static_cast<std::int32_t>(source.AsInt());
        return true;
    case kb::scene::UIComponentPropertyType::UInt32:
        if (source.Type() == ScriptValueType::UInt32) {
            output = source.AsUInt32();
            return true;
        }
        if (source.Type() != ScriptValueType::Int || source.AsInt() < 0)
            return false;
        output = static_cast<std::uint32_t>(source.AsInt());
        return true;
    case kb::scene::UIComponentPropertyType::Float:
        if (source.Type() == ScriptValueType::Float) {
            output = source.AsFloat();
            return true;
        }
        if (source.Type() != ScriptValueType::Int)
            return false;
        output = static_cast<float>(source.AsInt());
        return true;
    case kb::scene::UIComponentPropertyType::String:
        if (source.Type() != ScriptValueType::String)
            return false;
        output = source.AsString();
        return true;
    case kb::scene::UIComponentPropertyType::Entity:
        if (source.Type() == ScriptValueType::Entity) {
            output = source.AsUInt64();
            return true;
        }
        if (source.Type() != ScriptValueType::Int || source.AsInt() < 0)
            return false;
        output = static_cast<std::uint64_t>(source.AsInt());
        return true;
    case kb::scene::UIComponentPropertyType::Asset:
        if (source.Type() == ScriptValueType::Hash) {
            output = source.AsUInt64();
            return true;
        }
        if (source.Type() != ScriptValueType::Int || source.AsInt() < 0)
            return false;
        output = static_cast<std::uint64_t>(source.AsInt());
        return true;
    }
    return false;
}

[[nodiscard]] std::string WriteError(kb::scene::UIComponentPropertyWriteResult result) {
    switch (result) {
    case kb::scene::UIComponentPropertyWriteResult::Succeeded:
        return {};
    case kb::scene::UIComponentPropertyWriteResult::ComponentMissing:
        return "component is not present on entity";
    case kb::scene::UIComponentPropertyWriteResult::PropertyMissing:
        return "component property is not registered for scripts";
    case kb::scene::UIComponentPropertyWriteResult::ReadOnly:
        return "component property is read-only for scripts";
    case kb::scene::UIComponentPropertyWriteResult::TypeMismatch:
        return "script value type does not match component property";
    case kb::scene::UIComponentPropertyWriteResult::InvalidValue:
        return "component property value is outside its valid range";
    }
    return "component property update failed";
}

[[nodiscard]] bool IsLiveEntityReference(kb::scene::Scene& scene,
                                         const kb::scene::UIComponentPropertyDescriptor& property,
                                         const kb::scene::UIComponentPropertyValue& value) noexcept {
    if (property.type != kb::scene::UIComponentPropertyType::Entity)
        return true;
    const std::uint64_t id = std::get<std::uint64_t>(value);
    return id == 0U || scene.Entities().IsAlive(kb::scene::SceneEntity{id});
}

[[nodiscard]] std::string ValidateAssetReference(
    kb::scene::Scene& scene,
    const kb::scene::UIComponentPropertyDescriptor& property,
    const kb::scene::UIComponentPropertyValue& value) {
    if (property.type != kb::scene::UIComponentPropertyType::Asset) {
        return {};
    }
    const std::uint64_t rawId = std::get<std::uint64_t>(value);
    const kb::scene::UIComponentAssetReferenceValidationResult validation =
        kb::scene::ValidateUIComponentAssetReference(
            scene.Assets().Manager().Registry(), property, rawId);
    if (validation == kb::scene::UIComponentAssetReferenceValidationResult::MissingAsset) {
        return "component asset reference must be empty or registered";
    }
    if (validation != kb::scene::UIComponentAssetReferenceValidationResult::Succeeded) {
        return "component asset reference has the wrong asset kind";
    }
    return {};
}

struct PropertyCatalogCache {
    std::array<std::vector<ScriptSceneComponentPropertyDesc>,
               static_cast<std::size_t>(kb::scene::UIComponentType::WidgetSwitcher) + 1U>
        properties;

    PropertyCatalogCache() {
        for (const kb::scene::UIComponentDescriptor& component : kb::scene::UIComponentCatalog()) {
            std::vector<ScriptSceneComponentPropertyDesc>& output =
                properties[static_cast<std::size_t>(component.type)];
            for (const kb::scene::UIComponentPropertyDescriptor& property :
                 kb::scene::UIComponentPropertyCatalog(component.type)) {
                output.push_back(ScriptSceneComponentPropertyDesc{
                    .name = property.name,
                    .type = ScriptType(property.type),
                    .writable = property.writable,
                });
            }
        }
    }
};

[[nodiscard]] const PropertyCatalogCache& PropertyCatalogs() {
    static const PropertyCatalogCache cache;
    return cache;
}

} // namespace

std::span<const std::string_view> ScriptUIComponentApi::ComponentNames() noexcept {
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> output;
        output.reserve(kb::scene::UIComponentCatalog().size());
        for (const kb::scene::UIComponentDescriptor& descriptor : kb::scene::UIComponentCatalog()) {
            output.push_back(descriptor.displayName);
        }
        return output;
    }();
    return names;
}

bool ScriptUIComponentApi::IsComponent(std::string_view componentName) noexcept {
    return ResolveComponent(componentName) != nullptr;
}

std::span<const ScriptSceneComponentPropertyDesc>
ScriptUIComponentApi::ComponentProperties(std::string_view componentName) noexcept {
    const kb::scene::UIComponentDescriptor* component = ResolveComponent(componentName);
    if (component == nullptr)
        return {};
    return PropertyCatalogs().properties[static_cast<std::size_t>(component->type)];
}

bool ScriptUIComponentApi::HasComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
                                        std::string_view componentName) noexcept {
    const kb::scene::UIComponentDescriptor* component = ResolveComponent(componentName);
    if (component == nullptr || !entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return false;
    }
    return kb::scene::HasUIComponent(kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity),
                                     component->type);
}

ScriptSceneComponentPropertyResult ScriptUIComponentApi::GetProperty(kb::scene::Scene& scene,
                                                                     kb::scene::SceneEntity entity,
                                                                     std::string_view componentName,
                                                                     std::string_view propertyName) {
    const kb::scene::UIComponentDescriptor* component = ResolveComponent(componentName);
    if (component == nullptr || !entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return ScriptSceneComponentPropertyResult{.error = "component is not present on entity"};
    }
    const kb::scene::UIComponentPropertyDescriptor* property =
        kb::scene::FindUIComponentProperty(component->type, propertyName);
    if (property == nullptr) {
        return ScriptSceneComponentPropertyResult{.error = "component property is not registered for scripts"};
    }
    const kb::scene::UIComponentSet components = kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity);
    kb::scene::UIComponentPropertyValue value{false};
    if (!kb::scene::ReadUIComponentProperty(components, component->type, propertyName, value)) {
        return ScriptSceneComponentPropertyResult{.error = "component is not present on entity"};
    }
    return ScriptSceneComponentPropertyResult{
        .succeeded = true,
        .value = ToScriptValue(value, property->type),
        .error = {},
    };
}

ScriptSceneComponentMutationResult ScriptUIComponentApi::SetProperty(kb::scene::Scene& scene,
                                                                     kb::scene::SceneEntity entity,
                                                                     std::string_view componentName,
                                                                     std::string_view propertyName,
                                                                     const ScriptValue& value) {
    const kb::scene::UIComponentDescriptor* component = ResolveComponent(componentName);
    if (component == nullptr || !entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return ScriptSceneComponentMutationResult{.error = "component is not present on entity"};
    }
    const kb::scene::UIComponentPropertyDescriptor* property =
        kb::scene::FindUIComponentProperty(component->type, propertyName);
    if (property == nullptr) {
        return ScriptSceneComponentMutationResult{.error = "component property is not registered for scripts"};
    }
    kb::scene::UIComponentPropertyValue converted{false};
    if (!FromScriptValue(value, property->type, converted)) {
        return ScriptSceneComponentMutationResult{.error = "script value type does not match component property"};
    }
    if (!IsLiveEntityReference(scene, *property, converted)) {
        return ScriptSceneComponentMutationResult{.error = "component entity reference must be empty or live"};
    }
    if (std::string assetError = ValidateAssetReference(scene, *property, converted); !assetError.empty()) {
        return ScriptSceneComponentMutationResult{.error = std::move(assetError)};
    }
    kb::scene::UIComponentSet components = kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity);
    const kb::scene::UIComponentPropertyWriteResult result =
        kb::scene::WriteUIComponentProperty(components, component->type, propertyName, converted);
    if (result != kb::scene::UIComponentPropertyWriteResult::Succeeded) {
        return ScriptSceneComponentMutationResult{.error = WriteError(result)};
    }
    kb::scene::SynchronizeSceneUIComponents(scene.Components().UI(), entity, components);
    return ScriptSceneComponentMutationResult{.succeeded = true, .error = {}};
}

} // namespace kb::script
