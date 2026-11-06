#include "scene/asset/io/components/SceneAssetRenderComponentCodec.hpp"

#include "engine/scene/SceneDocument.hpp"
#include "scene/asset/io/SceneAssetPrimitiveCodec.hpp"

namespace kb::scene {

bool SceneAssetRenderComponentCodec::ReadMeshRenderer(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, MeshRendererComponent& output) {
    if (!input.ReadUInt64(output.meshAssetId) ||
        !input.ReadUInt64(output.materialAssetId) ||
        !input.ReadUInt32(output.materialSlotOverrideCount) ||
        output.materialSlotOverrideCount > kMaxMeshRendererMaterialSlotOverrides) {
        return false;
    }
    for (std::uint64_t& materialSlotAssetId : output.materialSlotAssetIds) {
        if (!input.ReadUInt64(materialSlotAssetId)) {
            return false;
        }
    }
    if (!input.ReadBool(output.castsShadow) || !input.ReadBool(output.receivesShadow) || !input.ReadUInt32(output.layer)) {
        return false;
    }
    if (fileVersion >= SceneDocument::MeshLodAndShadowQualityFileVersion) {
        std::uint32_t mobility = 0U;
        if (!input.ReadInt32(output.lodBias) || !input.ReadBool(output.lodEnabled) || !input.ReadUInt32(mobility) ||
            mobility > static_cast<std::uint32_t>(MeshMobility::Static)) {
            return false;
        }
        output.mobility = static_cast<MeshMobility>(mobility);
    }
    return IsMeshRendererComponentValid(output);
}

void SceneAssetRenderComponentCodec::WriteMeshRenderer(std::vector<std::uint8_t>& output, const MeshRendererComponent& meshRenderer) {
    SceneAssetBinaryIO::WriteUInt64(output, meshRenderer.meshAssetId);
    SceneAssetBinaryIO::WriteUInt64(output, meshRenderer.materialAssetId);
    SceneAssetBinaryIO::WriteUInt32(output, meshRenderer.materialSlotOverrideCount);
    for (const std::uint64_t materialSlotAssetId : meshRenderer.materialSlotAssetIds) {
        SceneAssetBinaryIO::WriteUInt64(output, materialSlotAssetId);
    }
    SceneAssetBinaryIO::WriteUInt8(output, meshRenderer.castsShadow ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, meshRenderer.receivesShadow ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt32(output, meshRenderer.layer);
    SceneAssetBinaryIO::WriteInt32(output, meshRenderer.lodBias);
    SceneAssetBinaryIO::WriteUInt8(output, meshRenderer.lodEnabled ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(meshRenderer.mobility));
}

bool SceneAssetRenderComponentCodec::ReadLight(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, LightComponent& output) {
    std::uint32_t kind = 0;
    bool castsShadow = true;
    bool useColorTemperature = false;
    if (!input.ReadUInt32(kind) ||
        kind > static_cast<std::uint32_t>(LightKind::Tube) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.color) ||
        !input.ReadFloat(output.intensity) ||
        !input.ReadFloat(output.range) ||
        !input.ReadFloat(output.innerConeDegrees) ||
        !input.ReadFloat(output.outerConeDegrees) ||
        !input.ReadFloat(output.areaWidth) ||
        !input.ReadFloat(output.areaHeight) ||
        !input.ReadFloat(output.contactShadowLength) ||
        !input.ReadFloat(output.volumetricScattering) ||
        !input.ReadBool(castsShadow) ||
        !input.ReadBool(useColorTemperature) ||
        !input.ReadFloat(output.colorTemperatureKelvin) ||
        !input.ReadUInt32(output.layerMask)) {
        return false;
    }
    if (fileVersion >= SceneDocument::LightShadowSettingsFileVersion) {
        std::uint32_t mobility = 0U;
        if (!input.ReadFloat(output.sourceRadius) || !input.ReadFloat(output.shadowStrength) ||
            !input.ReadFloat(output.shadowDistance) || !input.ReadUInt32(mobility) ||
            mobility > static_cast<std::uint32_t>(LightMobility::Static)) {
            return false;
        }
        output.mobility = static_cast<LightMobility>(mobility);
    }
    if (fileVersion >= SceneDocument::MeshLodAndShadowQualityFileVersion) {
        std::uint32_t filter = 0U;
        std::uint32_t quality = 0U;
        if (!input.ReadFloat(output.shadowBias) || !input.ReadFloat(output.shadowNormalBias) || !input.ReadUInt32(filter) ||
            !input.ReadUInt32(quality) || !input.ReadUInt32(output.shadowCascadeCount) || !input.ReadFloat(output.shadowCascadeDistribution) ||
            filter > static_cast<std::uint32_t>(LightShadowFilter::Pcss) || quality > static_cast<std::uint32_t>(LightShadowQuality::Ultra)) {
            return false;
        }
        output.shadowFilter = static_cast<LightShadowFilter>(filter);
        output.shadowQuality = static_cast<LightShadowQuality>(quality);
    }
    output.kind = static_cast<LightKind>(kind);
    output.castsShadow = castsShadow;
    output.useColorTemperature = useColorTemperature;
    return IsLightComponentValid(output);
}

void SceneAssetRenderComponentCodec::WriteLight(std::vector<std::uint8_t>& output, const LightComponent& light) {
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(light.kind));
    SceneAssetPrimitiveCodec::WriteVec3(output, light.color);
    SceneAssetBinaryIO::WriteFloat(output, light.intensity);
    SceneAssetBinaryIO::WriteFloat(output, light.range);
    SceneAssetBinaryIO::WriteFloat(output, light.innerConeDegrees);
    SceneAssetBinaryIO::WriteFloat(output, light.outerConeDegrees);
    SceneAssetBinaryIO::WriteFloat(output, light.areaWidth);
    SceneAssetBinaryIO::WriteFloat(output, light.areaHeight);
    SceneAssetBinaryIO::WriteFloat(output, light.contactShadowLength);
    SceneAssetBinaryIO::WriteFloat(output, light.volumetricScattering);
    SceneAssetBinaryIO::WriteUInt8(output, light.castsShadow ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, light.useColorTemperature ? 1U : 0U);
    SceneAssetBinaryIO::WriteFloat(output, light.colorTemperatureKelvin);
    SceneAssetBinaryIO::WriteUInt32(output, light.layerMask);
    SceneAssetBinaryIO::WriteFloat(output, light.sourceRadius);
    SceneAssetBinaryIO::WriteFloat(output, light.shadowStrength);
    SceneAssetBinaryIO::WriteFloat(output, light.shadowDistance);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(light.mobility));
    SceneAssetBinaryIO::WriteFloat(output, light.shadowBias);
    SceneAssetBinaryIO::WriteFloat(output, light.shadowNormalBias);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(light.shadowFilter));
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(light.shadowQuality));
    SceneAssetBinaryIO::WriteUInt32(output, light.shadowCascadeCount);
    SceneAssetBinaryIO::WriteFloat(output, light.shadowCascadeDistribution);
}

} // namespace kb::scene
