#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"

#include "engine/assets/AssetMemoryInputStream.hpp"
#include "RenderMaterialAtomicFileWriter.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] std::string MaterialTypeDocumentErrorMessage(const RenderMaterialTypeDocumentParseResult& result) {
    if (result.diagnostics.empty()) {
        return {};
    }
    std::ostringstream output;
    output << "Render material type asset load failed";
    for (const RenderMaterialTypeDocumentDiagnostic& diagnostic : result.diagnostics) {
        output << "; code " << RenderMaterialTypeDocumentDiagnosticCodeName(diagnostic.code) << ": ";
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

} // namespace

std::string_view RenderMaterialTypeAssetLoader::Type() const noexcept {
    return kRenderMaterialTypeAssetType;
}

std::type_index RenderMaterialTypeAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialTypeDocument);
}

std::vector<std::string> RenderMaterialTypeAssetLoader::Extensions() const {
    return { kRenderMaterialTypeAssetExtension };
}

kb::assets::AssetLoadResult RenderMaterialTypeAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    kb::assets::AssetMemoryInputStream input{ sourceBytes };
    RenderMaterialTypeDocumentParseResult result = LoadTypeWithDiagnostics(input);
    if (!result.document.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = MaterialTypeDocumentErrorMessage(result) };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialTypeDocument>(*result.document),
        .error = {},
    };
}

std::optional<RenderMaterialTypeDocument> RenderMaterialTypeAssetLoader::LoadType(const std::filesystem::path& path) {
    RenderMaterialTypeDocumentParseResult result = LoadTypeWithDiagnostics(path);
    return result.document;
}

std::optional<RenderMaterialTypeDocument> RenderMaterialTypeAssetLoader::LoadType(std::istream& input) {
    RenderMaterialTypeDocumentParseResult result = LoadTypeWithDiagnostics(input);
    return result.document;
}

RenderMaterialTypeDocumentParseResult RenderMaterialTypeAssetLoader::LoadTypeWithDiagnostics(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        RenderMaterialTypeDocumentParseResult result{};
        result.diagnostics.push_back(RenderMaterialTypeDocumentDiagnostic{
            .code = RenderMaterialTypeDocumentDiagnosticCode::FileOpenFailed,
            .message = "Material Type asset file could not be opened.",
            .text = path.generic_string(),
        });
        return result;
    }
    return ParseRenderMaterialTypeDocument(input);
}

RenderMaterialTypeDocumentParseResult RenderMaterialTypeAssetLoader::LoadTypeWithDiagnostics(std::istream& input) {
    return ParseRenderMaterialTypeDocument(input);
}

bool RenderMaterialTypeAssetLoader::SaveType(const std::filesystem::path& path, const RenderMaterialTypeDocument& document) {
    return detail::WriteMaterialFileAtomically(path, [&document](std::ostream& output) {
        WriteRenderMaterialTypeDocument(output, document);
    });
}

} // namespace kb::render
