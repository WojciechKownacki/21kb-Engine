#include "scene/asset/io/components/SceneAssetTagsComponentCodec.hpp"

#include <string>

namespace kb::scene {

bool SceneAssetTagsComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, TagsComponent& output) {
    std::string tags;
    if (!input.ReadString(tags, TagsComponent::MaxBytes)) {
        return false;
    }
    SetTagsText(output, tags);
    return true;
}

void SceneAssetTagsComponentCodec::Write(std::vector<std::uint8_t>& output, const TagsComponent& tags) {
    SceneAssetBinaryIO::WriteString(output, TagsText(tags));
}

} // namespace kb::scene
