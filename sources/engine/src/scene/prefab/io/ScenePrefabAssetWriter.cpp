#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetTemplateWriter.hpp"
#include "scene/prefab/io/ScenePrefabAssetVariantWriter.hpp"

#include <fstream>

namespace kb::scene {
namespace {

[[nodiscard]] bool PrepareOutputPath(const std::filesystem::path& path) {
    if (!path.has_parent_path()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error;
}

[[nodiscard]] bool CanWrite(const ScenePrefabAssetWriteDesc& asset) {
    if (asset.name.empty() || asset.guid.empty()) {
        return false;
    }
    return asset.kind == ScenePrefabAssetKind::Variant ? ScenePrefabAssetVariantWriter::CanWrite(asset) : ScenePrefabAssetTemplateWriter::CanWrite(asset);
}

void WriteHeader(std::ostream& output, const ScenePrefabAssetWriteDesc& asset) {
    output << ScenePrefabAssetFormat::HeaderV2 << '\n';
    output << ScenePrefabAssetFormat::KindKey << '=' << (asset.kind == ScenePrefabAssetKind::Template ? ScenePrefabAssetFormat::TemplateKind : ScenePrefabAssetFormat::VariantKind) << '\n';
    output << ScenePrefabAssetFormat::GuidKey << '=' << ScenePrefabAssetEscaper::Escape(asset.guid) << '\n';
    output << ScenePrefabAssetFormat::NameKey << '=' << ScenePrefabAssetEscaper::Escape(asset.name) << '\n';
}

void WriteBody(std::ostream& output, const ScenePrefabAssetWriteDesc& asset) {
    if (asset.kind == ScenePrefabAssetKind::Variant) {
        ScenePrefabAssetVariantWriter::WriteBody(output, asset);
        return;
    }
    ScenePrefabAssetTemplateWriter::WriteBody(output, asset);
}

} // namespace

bool ScenePrefabAssetWriter::Write(const std::filesystem::path& path, std::string_view name, const ScenePrefab& prefab) {
    return Write(
        path,
        ScenePrefabAssetWriteDesc{
            .kind = ScenePrefabAssetKind::Template,
            .guid = "transient-prefab",
            .name = name,
            .baseGuid = {},
            .prefab = &prefab,
            .overrides = nullptr,
        });
}

bool ScenePrefabAssetWriter::Write(const std::filesystem::path& path, const ScenePrefabAssetWriteDesc& asset) {
    if (!PrepareOutputPath(path) || !CanWrite(asset)) {
        return false;
    }

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        return false;
    }

    WriteHeader(output, asset);
    WriteBody(output, asset);
    return output.good();
}

} // namespace kb::scene
