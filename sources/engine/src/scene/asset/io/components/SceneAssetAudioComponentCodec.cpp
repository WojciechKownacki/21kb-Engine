#include "scene/asset/io/components/SceneAssetAudioComponentCodec.hpp"

namespace kb::scene {

bool SceneAssetAudioComponentCodec::ReadSource(SceneAssetBinaryIO::ByteReader& input, AudioSourceComponent& output) {
    bool loop = false;
    bool spatial = true;
    bool autoplay = false;
    if (!input.ReadUInt64(output.clipAssetId) ||
        !input.ReadFloat(output.volume) ||
        !input.ReadFloat(output.pitch) ||
        !input.ReadBool(loop) ||
        !input.ReadBool(spatial) ||
        !input.ReadBool(autoplay)) {
        return false;
    }
    output.loop = loop;
    output.spatial = spatial;
    output.autoplay = autoplay;
    return true;
}

void SceneAssetAudioComponentCodec::WriteSource(std::vector<std::uint8_t>& output, const AudioSourceComponent& audioSource) {
    SceneAssetBinaryIO::WriteUInt64(output, audioSource.clipAssetId);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.volume);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.pitch);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.loop ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.spatial ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.autoplay ? 1U : 0U);
}

bool SceneAssetAudioComponentCodec::ReadListener(SceneAssetBinaryIO::ByteReader& input, AudioListenerComponent& output) {
    bool primary = true;
    bool enabled = true;
    if (!input.ReadBool(primary) ||
        !input.ReadBool(enabled)) {
        return false;
    }
    output.primary = primary;
    output.enabled = enabled;
    return true;
}

void SceneAssetAudioComponentCodec::WriteListener(std::vector<std::uint8_t>& output, const AudioListenerComponent& audioListener) {
    SceneAssetBinaryIO::WriteUInt8(output, audioListener.primary ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioListener.enabled ? 1U : 0U);
}

} // namespace kb::scene
