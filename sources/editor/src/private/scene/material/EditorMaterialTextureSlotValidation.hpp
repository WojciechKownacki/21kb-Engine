#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace kb::editor {

enum class EditorMaterialTextureSemantic {
    Unknown,
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
};

enum class EditorMaterialTextureColorSpace {
    Srgb,
    Linear,
};

struct EditorMaterialTextureSlotValidationResult {
    bool accepted = true;
    EditorMaterialTextureSemantic expectedSemantic = EditorMaterialTextureSemantic::Unknown;
    EditorMaterialTextureSemantic inferredSemantic = EditorMaterialTextureSemantic::Unknown;
    EditorMaterialTextureColorSpace expectedColorSpace = EditorMaterialTextureColorSpace::Linear;
    EditorMaterialTextureColorSpace inferredColorSpace = EditorMaterialTextureColorSpace::Linear;
};

class EditorMaterialTextureSlotValidation {
public:
    EditorMaterialTextureSlotValidation() = delete;

    [[nodiscard]] static EditorMaterialTextureSemantic ExpectedSemantic(EditorMaterialTextureSlot slot) noexcept {
        switch (slot) {
        case EditorMaterialTextureSlot::Albedo:
            return EditorMaterialTextureSemantic::BaseColor;
        case EditorMaterialTextureSlot::Normal:
            return EditorMaterialTextureSemantic::Normal;
        case EditorMaterialTextureSlot::MetallicRoughness:
            return EditorMaterialTextureSemantic::MetallicRoughness;
        case EditorMaterialTextureSlot::Occlusion:
            return EditorMaterialTextureSemantic::Occlusion;
        case EditorMaterialTextureSlot::Emissive:
            return EditorMaterialTextureSemantic::Emissive;
        }
        return EditorMaterialTextureSemantic::Unknown;
    }

    [[nodiscard]] static EditorMaterialTextureColorSpace ExpectedColorSpace(EditorMaterialTextureSemantic semantic) noexcept {
        switch (semantic) {
        case EditorMaterialTextureSemantic::BaseColor:
        case EditorMaterialTextureSemantic::Emissive:
            return EditorMaterialTextureColorSpace::Srgb;
        case EditorMaterialTextureSemantic::Normal:
        case EditorMaterialTextureSemantic::MetallicRoughness:
        case EditorMaterialTextureSemantic::Occlusion:
        case EditorMaterialTextureSemantic::Unknown:
            return EditorMaterialTextureColorSpace::Linear;
        }
        return EditorMaterialTextureColorSpace::Linear;
    }

    [[nodiscard]] static std::string_view SemanticName(EditorMaterialTextureSemantic semantic) noexcept {
        switch (semantic) {
        case EditorMaterialTextureSemantic::BaseColor:
            return "Base Color";
        case EditorMaterialTextureSemantic::Normal:
            return "Normal";
        case EditorMaterialTextureSemantic::MetallicRoughness:
            return "Metallic-Roughness";
        case EditorMaterialTextureSemantic::Occlusion:
            return "Occlusion";
        case EditorMaterialTextureSemantic::Emissive:
            return "Emissive";
        case EditorMaterialTextureSemantic::Unknown:
            return "Unknown";
        }
        return "Unknown";
    }

    [[nodiscard]] static std::string_view ColorSpaceName(EditorMaterialTextureColorSpace colorSpace) noexcept {
        return colorSpace == EditorMaterialTextureColorSpace::Srgb ? "sRGB" : "linear";
    }

    [[nodiscard]] static EditorMaterialTextureSemantic InferSemantic(const kb::assets::AssetMetadata& metadata) {
        const std::string text = LowerAscii(metadata.name + " " + metadata.virtualPath.generic_string() + " " + metadata.physicalPath.generic_string());
        if (ContainsToken(text, "normal") || ContainsToken(text, " nrm") || ContainsToken(text, "_nrm") || ContainsToken(text, "-nrm")) {
            return EditorMaterialTextureSemantic::Normal;
        }
        if (ContainsToken(text, "metallicroughness") || ContainsToken(text, "metallic-roughness") || ContainsToken(text, "metallic_roughness")
            || ContainsToken(text, "roughnessmetallic") || ContainsToken(text, "orm") || ContainsToken(text, "rmo") || ContainsToken(text, "mr.")) {
            return EditorMaterialTextureSemantic::MetallicRoughness;
        }
        if (ContainsToken(text, "occlusion") || ContainsToken(text, "ambientocclusion") || ContainsToken(text, "ambient_occlusion") || ContainsToken(text, " ao")
            || ContainsToken(text, "_ao") || ContainsToken(text, "-ao")) {
            return EditorMaterialTextureSemantic::Occlusion;
        }
        if (ContainsToken(text, "emissive") || ContainsToken(text, "emission")) {
            return EditorMaterialTextureSemantic::Emissive;
        }
        if (ContainsToken(text, "basecolor") || ContainsToken(text, "base_color") || ContainsToken(text, "albedo") || ContainsToken(text, "diffuse")) {
            return EditorMaterialTextureSemantic::BaseColor;
        }
        return EditorMaterialTextureSemantic::Unknown;
    }

    [[nodiscard]] static EditorMaterialTextureSlotValidationResult Validate(const kb::assets::AssetMetadata& metadata, EditorMaterialTextureSlot slot) {
        EditorMaterialTextureSlotValidationResult result{};
        result.expectedSemantic = ExpectedSemantic(slot);
        result.inferredSemantic = InferSemantic(metadata);
        result.expectedColorSpace = ExpectedColorSpace(result.expectedSemantic);
        result.inferredColorSpace = ExpectedColorSpace(result.inferredSemantic);
        result.accepted = result.inferredSemantic == EditorMaterialTextureSemantic::Unknown || result.inferredSemantic == result.expectedSemantic;
        return result;
    }

private:
    [[nodiscard]] static std::string LowerAscii(std::string text) {
        std::ranges::transform(text, text.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        return text;
    }

    [[nodiscard]] static bool ContainsToken(std::string_view text, std::string_view token) noexcept {
        return text.find(token) != std::string_view::npos;
    }
};

} // namespace kb::editor
