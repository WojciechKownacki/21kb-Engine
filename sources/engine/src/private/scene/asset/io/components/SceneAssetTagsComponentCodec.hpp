#pragma once

#include "engine/scene/TagsComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetTagsComponentCodec final {
public:
    SceneAssetTagsComponentCodec() = delete;

    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, TagsComponent& output);
    static void Write(std::vector<std::uint8_t>& output, const TagsComponent& tags);
};

} // namespace kb::scene
