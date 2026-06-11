#pragma once

#include "engine/scene/InputComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetInputComponentCodec final {
public:
    SceneAssetInputComponentCodec() = delete;

    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, InputComponent& output);
    static void Write(std::vector<std::uint8_t>& output, const InputComponent& input);
};

} // namespace kb::scene
