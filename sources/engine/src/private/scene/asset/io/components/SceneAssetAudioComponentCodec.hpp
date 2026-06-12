#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetAudioComponentCodec final {
public:
    SceneAssetAudioComponentCodec() = delete;

    [[nodiscard]] static bool ReadSource(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, AudioSourceComponent& output);
    static void WriteSource(std::vector<std::uint8_t>& output, const AudioSourceComponent& audioSource);

    [[nodiscard]] static bool ReadListener(SceneAssetBinaryIO::ByteReader& input, AudioListenerComponent& output);
    static void WriteListener(std::vector<std::uint8_t>& output, const AudioListenerComponent& audioListener);
};

} // namespace kb::scene
