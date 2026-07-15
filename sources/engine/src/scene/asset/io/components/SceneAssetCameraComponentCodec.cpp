#include "scene/asset/io/components/SceneAssetCameraComponentCodec.hpp"

namespace kb::scene {

bool SceneAssetCameraComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, CameraComponent& output) {
    std::uint32_t projection = 0;
    bool primary = false;
    std::uint32_t clearMode = 0;
    if (!input.ReadUInt32(projection) ||
        projection > static_cast<std::uint32_t>(CameraProjection::Orthographic) ||
        !input.ReadFloat(output.verticalFovDegrees) ||
        !input.ReadFloat(output.orthographicHeight) ||
        !input.ReadFloat(output.nearClip) ||
        !input.ReadFloat(output.farClip) ||
        !input.ReadBool(primary) ||
        !input.ReadUInt32(output.viewportId) ||
        !input.ReadInt32(output.priority) ||
        !input.ReadUInt32(output.cullingMask) ||
        !input.ReadUInt32(clearMode) ||
        clearMode > static_cast<std::uint32_t>(CameraClearMode::DontClear) ||
        !input.ReadFloat(output.clearColor.x) ||
        !input.ReadFloat(output.clearColor.y) ||
        !input.ReadFloat(output.clearColor.z)) {
        return false;
    }
    output.projection = static_cast<CameraProjection>(projection);
    output.primary = primary;
    output.clearMode = static_cast<CameraClearMode>(clearMode);
    return true;
}

void SceneAssetCameraComponentCodec::Write(std::vector<std::uint8_t>& output, const CameraComponent& camera) {
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(camera.projection));
    SceneAssetBinaryIO::WriteFloat(output, camera.verticalFovDegrees);
    SceneAssetBinaryIO::WriteFloat(output, camera.orthographicHeight);
    SceneAssetBinaryIO::WriteFloat(output, camera.nearClip);
    SceneAssetBinaryIO::WriteFloat(output, camera.farClip);
    SceneAssetBinaryIO::WriteUInt8(output, camera.primary ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt32(output, camera.viewportId);
    SceneAssetBinaryIO::WriteInt32(output, camera.priority);
    SceneAssetBinaryIO::WriteUInt32(output, camera.cullingMask);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(camera.clearMode));
    SceneAssetBinaryIO::WriteFloat(output, camera.clearColor.x);
    SceneAssetBinaryIO::WriteFloat(output, camera.clearColor.y);
    SceneAssetBinaryIO::WriteFloat(output, camera.clearColor.z);
}

} // namespace kb::scene
