#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

enum class RenderMaterialParameterType : std::uint8_t {
    Scalar,
    Vec3,
    Vec4,
    Color,
    Enum,
    Bool,
    Texture,
};

enum class RenderMaterialParameterGroup : std::uint8_t {
    Core,
    Surface,
    Advanced,
};

enum class RenderMaterialTextureColorSpace : std::uint8_t {
    Srgb,
    Linear,
    Unknown,
};

enum class RenderMaterialFeatureSupport : std::uint8_t {
    Supported,
    ParsedButIgnored,
    NotApplicable,
};

struct RenderMaterialParameterRange {
    float min = 0.0F;
    float max = 1.0F;
};

struct RenderMaterialParameterSchema {
    std::string_view name;
    RenderMaterialParameterType type = RenderMaterialParameterType::Scalar;
    RenderMaterialParameterGroup group = RenderMaterialParameterGroup::Core;
    RenderMaterialFeatureSupport runtimeSupport = RenderMaterialFeatureSupport::Supported;
    std::optional<RenderMaterialParameterRange> range;
    std::string_view defaultValueHint;
    std::string_view description;
};

struct RenderMaterialTextureSlotSchema {
    std::string_view name;
    std::string_view assetIdFieldName;
    std::string_view pathFieldName;
    RenderMaterialTextureColorSpace expectedColorSpace = RenderMaterialTextureColorSpace::Unknown;
    RenderMaterialFeatureSupport runtimeSupport = RenderMaterialFeatureSupport::Supported;
    std::string_view description;
    std::string_view fallbackDescription;
};

struct RenderMaterialTypeSchema {
    std::string_view typeName;
    std::uint32_t typeVersion = 0;
    std::vector<RenderMaterialParameterSchema> parameters;
    std::vector<RenderMaterialTextureSlotSchema> textureSlots;
    std::vector<std::string_view> alphaModes;
    std::vector<std::string_view> unsupportedAdvancedFeatures;
};

/// Returns the built-in PBR material type schema (version 1).
[[nodiscard]] const RenderMaterialTypeSchema& GetBuiltInPbrMaterialTypeSchema() noexcept;

/// Finds a parameter schema by exact field name. Returns nullptr if not found.
[[nodiscard]] const RenderMaterialParameterSchema* FindMaterialParameterSchema(
    const RenderMaterialTypeSchema& schema, std::string_view name) noexcept;

/// Finds a texture slot schema by its asset-id field name. Returns nullptr if not found.
[[nodiscard]] const RenderMaterialTextureSlotSchema* FindMaterialTextureSlotSchema(
    const RenderMaterialTypeSchema& schema, std::string_view assetIdFieldName) noexcept;

/// Checks if a field name corresponds to a texture path field in the schema.
[[nodiscard]] bool IsMaterialTexturePathField(
    const RenderMaterialTypeSchema& schema, std::string_view name) noexcept;

} // namespace kb::render
