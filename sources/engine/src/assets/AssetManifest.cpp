#include "engine/assets/AssetManifest.hpp"

#include "engine/assets/AssetId.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace kb::assets {
namespace {

constexpr std::string_view Header = "kbassetmanifest.v1";

[[nodiscard]] std::string Escape(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (const char value : text) {
        switch (value) {
        case '\\':
            output += "\\\\";
            break;
        case '\t':
            output += "\\t";
            break;
        case '\n':
            output += "\\n";
            break;
        default:
            output += value;
            break;
        }
    }
    return output;
}

[[nodiscard]] bool Unescape(std::string_view text, std::string& output) {
    output.clear();
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\') {
            output += text[index];
            continue;
        }
        if (++index >= text.size()) {
            return false;
        }
        switch (text[index]) {
        case '\\':
            output += '\\';
            break;
        case 't':
            output += '\t';
            break;
        case 'n':
            output += '\n';
            break;
        default:
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::string_view> SplitTabs(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t end = line.find('\t', begin);
        fields.push_back(line.substr(begin, end == std::string_view::npos ? line.size() - begin : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return fields;
}

} // namespace

bool AssetManifest::Save(const std::filesystem::path& path, const AssetRegistry& registry) {
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

    output << Header << '\n';
    for (const AssetMetadata& metadata : registry.All()) {
        output << ToString(metadata.id) << '\t'
               << Escape(metadata.type) << '\t'
               << Escape(metadata.name) << '\t'
               << Escape(NormalizeAssetPath(metadata.virtualPath)) << '\t'
               << Escape(metadata.physicalPath.string()) << '\t'
               << metadata.contentHash << '\t'
               << (metadata.runtimeLoadable ? 1 : 0) << '\n';
    }
    return output.good();
}

bool AssetManifest::Load(const std::filesystem::path& path, AssetRegistry& registry) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    if (!std::getline(input, line) || line != Header) {
        return false;
    }

    AssetRegistry loaded;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 7) {
            return false;
        }

        AssetMetadata metadata;
        std::string virtualPath;
        std::string physicalPath;
        if (!TryParseAssetId(fields[0], metadata.id)
            || !Unescape(fields[1], metadata.type)
            || !Unescape(fields[2], metadata.name)
            || !Unescape(fields[3], virtualPath)
            || !Unescape(fields[4], physicalPath)) {
            return false;
        }

        std::istringstream hashStream{ std::string{ fields[5] } };
        int runtimeLoadable = 0;
        std::istringstream runtimeStream{ std::string{ fields[6] } };
        if (!(hashStream >> metadata.contentHash) || !(runtimeStream >> runtimeLoadable)) {
            return false;
        }

        metadata.virtualPath = std::move(virtualPath);
        metadata.physicalPath = std::move(physicalPath);
        metadata.runtimeLoadable = runtimeLoadable != 0;
        if (!loaded.Upsert(std::move(metadata))) {
            return false;
        }
    }

    registry = std::move(loaded);
    return true;
}

} // namespace kb::assets
