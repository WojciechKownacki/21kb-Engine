#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetKeyValueReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetTemplateReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetVariantReader.hpp"

#include <fstream>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] bool ReadV2Header(std::istream& input, ScenePrefabAssetReadResult& result) {
    std::string value;
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::KindKey, value)) {
        return false;
    }
    if (value == ScenePrefabAssetFormat::TemplateKind) {
        result.kind = ScenePrefabAssetKind::Template;
    } else if (value == ScenePrefabAssetFormat::VariantKind) {
        result.kind = ScenePrefabAssetKind::Variant;
    } else {
        return false;
    }

    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::GuidKey, value) || !ScenePrefabAssetKeyValueReader::ReadEscaped(value, result.guid)) {
        return false;
    }
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::NameKey, value) || !ScenePrefabAssetKeyValueReader::ReadEscaped(value, result.name)) {
        return false;
    }

    return true;
}

[[nodiscard]] bool ReadV2(std::istream& input, ScenePrefabAssetReadResult& result) {
    return ReadV2Header(input, result)
        && (result.kind == ScenePrefabAssetKind::Variant
                ? ScenePrefabAssetVariantReader::ReadV2(input, result)
                : ScenePrefabAssetTemplateReader::ReadV2(input, result));
}

} // namespace

bool ScenePrefabAssetReader::Read(const std::filesystem::path& path, ScenePrefabAssetReadResult& output) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }

    ScenePrefabAssetReadResult result;
    std::string line;
    if (!ScenePrefabAssetFieldParser::ReadLine(input, line)) {
        return false;
    }

    const bool read = line == ScenePrefabAssetFormat::Header ? ScenePrefabAssetTemplateReader::ReadLegacy(input, result) : (line == ScenePrefabAssetFormat::HeaderV2 && ReadV2(input, result));
    if (!read) {
        return false;
    }

    if (ScenePrefabAssetFieldParser::ReadLine(input, line)) {
        return false;
    }

    output = std::move(result);
    return true;
}

bool ScenePrefabAssetReader::ReadGuid(const std::filesystem::path& path, std::string& guid) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || line != ScenePrefabAssetFormat::HeaderV2) {
        return false;
    }

    ScenePrefabAssetReadResult header;
    if (!ReadV2Header(input, header) || header.guid.empty()) {
        return false;
    }

    guid = std::move(header.guid);
    return true;
}

} // namespace kb::scene
