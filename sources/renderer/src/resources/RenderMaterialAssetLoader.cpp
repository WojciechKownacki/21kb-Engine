#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include "resources/RenderMaterialAssetParser.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::render {

bool RenderMaterialAssetParseResult::Succeeded() const noexcept {
    return asset.has_value() && diagnostics.empty();
}

std::string RenderMaterialAssetParseResult::ErrorMessage() const {
    if (diagnostics.empty()) {
        return {};
    }

    std::ostringstream output;
    output << "Render material asset load failed";
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        output << "; ";
        if (diagnostic.line > 0U) {
            output << "line " << diagnostic.line << ": ";
        }
        output << diagnostic.message;
        if (!diagnostic.text.empty()) {
            output << " [" << diagnostic.text << "]";
        }
    }
    return output.str();
}

std::string_view RenderMaterialAssetLoader::Type() const noexcept {
    return "RenderMaterial";
}

std::type_index RenderMaterialAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialAssetData);
}

std::vector<std::string> RenderMaterialAssetLoader::Extensions() const {
    return { ".kbmat" };
}

kb::assets::AssetLoadResult RenderMaterialAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    RenderMaterialAssetParseResult material = LoadMaterialWithDiagnostics(request.resolvedPath);
    if (!material.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = material.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialAssetData>(*material.asset),
        .error = {},
    };
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::Load(path);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(std::istream& input) {
    return RenderMaterialAssetParser::Parse(input);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::LoadWithDiagnostics(path);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(std::istream& input) {
    return RenderMaterialAssetParser::ParseWithDiagnostics(input);
}

} // namespace kb::render
