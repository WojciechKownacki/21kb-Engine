#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <sstream>

namespace kb::render {

std::uint32_t RenderMaterialGraphSamplerBgfxFlags(const RenderMaterialGraphSamplerState& state) noexcept {
    std::uint32_t flags = 0U;
    if (state.minFilter == RenderMaterialGraphSamplerFilter::Point) {
        flags |= BGFX_SAMPLER_MIN_POINT;
    }
    if (state.magFilter == RenderMaterialGraphSamplerFilter::Point) {
        flags |= BGFX_SAMPLER_MAG_POINT;
    }
    if (state.mipFilter == RenderMaterialGraphSamplerFilter::Point) {
        flags |= BGFX_SAMPLER_MIP_POINT;
    }
    switch (state.wrapU) {
    case RenderMaterialGraphSamplerWrap::Clamp: flags |= BGFX_SAMPLER_U_CLAMP; break;
    case RenderMaterialGraphSamplerWrap::Mirror: flags |= BGFX_SAMPLER_U_MIRROR; break;
    case RenderMaterialGraphSamplerWrap::Repeat: break;
    }
    switch (state.wrapV) {
    case RenderMaterialGraphSamplerWrap::Clamp: flags |= BGFX_SAMPLER_V_CLAMP; break;
    case RenderMaterialGraphSamplerWrap::Mirror: flags |= BGFX_SAMPLER_V_MIRROR; break;
    case RenderMaterialGraphSamplerWrap::Repeat: break;
    }
    return flags;
}

namespace {

constexpr std::uint64_t kGraphProgramFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kGraphProgramFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kMaterialGraphShaderWrapperVersion = 4ULL;

void HashByte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kGraphProgramFnvPrime;
}

void HashBool(std::uint64_t& hash, bool value) noexcept {
    HashByte(hash, value ? 1U : 0U);
}

void HashU32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashString(std::uint64_t& hash, const std::string& value) noexcept {
    HashU64(hash, static_cast<std::uint64_t>(value.size()));
    for (const char ch : value) {
        HashByte(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    }
}

[[nodiscard]] RenderMaterialAlphaMode AlphaModeForBlendMode(RenderMaterialGraphBlendMode blendMode) noexcept {
    switch (blendMode) {
    case RenderMaterialGraphBlendMode::Opaque:
        return RenderMaterialAlphaMode::Opaque;
    case RenderMaterialGraphBlendMode::Masked:
        return RenderMaterialAlphaMode::Mask;
    case RenderMaterialGraphBlendMode::Translucent:
    case RenderMaterialGraphBlendMode::Additive:
    case RenderMaterialGraphBlendMode::Modulate:
    case RenderMaterialGraphBlendMode::AlphaComposite:
    case RenderMaterialGraphBlendMode::AlphaHoldout:
        return RenderMaterialAlphaMode::Blend;
    }
    return RenderMaterialAlphaMode::Opaque;
}

[[nodiscard]] RenderMaterialTranslucencyBlend TranslucencyBlendForBlendMode(RenderMaterialGraphBlendMode blendMode) noexcept {
    switch (blendMode) {
    case RenderMaterialGraphBlendMode::Additive:
        return RenderMaterialTranslucencyBlend::Additive;
    case RenderMaterialGraphBlendMode::Modulate:
        return RenderMaterialTranslucencyBlend::Modulate;
    case RenderMaterialGraphBlendMode::AlphaComposite:
        return RenderMaterialTranslucencyBlend::PreMultipliedAlpha;
    case RenderMaterialGraphBlendMode::AlphaHoldout:
        return RenderMaterialTranslucencyBlend::AlphaHoldout;
    case RenderMaterialGraphBlendMode::Opaque:
    case RenderMaterialGraphBlendMode::Masked:
    case RenderMaterialGraphBlendMode::Translucent:
        return RenderMaterialTranslucencyBlend::Alpha;
    }
    return RenderMaterialTranslucencyBlend::Alpha;
}

[[nodiscard]] RenderMaterialGraphUniformBindingType UniformBindingTypeForKind(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return RenderMaterialGraphUniformBindingType::Vector;
    case RenderMaterialGraphNodeKind::ParameterColor:
        return RenderMaterialGraphUniformBindingType::Color;
    default:
        return RenderMaterialGraphUniformBindingType::Scalar;
    }
}

[[nodiscard]] RenderMaterialGraphUniformBindingSource UniformBindingSourceForReflection(
    RenderMaterialGraphReflectionUniformSource source) noexcept {
    switch (source) {
    case RenderMaterialGraphReflectionUniformSource::MaterialParameter:
        return RenderMaterialGraphUniformBindingSource::MaterialParameter;
    case RenderMaterialGraphReflectionUniformSource::ParameterCollection:
        return RenderMaterialGraphUniformBindingSource::ParameterCollection;
    }
    return RenderMaterialGraphUniformBindingSource::MaterialParameter;
}

[[nodiscard]] RenderTextureColorSpace TextureBindingColorSpace(RenderMaterialTextureColorSpace colorSpace) noexcept {
    return colorSpace == RenderMaterialTextureColorSpace::Srgb
        ? RenderTextureColorSpace::Srgb
        : RenderTextureColorSpace::Linear;
}

[[nodiscard]] std::string_view MaterialGraphDebugColorSpaceName(RenderMaterialTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderMaterialTextureColorSpace::Srgb: return "Srgb";
    case RenderMaterialTextureColorSpace::Linear: return "Linear";
    case RenderMaterialTextureColorSpace::Unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] std::string_view MaterialGraphDebugRuntimeColorSpaceName(RenderTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderTextureColorSpace::Srgb: return "Srgb";
    case RenderTextureColorSpace::Linear: return "Linear";
    }
    return "Linear";
}

[[nodiscard]] std::string_view MaterialGraphDebugTextureDimensionName(RenderMaterialGraphTextureDimension dimension) noexcept {
    switch (dimension) {
    case RenderMaterialGraphTextureDimension::Texture2D: return "Texture2D";
    case RenderMaterialGraphTextureDimension::TextureCube: return "TextureCube";
    case RenderMaterialGraphTextureDimension::Texture3D: return "Texture3D";
    case RenderMaterialGraphTextureDimension::Texture2DArray: return "Texture2DArray";
    }
    return "Texture2D";
}

[[nodiscard]] const RenderMaterialGraphParameterValue* FindParameterValue(
    std::span<const RenderMaterialGraphParameterValue> values,
    const std::string& stableId) noexcept {
    for (const RenderMaterialGraphParameterValue& value : values) {
        if (value.stableId == stableId) {
            return &value;
        }
    }
    return nullptr;
}

} // namespace

std::uint64_t RenderMaterialGraphVariantKey(const RenderMaterialGraphShaderSource& shader) noexcept {
    std::uint64_t hash = kGraphProgramFnvOffset;
    HashU64(hash, shader.sourceHash);
    HashU64(hash, kMaterialGraphShaderWrapperVersion);
    HashByte(hash, static_cast<std::uint8_t>(shader.reflection.shadingModel));
    HashByte(hash, static_cast<std::uint8_t>(shader.reflection.blendMode));
    HashBool(hash, shader.reflection.hasWorldPositionOffset);
    HashBool(hash, shader.reflection.hasCustomizedUv0);
    HashBool(hash, shader.reflection.hasDisplacement);
    HashBool(hash, shader.reflection.hasTangentOutput);
    HashBool(hash, shader.reflection.usesSceneDepth);
    HashBool(hash, shader.reflection.usesSceneColor);
    HashU64(hash, static_cast<std::uint64_t>(shader.reflection.requiredVaryings.size()));
    for (const std::string& varying : shader.reflection.requiredVaryings) {
        HashString(hash, varying);
    }
    return hash;
}

std::uint64_t RenderMaterialGraphPipelineStateKey(const RenderMaterialGraphShaderSource& shader) noexcept {
    std::uint64_t hash = kGraphProgramFnvOffset;
    HashByte(hash, static_cast<std::uint8_t>(AlphaModeForBlendMode(shader.reflection.blendMode)));
    HashByte(hash, static_cast<std::uint8_t>(TranslucencyBlendForBlendMode(shader.reflection.blendMode)));
    HashByte(hash, static_cast<std::uint8_t>(shader.reflection.shadingModel));
    HashByte(hash, static_cast<std::uint8_t>(shader.reflection.blendMode));
    HashBool(hash, shader.reflection.hasWorldPositionOffset);
    HashBool(hash, shader.reflection.hasCustomizedUv0);
    HashBool(hash, shader.reflection.hasDisplacement);
    HashBool(hash, shader.reflection.hasTangentOutput);
    HashBool(hash, shader.reflection.usesSceneDepth);
    HashBool(hash, shader.reflection.usesSceneColor);
    HashU32(hash, static_cast<std::uint32_t>(shader.reflection.textures.size()));
    HashU32(hash, static_cast<std::uint32_t>(shader.reflection.uniforms.size()));
    return hash;
}

RenderMaterialGraphProgramBindingResult BuildRenderMaterialGraphProgramBinding(
    std::uint64_t materialTypeId,
    std::uint32_t materialTypeVersion,
    const RenderMaterialGraphShaderSource& shader,
    std::span<const RenderMaterialGraphParameterValue> parameterValues) {
    RenderMaterialGraphProgramBindingResult result{};
    RenderMaterialGraphProgramBinding& binding = result.binding;
    binding.active = true;
    binding.materialTypeId = materialTypeId;
    binding.materialTypeVersion = materialTypeVersion;
    binding.graphSourceHash = shader.sourceHash;
    binding.variantKey = RenderMaterialGraphVariantKey(shader);
    binding.pipelineStateKey = RenderMaterialGraphPipelineStateKey(shader);
    binding.requiredVaryings = shader.reflection.requiredVaryings;
    binding.requiresGeneratedVertexShader =
        shader.reflection.hasWorldPositionOffset ||
        shader.reflection.hasCustomizedUv0 ||
        shader.reflection.hasDisplacement;
    binding.usesSceneDepth = shader.reflection.usesSceneDepth;
    binding.usesSceneColor = shader.reflection.usesSceneColor;

    // MAT-38/#25d: resolve the scene render state from the graph blend mode so the scene submits a
    // translucent graph material in the transparent pass with the matching blend equation.
    binding.alphaMode = AlphaModeForBlendMode(shader.reflection.blendMode);
    binding.translucencyBlend = TranslucencyBlendForBlendMode(shader.reflection.blendMode);

    {
        std::ostringstream row;
        row << "build-binding materialTypeId=" << materialTypeId
            << " version=" << materialTypeVersion
            << " sourceHash=" << shader.sourceHash
            << " variantKey=" << binding.variantKey
            << " uniforms=" << shader.reflection.uniforms.size()
            << " textures=" << shader.reflection.textures.size()
            << " requiredVaryings=" << shader.reflection.requiredVaryings.size()
            << " generatedVS=" << (binding.requiresGeneratedVertexShader ? "true" : "false");
        WriteRendererMaterialGraphDebugLog("binding", row.str());
    }

    binding.uniforms.reserve(shader.reflection.uniforms.size());
    for (const RenderMaterialGraphReflectionUniform& uniform : shader.reflection.uniforms) {
        RenderMaterialGraphUniformBinding uniformBinding{
            .name = uniform.name,
            .stableId = uniform.stableId,
            .type = UniformBindingTypeForKind(uniform.kind),
            .source = UniformBindingSourceForReflection(uniform.source),
            .collectionAssetId = uniform.collectionAssetId,
            .collectionParameterStableId = uniform.collectionParameterStableId,
        };
        uniformBinding.value[0] = uniform.defaultValue[0];
        uniformBinding.value[1] = uniform.defaultValue[1];
        uniformBinding.value[2] = uniform.defaultValue[2];
        uniformBinding.value[3] = uniform.defaultValue[3];
        if (uniform.source == RenderMaterialGraphReflectionUniformSource::ParameterCollection) {
            if (const std::optional<RenderMaterialParameterCollectionRuntimeValue> value =
                    GlobalRenderMaterialParameterCollectionStore().Resolve(uniform.collectionAssetId, uniform.collectionParameterStableId)) {
                uniformBinding.value[0] = value->value[0];
                uniformBinding.value[1] = value->value[1];
                uniformBinding.value[2] = value->value[2];
                uniformBinding.value[3] = value->value[3];
            } else {
                result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
                    .severity = RenderMaterialGraphDiagnosticSeverity::Error,
                    .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    .assetId = uniform.collectionAssetId,
                    .pin = uniform.collectionParameterStableId,
                    .message = "Material graph collection parameter '" + uniform.collectionParameterStableId +
                        "' from collection " + std::to_string(uniform.collectionAssetId) + " has no resolved runtime value.",
                });
            }
        } else if (const RenderMaterialGraphParameterValue* value = FindParameterValue(parameterValues, uniform.stableId)) {
            uniformBinding.value[0] = value->numbers[0];
            uniformBinding.value[1] = value->numbers[1];
            uniformBinding.value[2] = value->numbers[2];
            uniformBinding.value[3] = value->numbers[3];
        }
        {
            std::ostringstream row;
            row << "uniform name=" << uniformBinding.name
                << " stableId=" << uniformBinding.stableId
                << " kind=" << RenderMaterialGraphNodeKindName(uniform.kind)
                << " value=(" << uniformBinding.value[0] << "," << uniformBinding.value[1] << ","
                << uniformBinding.value[2] << "," << uniformBinding.value[3] << ")";
            WriteRendererMaterialGraphDebugLog("binding", row.str());
        }
        binding.uniforms.push_back(std::move(uniformBinding));
    }

    binding.textures.reserve(shader.reflection.textures.size());
    for (const RenderMaterialGraphReflectionTexture& texture : shader.reflection.textures) {
        RenderMaterialGraphTextureBinding textureBinding{
            .samplerName = texture.samplerName,
            .stableId = texture.stableId,
            .slot = texture.slot,
            .role = texture.role,
            .colorSpace = TextureBindingColorSpace(texture.colorSpace),
            .samplerFlags = RenderMaterialGraphSamplerBgfxFlags(texture.samplerState),
            .dimension = texture.dimension,
        };
        if (const RenderMaterialGraphParameterValue* value = FindParameterValue(parameterValues, texture.stableId)) {
            textureBinding.textureAssetId = value->assetId;
            textureBinding.resolved = value->assetId != 0U;
        }
        if (!textureBinding.resolved) {
            result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
                .severity = RenderMaterialGraphDiagnosticSeverity::Warning,
                .kind = RenderMaterialGraphDiagnosticKind::MissingTexture,
                .pin = texture.stableId,
                .message = "Material graph texture binding '" + texture.stableId + "' has no resolved texture asset; the runtime must bind a fallback texture.",
            });
        }
        {
            std::ostringstream row;
            row << "texture sampler=" << textureBinding.samplerName
                << " stableId=" << textureBinding.stableId
                << " slot=" << textureBinding.slot
                << " role=" << (textureBinding.role.empty() ? "<empty>" : textureBinding.role)
                << " reflectedColorSpace=" << MaterialGraphDebugColorSpaceName(texture.colorSpace)
                << " runtimeColorSpace=" << MaterialGraphDebugRuntimeColorSpaceName(textureBinding.colorSpace)
                << " dimension=" << MaterialGraphDebugTextureDimensionName(textureBinding.dimension)
                << " assetId=" << textureBinding.textureAssetId
                << " resolved=" << (textureBinding.resolved ? "true" : "false")
                << " samplerFlags=" << textureBinding.samplerFlags;
            WriteRendererMaterialGraphDebugLog("binding", row.str());
        }
        binding.textures.push_back(std::move(textureBinding));
    }

    return result;
}

} // namespace kb::render
