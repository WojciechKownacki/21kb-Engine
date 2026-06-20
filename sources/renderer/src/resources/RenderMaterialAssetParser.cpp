#include "resources/RenderMaterialAssetParser.hpp"

#include "resources/RenderMaterialAssetFieldParser.hpp"

#include <cstddef>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    return LoadWithDiagnostics(path).asset;
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetParser::Parse(std::istream& input) {
    return ParseWithDiagnostics(input).asset;
}

RenderMaterialAssetParseResult RenderMaterialAssetParser::LoadWithDiagnostics(const std::filesystem::path& path) {
    std::ifstream input{ path };
    if (!input) {
        return RenderMaterialAssetParseResult{
            .asset = std::nullopt,
            .diagnostics = {
                RenderMaterialAssetParseDiagnostic{
                    .line = 0U,
                    .field = {},
                    .message = "Material file could not be opened: " + path.generic_string(),
                    .text = {},
                },
            },
        };
    }
    return ParseWithDiagnostics(input);
}

RenderMaterialAssetParseResult RenderMaterialAssetParser::ParseWithDiagnostics(std::istream& input) {
    RenderMaterialAssetData asset{};
    std::vector<RenderMaterialAssetParseDiagnostic> diagnostics;
    bool sawMaterialProperty = false;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (!RenderMaterialAssetFieldParser::Apply(keyword, rest, asset)) {
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .line = lineNumber,
                .field = std::string{ keyword },
                .message = RenderMaterialAssetFieldParser::IsKnown(keyword)
                    ? "Invalid value for material field '" + std::string{ keyword } + "'."
                    : "Unknown material field '" + std::string{ keyword } + "'.",
                .text = std::string{ trimmed },
            });
            continue;
        }
        sawMaterialProperty = true;
    }

    if (!sawMaterialProperty && diagnostics.empty()) {
        diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .line = 0U,
            .field = {},
            .message = "Material asset does not contain any material properties.",
            .text = {},
        });
    }

    return RenderMaterialAssetParseResult{
        .asset = diagnostics.empty() && sawMaterialProperty ? std::optional<RenderMaterialAssetData>{ asset } : std::nullopt,
        .diagnostics = std::move(diagnostics),
    };
}

} // namespace kb::render
