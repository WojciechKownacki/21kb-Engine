#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"

namespace kb::render {
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
