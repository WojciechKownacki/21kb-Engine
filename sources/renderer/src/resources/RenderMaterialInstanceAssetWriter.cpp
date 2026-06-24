#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"

#include <filesystem>
#include <fstream>
#include <ostream>

namespace kb::render {

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
}

} // namespace kb::render
