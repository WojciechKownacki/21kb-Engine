#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"

#include "kb/render/resources/RenderMaterialAssetWriter.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::render {
namespace {

[[nodiscard]] bool IsMaterialDocumentHeaderLine(std::string_view line) noexcept {
    return line.empty() ||
        line == "# KB material" ||
        line.rfind("version ", 0U) == 0U;
}

void WriteOverrideMaterialBody(std::ostream& output, const RenderMaterialAssetData& overrides) {
    std::ostringstream materialDocument;
    RenderMaterialAssetWriter::Write(materialDocument, overrides);

    std::istringstream input{ materialDocument.str() };
    std::string line;
    while (std::getline(input, line)) {
        if (IsMaterialDocumentHeaderLine(line)) {
            continue;
        }
        output << line << '\n';
    }
}

void WriteFloat(std::ostream& output, float value) {
    output << std::setprecision(9) << value;
}

void WriteBasePropertyOverrides(std::ostream& output, const RenderMaterialInstanceBasePropertyOverrides& overrides) {
    if (overrides.overrideBlendMode) {
        output << "bOverride_blendMode true\n";
        output << "blendMode " << RenderMaterialGraphBlendModeName(overrides.blendMode) << '\n';
    }
    if (overrides.overrideShadingModel) {
        output << "bOverride_shadingModel true\n";
        output << "shadingModel " << RenderMaterialShadingModelName(overrides.shadingModel) << '\n';
    }
    if (overrides.overrideTwoSided) {
        output << "bOverride_twoSided true\n";
        output << "twoSided " << (overrides.twoSided ? "true" : "false") << '\n';
    }
    if (overrides.overrideOpacityMaskClip) {
        output << "bOverride_opacityMaskClip true\n";
        output << "opacityMaskClip ";
        WriteFloat(output, overrides.opacityMaskClip);
        output << '\n';
    }
    if (overrides.overrideDomain) {
        output << "bOverride_domain true\n";
        output << "domain " << RenderMaterialDomainName(overrides.domain) << '\n';
    }
}

void WriteStaticParameterOverrides(
    std::ostream& output,
    const std::vector<RenderMaterialInstanceStaticParameterOverride>& overrides) {
    for (const RenderMaterialInstanceStaticParameterOverride& overrideValue : overrides) {
        if (overrideValue.stableId.empty() || overrideValue.value.empty()) {
            continue;
        }
        output << "staticOverride " << overrideValue.stableId << ' '
               << RenderMaterialGraphNodeKindName(overrideValue.nodeKind) << ' '
               << overrideValue.value << '\n';
    }
}

} // namespace

bool RenderMaterialInstanceAssetWriter::Save(const std::filesystem::path& path, const RenderMaterialInstanceAssetData& asset) {
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    const std::filesystem::path tmpPath = path.string() + ".tmp";
    {
        std::ofstream output{ tmpPath, std::ios::trunc | std::ios::binary };
        if (!output) {
            return false;
        }
        Write(output, asset);
        output.flush();
        if (!output) {
            return false;
        }
    }

    std::filesystem::rename(tmpPath, path, error);
    if (error) {
        error.clear();
        std::filesystem::copy_file(tmpPath, path, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            return false;
        }
        std::filesystem::remove(tmpPath, error);
    }
    return !error;
}

void RenderMaterialInstanceAssetWriter::Write(std::ostream& output, const RenderMaterialInstanceAssetData& asset) {
    output << "# KB material instance\n";
    output << "version " << (asset.documentVersion == 0U ? kRenderMaterialInstanceAssetDocumentVersion : asset.documentVersion) << '\n';
    output << "parentMaterialAssetId " << asset.parentMaterialAssetId.value << '\n';
    WriteStaticParameterOverrides(output, asset.staticParameterOverrides);
    WriteBasePropertyOverrides(output, asset.basePropertyOverrides);
    if (asset.hasOverrides) {
        WriteOverrideMaterialBody(output, asset.overrides);
    }
}

} // namespace kb::render
