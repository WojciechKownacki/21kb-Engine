#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorTextureAssetMetadataResolver.hpp"

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
    Unknown,
    Srgb,
    Linear,
};

struct EditorMaterialTextureSlotValidationResult {
    bool accepted = true;
    EditorMaterialTextureSemantic expectedSemantic = EditorMaterialTextureSemantic::Unknown;
    EditorMaterialTextureSemantic inferredSemantic = EditorMaterialTextureSemantic::Unknown;
    EditorMaterialTextureColorSpace expectedColorSpace = EditorMaterialTextureColorSpace::Unknown;
    EditorMaterialTextureColorSpace inferredColorSpace = EditorMaterialTextureColorSpace::Unknown;
    std::string metadataDiagnostic;
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
            return EditorMaterialTextureColorSpace::Linear;
        case EditorMaterialTextureSemantic::Unknown:
            return EditorMaterialTextureColorSpace::Unknown;
        }
        return EditorMaterialTextureColorSpace::Unknown;
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
        switch (colorSpace) {
        case EditorMaterialTextureColorSpace::Srgb:
            return "sRGB";
        case EditorMaterialTextureColorSpace::Linear:
            return "linear";
        case EditorMaterialTextureColorSpace::Unknown:
            return "Unknown";
        }
        return "Unknown";
    }

    [[nodiscard]] static EditorMaterialTextureSlotValidationResult Validate(const kb::assets::AssetMetadata& metadata, EditorMaterialTextureSlot slot) {
        EditorMaterialTextureSlotValidationResult result{};
        result.expectedSemantic = ExpectedSemantic(slot);
        result.expectedColorSpace = ExpectedColorSpace(result.expectedSemantic);
        const EditorTextureAssetMetadataResolution resolved = EditorResolveTextureAssetMetadata(metadata);
        if (!resolved.Resolved()) {
            result.accepted = false;
            result.metadataDiagnostic = resolved.diagnostic;
            return result;
        }

        result.inferredSemantic = SemanticFromAsset(resolved.asset->semantic);
        result.inferredColorSpace = ColorSpaceFromAsset(resolved.asset->colorSpace);
        if (result.inferredSemantic == EditorMaterialTextureSemantic::Unknown) {
            result.metadataDiagnostic = "Texture semantic metadata is Unknown.";
        }
        if (result.inferredColorSpace == EditorMaterialTextureColorSpace::Unknown) {
            if (!result.metadataDiagnostic.empty()) {
                result.metadataDiagnostic += " ";
            }
            result.metadataDiagnostic += "Texture color-space metadata is Unknown.";
        }
        result.accepted = result.inferredSemantic == result.expectedSemantic &&
            result.inferredColorSpace == result.expectedColorSpace;
        return result;
    }

    [[nodiscard]] static std::string RejectionMessage(const kb::assets::AssetMetadata& metadata, const EditorMaterialTextureSlotValidationResult& validation) {
        std::string message = "Texture '" + (metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name)
            + "' declares " + std::string{ SemanticName(validation.inferredSemantic) }
            + "/" + std::string{ ColorSpaceName(validation.inferredColorSpace) }
            + ", but the " + std::string{ SemanticName(validation.expectedSemantic) }
            + " slot requires " + std::string{ ColorSpaceName(validation.expectedColorSpace) } + ".";
        if (!validation.metadataDiagnostic.empty()) {
            message += " " + validation.metadataDiagnostic;
        }
        return message;
    }

private:
    [[nodiscard]] static EditorMaterialTextureSemantic SemanticFromAsset(kb::render::RenderTextureAssetSemantic semantic) noexcept {
        switch (semantic) {
        case kb::render::RenderTextureAssetSemantic::BaseColor: return EditorMaterialTextureSemantic::BaseColor;
        case kb::render::RenderTextureAssetSemantic::Normal: return EditorMaterialTextureSemantic::Normal;
        case kb::render::RenderTextureAssetSemantic::MetallicRoughness: return EditorMaterialTextureSemantic::MetallicRoughness;
        case kb::render::RenderTextureAssetSemantic::Occlusion: return EditorMaterialTextureSemantic::Occlusion;
        case kb::render::RenderTextureAssetSemantic::Emissive: return EditorMaterialTextureSemantic::Emissive;
        case kb::render::RenderTextureAssetSemantic::Unknown: return EditorMaterialTextureSemantic::Unknown;
        }
        return EditorMaterialTextureSemantic::Unknown;
    }

    [[nodiscard]] static EditorMaterialTextureColorSpace ColorSpaceFromAsset(kb::render::RenderTextureAssetColorSpace colorSpace) noexcept {
        switch (colorSpace) {
        case kb::render::RenderTextureAssetColorSpace::Srgb: return EditorMaterialTextureColorSpace::Srgb;
        case kb::render::RenderTextureAssetColorSpace::Linear: return EditorMaterialTextureColorSpace::Linear;
        case kb::render::RenderTextureAssetColorSpace::Unknown: return EditorMaterialTextureColorSpace::Unknown;
        }
        return EditorMaterialTextureColorSpace::Unknown;
    }
};

} // namespace kb::editor
