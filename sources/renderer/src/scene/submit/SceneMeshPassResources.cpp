#include "scene/submit/SceneMeshPassResources.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "scene/submit/SceneMeshMaterialBindingResolver.hpp"

#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] bgfx::TextureHandle CreateFallbackWhiteTexture() {
    const std::uint32_t white = 0xFFFF'FFFFU;
    const bgfx::Memory* memory = bgfx::copy(&white, sizeof(white));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bgfx::TextureHandle CreateFallbackTexture(std::uint32_t rgba) {
    const bgfx::Memory* memory = bgfx::copy(&rgba, sizeof(rgba));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bool IsSelectionPass(MeshPassType pass) noexcept {
    return pass == MeshPassType::SelectionId || pass == MeshPassType::EditorSelection;
}

} // namespace

bool SceneMeshPassResources::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    meshProgram_ = ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_instanced.sc");
    if (!bgfx::isValid(meshProgram_)) {
        return false;
    }
    shadowProgram_ = ShaderLoader::LoadProgram("vs_mesh_shadow_instanced.sc", "fs_mesh_shadow_instanced.sc");
    if (!bgfx::isValid(shadowProgram_)) {
        Shutdown();
        return false;
    }
    selectionProgram_ = ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_selection_instanced.sc");
    if (!bgfx::isValid(selectionProgram_)) {
        Shutdown();
        return false;
    }

    albedoSampler_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    shadowSampler_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    normalSampler_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    metallicRoughnessSampler_ = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
    occlusionSampler_ = bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler);
    emissiveSampler_ = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    materialParamsUniform_ = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
    materialEmissiveUniform_ = bgfx::createUniform("u_materialEmissive", bgfx::UniformType::Vec4);
    materialFlagsUniform_ = bgfx::createUniform("u_materialFlags", bgfx::UniformType::Vec4);
    materialUvTransformUniform_ = bgfx::createUniform("u_materialUvTransform", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    lightDirKindUniform_ = bgfx::createUniform("u_lightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightPositionRangeUniform_ = bgfx::createUniform("u_lightPositionRange", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightSpotUniform_ = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightParamsUniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_environmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_environmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
    shadowViewProjUniform_ = bgfx::createUniform("u_shadowViewProj", bgfx::UniformType::Mat4);
    shadowParamsUniform_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    fallbackWhiteTexture_ = CreateFallbackWhiteTexture();
    fallbackNormalTexture_ = CreateFallbackTexture(0xFFFF'8080U);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }

    return true;
}

void SceneMeshPassResources::Shutdown() {
    if (bgfx::isValid(fallbackNormalTexture_)) {
        bgfx::destroy(fallbackNormalTexture_);
        fallbackNormalTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackWhiteTexture_)) {
        bgfx::destroy(fallbackWhiteTexture_);
        fallbackWhiteTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowParamsUniform_)) {
        bgfx::destroy(shadowParamsUniform_);
        shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowViewProjUniform_)) {
        bgfx::destroy(shadowViewProjUniform_);
        shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentParamsUniform_)) {
        bgfx::destroy(environmentParamsUniform_);
        environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentGroundUniform_)) {
        bgfx::destroy(environmentGroundUniform_);
        environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentZenithUniform_)) {
        bgfx::destroy(environmentZenithUniform_);
        environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ambientColorUniform_)) {
        bgfx::destroy(ambientColorUniform_);
        ambientColorUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightParamsUniform_)) {
        bgfx::destroy(lightParamsUniform_);
        lightParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightSpotUniform_)) {
        bgfx::destroy(lightSpotUniform_);
        lightSpotUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightColorIntensityUniform_)) {
        bgfx::destroy(lightColorIntensityUniform_);
        lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightPositionRangeUniform_)) {
        bgfx::destroy(lightPositionRangeUniform_);
        lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightDirKindUniform_)) {
        bgfx::destroy(lightDirKindUniform_);
        lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(albedoSampler_)) {
        bgfx::destroy(albedoSampler_);
        albedoSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowSampler_)) {
        bgfx::destroy(shadowSampler_);
        shadowSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(cameraPositionUniform_)) {
        bgfx::destroy(cameraPositionUniform_);
        cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialFlagsUniform_)) {
        bgfx::destroy(materialFlagsUniform_);
        materialFlagsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialUvTransformUniform_)) {
        bgfx::destroy(materialUvTransformUniform_);
        materialUvTransformUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialEmissiveUniform_)) {
        bgfx::destroy(materialEmissiveUniform_);
        materialEmissiveUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialParamsUniform_)) {
        bgfx::destroy(materialParamsUniform_);
        materialParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(emissiveSampler_)) {
        bgfx::destroy(emissiveSampler_);
        emissiveSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(occlusionSampler_)) {
        bgfx::destroy(occlusionSampler_);
        occlusionSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(metallicRoughnessSampler_)) {
        bgfx::destroy(metallicRoughnessSampler_);
        metallicRoughnessSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(normalSampler_)) {
        bgfx::destroy(normalSampler_);
        normalSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(meshProgram_)) {
        bgfx::destroy(meshProgram_);
        meshProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(selectionProgram_)) {
        bgfx::destroy(selectionProgram_);
        selectionProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowProgram_)) {
        bgfx::destroy(shadowProgram_);
        shadowProgram_ = BGFX_INVALID_HANDLE;
    }
}

bool SceneMeshPassResources::IsInitialized() const noexcept {
    return bgfx::isValid(meshProgram_) &&
        bgfx::isValid(shadowProgram_) &&
        bgfx::isValid(selectionProgram_) &&
        bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(shadowSampler_) &&
        bgfx::isValid(normalSampler_) &&
        bgfx::isValid(metallicRoughnessSampler_) &&
        bgfx::isValid(occlusionSampler_) &&
        bgfx::isValid(emissiveSampler_) &&
        bgfx::isValid(materialParamsUniform_) &&
        bgfx::isValid(materialEmissiveUniform_) &&
        bgfx::isValid(materialFlagsUniform_) &&
        bgfx::isValid(materialUvTransformUniform_) &&
        bgfx::isValid(cameraPositionUniform_) &&
        bgfx::isValid(lightDirKindUniform_) &&
        bgfx::isValid(lightPositionRangeUniform_) &&
        bgfx::isValid(lightColorIntensityUniform_) &&
        bgfx::isValid(lightSpotUniform_) &&
        bgfx::isValid(lightParamsUniform_) &&
        bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) &&
        bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_) &&
        bgfx::isValid(shadowViewProjUniform_) &&
        bgfx::isValid(shadowParamsUniform_) &&
        bgfx::isValid(fallbackWhiteTexture_) &&
        bgfx::isValid(fallbackNormalTexture_);
}

bgfx::ProgramHandle SceneMeshPassResources::Bind(const SceneMeshPassBindDesc& desc) const noexcept {
    const RenderMaterialResource* material = desc.command.materialResource;
    if (IsSelectionPass(desc.pass)) {
        const std::array<float, 16> disabledShadowViewProj{};
        bgfx::setUniform(shadowViewProjUniform_, disabledShadowViewProj.data());
        return selectionProgram_;
    }

    const SceneMeshMaterialBindingFallbacks fallbacks{
        .whiteTexture = fallbackWhiteTexture_,
        .normalTexture = fallbackNormalTexture_,
    };
    if (desc.pass == MeshPassType::ShadowDepth) {
        const SceneMeshShadowMaterialBinding materialBinding = SceneMeshMaterialBindingResolver::ResolveShadow(
            material,
            desc.resources,
            desc.resourceMap,
            fallbacks);
        bgfx::setTexture(0U, albedoSampler_, materialBinding.albedoTexture);
        bgfx::setUniform(materialParamsUniform_, materialBinding.params.data());
        bgfx::setUniform(materialFlagsUniform_, materialBinding.flags.data());
        bgfx::setUniform(materialUvTransformUniform_, materialBinding.uvTransform.data());
        return shadowProgram_;
    }

    const SceneMeshMaterialBinding materialBinding = SceneMeshMaterialBindingResolver::Resolve(
        material,
        desc.resources,
        desc.resourceMap,
        fallbacks);
    bgfx::setTexture(0U, albedoSampler_, materialBinding.albedoTexture);
    bgfx::setUniform(materialParamsUniform_, materialBinding.params.data());
    bgfx::setUniform(materialFlagsUniform_, materialBinding.flags.data());
    bgfx::setUniform(materialUvTransformUniform_, materialBinding.uvTransform.data());

    const std::array<float, 4> disabledShadowParams{};
    bgfx::setTexture(1U, normalSampler_, materialBinding.normalTexture);
    bgfx::setTexture(2U, metallicRoughnessSampler_, materialBinding.metallicRoughnessTexture);
    bgfx::setTexture(3U, occlusionSampler_, materialBinding.occlusionTexture);
    bgfx::setTexture(4U, emissiveSampler_, materialBinding.emissiveTexture);
    bgfx::setTexture(5U, shadowSampler_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->depthTexture : fallbackWhiteTexture_);
    bgfx::setUniform(materialEmissiveUniform_, materialBinding.emissive.data());
    bgfx::setUniform(cameraPositionUniform_, desc.cameraPosition.data());
    bgfx::setUniform(lightDirKindUniform_, desc.lighting.dirKind.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightPositionRangeUniform_, desc.lighting.positionRange.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightColorIntensityUniform_, desc.lighting.colorIntensity.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightSpotUniform_, desc.lighting.spot.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightParamsUniform_, desc.lighting.params.data());
    bgfx::setUniform(ambientColorUniform_, desc.lighting.ambient.data());
    bgfx::setUniform(environmentZenithUniform_, desc.lighting.environmentZenith.data());
    bgfx::setUniform(environmentGroundUniform_, desc.lighting.environmentGround.data());
    bgfx::setUniform(environmentParamsUniform_, desc.lighting.environmentParams.data());
    bgfx::setUniform(shadowViewProjUniform_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->lightViewProjection.data() : disabledShadowParams.data());
    bgfx::setUniform(shadowParamsUniform_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->params.data() : disabledShadowParams.data());
    return meshProgram_;
}

} // namespace kb::render
