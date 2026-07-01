#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"

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

[[nodiscard]] RenderMaterialGraphUniformBindingType UniformBindingTypeForKind(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterVector:
        return RenderMaterialGraphUniformBindingType::Vector;
    case RenderMaterialGraphNodeKind::ParameterColor:
        return RenderMaterialGraphUniformBindingType::Color;
    default:
        return RenderMaterialGraphUniformBindingType::Scalar;
    }
}

[[nodiscard]] RenderTextureColorSpace TextureBindingColorSpace(RenderMaterialTextureColorSpace colorSpace) noexcept {
    return colorSpace == RenderMaterialTextureColorSpace::Srgb
        ? RenderTextureColorSpace::Srgb
        : RenderTextureColorSpace::Linear;
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
    binding.requiredVaryings = shader.reflection.requiredVaryings;
    binding.usesSceneDepth = shader.reflection.usesSceneDepth;

    // MAT-38/#25d: resolve the scene render state from the graph blend mode so the scene submits a
    // translucent graph material in the transparent pass with the matching blend equation.
    switch (shader.reflection.blendMode) {
    case RenderMaterialGraphBlendMode::Opaque:
        binding.alphaMode = RenderMaterialAlphaMode::Opaque;
        break;
    case RenderMaterialGraphBlendMode::Masked:
        binding.alphaMode = RenderMaterialAlphaMode::Mask;
        break;
    case RenderMaterialGraphBlendMode::Translucent:
        binding.alphaMode = RenderMaterialAlphaMode::Blend;
        binding.translucencyBlend = RenderMaterialTranslucencyBlend::Alpha;
        break;
    case RenderMaterialGraphBlendMode::Additive:
        binding.alphaMode = RenderMaterialAlphaMode::Blend;
        binding.translucencyBlend = RenderMaterialTranslucencyBlend::Additive;
        break;
    case RenderMaterialGraphBlendMode::Modulate:
        binding.alphaMode = RenderMaterialAlphaMode::Blend;
        binding.translucencyBlend = RenderMaterialTranslucencyBlend::Modulate;
        break;
    case RenderMaterialGraphBlendMode::AlphaComposite:
        binding.alphaMode = RenderMaterialAlphaMode::Blend;
        binding.translucencyBlend = RenderMaterialTranslucencyBlend::PreMultipliedAlpha;
        break;
    case RenderMaterialGraphBlendMode::AlphaHoldout:
        binding.alphaMode = RenderMaterialAlphaMode::Blend;
        binding.translucencyBlend = RenderMaterialTranslucencyBlend::AlphaHoldout;
        break;
    }

    binding.uniforms.reserve(shader.reflection.uniforms.size());
    for (const RenderMaterialGraphReflectionUniform& uniform : shader.reflection.uniforms) {
        RenderMaterialGraphUniformBinding uniformBinding{
            .name = uniform.name,
            .stableId = uniform.stableId,
            .type = UniformBindingTypeForKind(uniform.kind),
        };
        if (const RenderMaterialGraphParameterValue* value = FindParameterValue(parameterValues, uniform.stableId)) {
            uniformBinding.value[0] = value->numbers[0];
            uniformBinding.value[1] = value->numbers[1];
            uniformBinding.value[2] = value->numbers[2];
            uniformBinding.value[3] = value->numbers[3];
        }
        binding.uniforms.push_back(std::move(uniformBinding));
    }

    binding.textures.reserve(shader.reflection.textures.size());
    for (const RenderMaterialGraphReflectionTexture& texture : shader.reflection.textures) {
        RenderMaterialGraphTextureBinding textureBinding{
            .samplerName = texture.samplerName,
            .stableId = texture.stableId,
            .slot = texture.slot,
            .colorSpace = TextureBindingColorSpace(texture.colorSpace),
            .samplerFlags = RenderMaterialGraphSamplerBgfxFlags(texture.samplerState),
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
        binding.textures.push_back(std::move(textureBinding));
    }

    return result;
}

} // namespace kb::render
