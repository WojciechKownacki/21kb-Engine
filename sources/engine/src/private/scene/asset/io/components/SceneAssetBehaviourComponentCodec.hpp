#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetBehaviourComponentCodec final {
public:
    SceneAssetBehaviourComponentCodec() = delete;

    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, BehaviourComponent& output);
    static void Write(std::vector<std::uint8_t>& output, const BehaviourComponent& behaviour);
};

} // namespace kb::scene
