#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldWriter.hpp"

#include <fstream>

namespace kb::scene {

bool ScenePrefabAssetWriter::Write(const std::filesystem::path& path, std::string_view name, const ScenePrefab& prefab) {
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        return false;
    }

    output << ScenePrefabAssetFormat::Header << '\n';
    output << ScenePrefabAssetFormat::NameKey << '=' << ScenePrefabAssetEscaper::Escape(name) << '\n';
    output << ScenePrefabAssetFormat::NodesKey << '=' << prefab.NodeCount() << '\n';

    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        ScenePrefabAssetFieldWriter::WriteNode(output, node);
    }

    return output.good();
}

} // namespace kb::scene
