#pragma once

#include "engine/assets/AssetKind.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace kb::scene {

enum class UIComponentPropertyType : std::uint8_t {
    Bool,
    Int,
    UInt32,
    Float,
    String,
    Entity,
    Asset,
};

using UIComponentPropertyValue = std::variant<bool, std::int32_t, std::uint32_t, float, std::string, std::uint64_t>;

struct UIComponentPropertyDescriptor {
    std::string_view name;
    UIComponentPropertyType type = UIComponentPropertyType::Float;
    bool writable = true;
    std::optional<kb::assets::AssetKind> assetKind;
    std::string_view assetDependencyRole;
};

enum class UIComponentAssetReferenceValidationResult : std::uint8_t {
    Succeeded,
    NotAssetProperty,
    MissingAsset,
    WrongAssetKind,
};

enum class UIComponentPropertyWriteResult : std::uint8_t {
    Succeeded,
    ComponentMissing,
    PropertyMissing,
    ReadOnly,
    TypeMismatch,
    InvalidValue,
};

[[nodiscard]] std::span<const UIComponentPropertyDescriptor>
UIComponentPropertyCatalog(UIComponentType component) noexcept;
[[nodiscard]] const UIComponentPropertyDescriptor* FindUIComponentProperty(UIComponentType component,
                                                                           std::string_view property) noexcept;

} // namespace kb::scene

namespace kb::assets {

class AssetRegistry;

} // namespace kb::assets

namespace kb::scene {

[[nodiscard]] UIComponentAssetReferenceValidationResult ValidateUIComponentAssetReference(
    const kb::assets::AssetRegistry& registry,
    const UIComponentPropertyDescriptor& property,
    std::uint64_t rawAssetId) noexcept;

[[nodiscard]] bool HasUIComponent(const UIComponentSet& components, UIComponentType component) noexcept;
[[nodiscard]] bool AddUIComponent(UIComponentSet& components, UIComponentType component);
[[nodiscard]] bool RemoveUIComponent(UIComponentSet& components, UIComponentType component) noexcept;

[[nodiscard]] bool ReadUIComponentProperty(const UIComponentSet& components, UIComponentType component,
                                           std::string_view property, UIComponentPropertyValue& output);
[[nodiscard]] UIComponentPropertyWriteResult WriteUIComponentProperty(UIComponentSet& components,
                                                                      UIComponentType component,
                                                                      std::string_view property,
                                                                      const UIComponentPropertyValue& value);

} // namespace kb::scene
