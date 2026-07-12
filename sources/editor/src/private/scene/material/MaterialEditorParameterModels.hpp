#pragma once

#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace kb::editor {

enum class MaterialEditorParameterValueKind : std::uint8_t {
    None,
    Scalar,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Enum,
    Bool,
    TextureAsset,
};

enum class MaterialEditorParameterGroup : std::uint8_t {
    Core,
    Surface,
    Texture,
    Advanced,
};

struct MaterialEditorParameterValue {
    MaterialEditorParameterValueKind kind = MaterialEditorParameterValueKind::None;
    std::array<float, 4U> numbers{};
    std::uint64_t assetId = 0;
    bool boolValue = false;
    std::string text;
};

struct MaterialEditorParameter {
    std::string stableId;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterGroup group = MaterialEditorParameterGroup::Core;
    std::string displayName;
    std::string description;
    MaterialEditorParameterValue value{};
    MaterialEditorParameterValue defaultValue{};
    std::optional<kb::render::RenderMaterialParameterRange> range;
    std::optional<kb::render::RenderMaterialTextureColorSpace> expectedTextureColorSpace;
    bool overrideEnabled = true;
    bool overrideActive = false;
    bool enabled = true;
    std::uint32_t sortOrder = 0U;
};

} // namespace kb::editor
