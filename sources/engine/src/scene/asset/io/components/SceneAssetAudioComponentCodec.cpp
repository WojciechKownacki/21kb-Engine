#include "scene/asset/io/components/SceneAssetAudioComponentCodec.hpp"

namespace kb::scene {

bool SceneAssetAudioComponentCodec::ReadSource(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, AudioSourceComponent& output) {
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
    if (fileVersion < 2U) {
        return true;
    }

    bool enabled = true;
    bool mute = false;
    std::uint32_t attenuationModel = static_cast<std::uint32_t>(kb::audio::AudioAttenuationModel::Inverse);
    if (!input.ReadBool(enabled) ||
        !input.ReadBool(mute) ||
        !input.ReadFloat(output.pan) ||
        !input.ReadFloat(output.spatialBlend) ||
        !input.ReadUInt32(attenuationModel) ||
        !input.ReadFloat(output.minDistance) ||
        !input.ReadFloat(output.maxDistance) ||
        !input.ReadFloat(output.rolloff) ||
        !input.ReadFloat(output.dopplerFactor)) {
        return false;
    }
    output.enabled = enabled;
    output.mute = mute;
    output.attenuationModel = static_cast<kb::audio::AudioAttenuationModel>(attenuationModel);
    return true;
}

void SceneAssetAudioComponentCodec::WriteSource(std::vector<std::uint8_t>& output, const AudioSourceComponent& audioSource) {
    SceneAssetBinaryIO::WriteUInt64(output, audioSource.clipAssetId);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.volume);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.pitch);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.loop ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.spatial ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.autoplay ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.enabled ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, audioSource.mute ? 1U : 0U);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.pan);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.spatialBlend);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(audioSource.attenuationModel));
    SceneAssetBinaryIO::WriteFloat(output, audioSource.minDistance);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.maxDistance);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.rolloff);
    SceneAssetBinaryIO::WriteFloat(output, audioSource.dopplerFactor);
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
