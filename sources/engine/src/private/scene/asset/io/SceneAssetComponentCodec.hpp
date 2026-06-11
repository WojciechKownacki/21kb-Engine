#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetComponentCodec final {
public:
    SceneAssetComponentCodec() = delete;

    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, ScenePrefabNodeComponents& output);
    static void Write(std::vector<std::uint8_t>& output, const ScenePrefabNodeComponents& components);
};

} // namespace kb::scene
