#include "resources/RenderMaterialAssetParser.hpp"

#include "resources/RenderMaterialAssetFieldParser.hpp"

#include <cstddef>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>

namespace kb::render {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

} // namespace

std::optional<RenderMaterialAssetData> RenderMaterialAssetParser::Load(const std::filesystem::path& path) {
    std::ifstream input{ path };
    if (!input) {
        return std::nullopt;
    }
    return Parse(input);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetParser::Parse(std::istream& input) {
    RenderMaterialAssetData asset{};
    bool sawMaterialProperty = false;

    std::string line;
    while (std::getline(input, line)) {
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (!RenderMaterialAssetFieldParser::Apply(keyword, rest, asset)) {
            return std::nullopt;
        }
        sawMaterialProperty = true;
    }

    return sawMaterialProperty ? std::optional<RenderMaterialAssetData>{ asset } : std::nullopt;
}

} // namespace kb::render
