#include "scene/prefab/io/ScenePrefabAssetTagsParser.hpp"

namespace kb::scene {

bool ScenePrefabAssetTagsParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    if (!fields.contains("tags")) {
        return true;
    }

    bool hasTags = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "tags", hasTags)) {
        return false;
    }
    if (!hasTags) {
        return true;
    }

    const auto iterator = fields.find("tags.value");
    if (iterator == fields.end()) {
        return false;
    }
    TagsComponent tags;
    SetTagsText(tags, iterator->second);
    components.tags = tags;
    return true;
}

} // namespace kb::scene
