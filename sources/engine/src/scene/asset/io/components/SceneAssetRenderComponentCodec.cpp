#include "scene/asset/io/components/SceneAssetRenderComponentCodec.hpp"

#include "scene/asset/io/SceneAssetPrimitiveCodec.hpp"

namespace kb::scene {

bool SceneAssetRenderComponentCodec::ReadMeshRenderer(SceneAssetBinaryIO::ByteReader& input, MeshRendererComponent& output) {
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
    return input.ReadBool(output.castsShadow) && input.ReadBool(output.receivesShadow);
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
}

bool SceneAssetRenderComponentCodec::ReadLight(SceneAssetBinaryIO::ByteReader& input, LightComponent& output) {
    std::uint32_t kind = 0;
    bool castsShadow = true;
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
        !input.ReadBool(castsShadow)) {
        return false;
    }
    output.kind = static_cast<LightKind>(kind);
    output.castsShadow = castsShadow;
    return true;
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
}

} // namespace kb::scene
