#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetCameraComponentCodec final {
public:
    SceneAssetCameraComponentCodec() = delete;

    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, CameraComponent& output);
    static void Write(std::vector<std::uint8_t>& output, const CameraComponent& camera);
};

} // namespace kb::scene
