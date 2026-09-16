#pragma once

#include "engine/ui/UIComponentSet.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetUIComponentCodec final {
public:
    SceneAssetUIComponentCodec() = delete;

    // `fileVersion` is the scene document version the bytes came from: UI component payloads
    // are fixed-layout, so a field added to a component is only present from the version that
    // introduced it and older files stop at the previous field.
    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion,
                                   UIComponentSet& output);
    static void Write(std::vector<std::uint8_t>& output, const UIComponentSet& components);
};

} // namespace kb::scene
