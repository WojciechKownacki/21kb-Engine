#pragma once

#include "engine/ui/UIComponentSet.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <vector>

namespace kb::scene {

class SceneAssetUIComponentCodec final {
public:
    SceneAssetUIComponentCodec() = delete;

    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, UIComponentSet& output);
    static void Write(std::vector<std::uint8_t>& output, const UIComponentSet& components);
};

} // namespace kb::scene
