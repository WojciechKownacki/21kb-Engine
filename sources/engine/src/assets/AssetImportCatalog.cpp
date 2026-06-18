#include "engine/assets/AssetImportCatalog.hpp"

#include "assets/AssetPathUtilities.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace kb::assets {
namespace {

struct ExtensionCategory {
    std::string_view extension;
    AssetImportCategory category = AssetImportCategory::Unknown;
};

constexpr std::array KnownExtensions{
    ExtensionCategory{ ".fbx", AssetImportCategory::Model },
    ExtensionCategory{ ".gltf", AssetImportCategory::Model },
    ExtensionCategory{ ".glb", AssetImportCategory::Model },
    ExtensionCategory{ ".obj", AssetImportCategory::Model },
    ExtensionCategory{ ".dae", AssetImportCategory::Model },
    ExtensionCategory{ ".3ds", AssetImportCategory::Model },
    ExtensionCategory{ ".blend", AssetImportCategory::Model },
    ExtensionCategory{ ".max", AssetImportCategory::Model },
    ExtensionCategory{ ".ma", AssetImportCategory::Model },
    ExtensionCategory{ ".mb", AssetImportCategory::Model },
    ExtensionCategory{ ".usd", AssetImportCategory::Model },
    ExtensionCategory{ ".usda", AssetImportCategory::Model },
    ExtensionCategory{ ".usdc", AssetImportCategory::Model },
    ExtensionCategory{ ".usdz", AssetImportCategory::Model },
    ExtensionCategory{ ".abc", AssetImportCategory::Model },
    ExtensionCategory{ ".stl", AssetImportCategory::Model },
    ExtensionCategory{ ".ply", AssetImportCategory::Model },
    ExtensionCategory{ ".x", AssetImportCategory::Model },
    ExtensionCategory{ ".lwo", AssetImportCategory::Model },
    ExtensionCategory{ ".c4d", AssetImportCategory::Model },
    ExtensionCategory{ ".ase", AssetImportCategory::Model },
    ExtensionCategory{ ".smd", AssetImportCategory::Model },
    ExtensionCategory{ ".png", AssetImportCategory::Texture },
    ExtensionCategory{ ".jpg", AssetImportCategory::Texture },
    ExtensionCategory{ ".jpeg", AssetImportCategory::Texture },
    ExtensionCategory{ ".tga", AssetImportCategory::Texture },
    ExtensionCategory{ ".bmp", AssetImportCategory::Texture },
    ExtensionCategory{ ".dds", AssetImportCategory::Texture },
    ExtensionCategory{ ".hdr", AssetImportCategory::Texture },
    ExtensionCategory{ ".exr", AssetImportCategory::Texture },
    ExtensionCategory{ ".ktx", AssetImportCategory::Texture },
    ExtensionCategory{ ".ktx2", AssetImportCategory::Texture },
    ExtensionCategory{ ".basis", AssetImportCategory::Texture },
    ExtensionCategory{ ".webp", AssetImportCategory::Texture },
    ExtensionCategory{ ".tif", AssetImportCategory::Texture },
    ExtensionCategory{ ".tiff", AssetImportCategory::Texture },
    ExtensionCategory{ ".psd", AssetImportCategory::Texture },
    ExtensionCategory{ ".svg", AssetImportCategory::Texture },
    ExtensionCategory{ ".ico", AssetImportCategory::Texture },
    ExtensionCategory{ ".gif", AssetImportCategory::Texture },
    ExtensionCategory{ ".wav", AssetImportCategory::Audio },
    ExtensionCategory{ ".ogg", AssetImportCategory::Audio },
    ExtensionCategory{ ".mp3", AssetImportCategory::Audio },
    ExtensionCategory{ ".flac", AssetImportCategory::Audio },
    ExtensionCategory{ ".aac", AssetImportCategory::Audio },
    ExtensionCategory{ ".m4a", AssetImportCategory::Audio },
    ExtensionCategory{ ".wma", AssetImportCategory::Audio },
    ExtensionCategory{ ".aiff", AssetImportCategory::Audio },
    ExtensionCategory{ ".aif", AssetImportCategory::Audio },
    ExtensionCategory{ ".xm", AssetImportCategory::Audio },
    ExtensionCategory{ ".mod", AssetImportCategory::Audio },
    ExtensionCategory{ ".s3m", AssetImportCategory::Audio },
    ExtensionCategory{ ".it", AssetImportCategory::Audio },
    ExtensionCategory{ ".mid", AssetImportCategory::Audio },
    ExtensionCategory{ ".midi", AssetImportCategory::Audio },
    ExtensionCategory{ ".mp4", AssetImportCategory::Video },
    ExtensionCategory{ ".mov", AssetImportCategory::Video },
    ExtensionCategory{ ".avi", AssetImportCategory::Video },
    ExtensionCategory{ ".mkv", AssetImportCategory::Video },
    ExtensionCategory{ ".webm", AssetImportCategory::Video },
    ExtensionCategory{ ".ogv", AssetImportCategory::Video },
    ExtensionCategory{ ".wmv", AssetImportCategory::Video },
    ExtensionCategory{ ".m4v", AssetImportCategory::Video },
    ExtensionCategory{ ".anim", AssetImportCategory::Animation },
    ExtensionCategory{ ".bvh", AssetImportCategory::Animation },
    ExtensionCategory{ ".clip", AssetImportCategory::Animation },
    ExtensionCategory{ ".motion", AssetImportCategory::Animation },
    ExtensionCategory{ ".anm", AssetImportCategory::Animation },
    ExtensionCategory{ ".skel", AssetImportCategory::Animation },
    ExtensionCategory{ ".vmd", AssetImportCategory::Animation },
    ExtensionCategory{ ".trc", AssetImportCategory::Animation },
    ExtensionCategory{ ".mat", AssetImportCategory::Material },
    ExtensionCategory{ ".material", AssetImportCategory::Material },
    ExtensionCategory{ ".mtl", AssetImportCategory::Material },
    ExtensionCategory{ ".hlsl", AssetImportCategory::Shader },
    ExtensionCategory{ ".glsl", AssetImportCategory::Shader },
    ExtensionCategory{ ".vert", AssetImportCategory::Shader },
    ExtensionCategory{ ".frag", AssetImportCategory::Shader },
    ExtensionCategory{ ".geom", AssetImportCategory::Shader },
    ExtensionCategory{ ".tesc", AssetImportCategory::Shader },
    ExtensionCategory{ ".tese", AssetImportCategory::Shader },
    ExtensionCategory{ ".comp", AssetImportCategory::Shader },
    ExtensionCategory{ ".spv", AssetImportCategory::Shader },
    ExtensionCategory{ ".wgsl", AssetImportCategory::Shader },
    ExtensionCategory{ ".fx", AssetImportCategory::Shader },
    ExtensionCategory{ ".fxh", AssetImportCategory::Shader },
    ExtensionCategory{ ".cginc", AssetImportCategory::Shader },
    ExtensionCategory{ ".ush", AssetImportCategory::Shader },
    ExtensionCategory{ ".usf", AssetImportCategory::Shader },
    ExtensionCategory{ ".ttf", AssetImportCategory::Font },
    ExtensionCategory{ ".otf", AssetImportCategory::Font },
    ExtensionCategory{ ".woff", AssetImportCategory::Font },
    ExtensionCategory{ ".woff2", AssetImportCategory::Font },
    ExtensionCategory{ ".fnt", AssetImportCategory::Font },
    ExtensionCategory{ ".font", AssetImportCategory::Font },
    ExtensionCategory{ ".lua", AssetImportCategory::Script },
    ExtensionCategory{ ".py", AssetImportCategory::Script },
    ExtensionCategory{ ".cs", AssetImportCategory::Script },
    ExtensionCategory{ ".js", AssetImportCategory::Script },
    ExtensionCategory{ ".ts", AssetImportCategory::Script },
    ExtensionCategory{ ".gd", AssetImportCategory::Script },
    ExtensionCategory{ ".native", AssetImportCategory::Script },
    ExtensionCategory{ ".kbgraph", AssetImportCategory::Script },
    ExtensionCategory{ ".umap", AssetImportCategory::Scene },
    ExtensionCategory{ ".21kbscene", AssetImportCategory::Scene },
    ExtensionCategory{ ".tscn", AssetImportCategory::Scene },
    ExtensionCategory{ ".scn", AssetImportCategory::Scene },
    ExtensionCategory{ ".escn", AssetImportCategory::Scene },
    ExtensionCategory{ ".prefab", AssetImportCategory::Scene },
    ExtensionCategory{ ".kbprefab", AssetImportCategory::Scene },
    ExtensionCategory{ ".xml", AssetImportCategory::Data },
    ExtensionCategory{ ".yaml", AssetImportCategory::Data },
    ExtensionCategory{ ".yml", AssetImportCategory::Data },
    ExtensionCategory{ ".csv", AssetImportCategory::Data },
    ExtensionCategory{ ".txt", AssetImportCategory::Data },
    ExtensionCategory{ ".bytes", AssetImportCategory::Data },
    ExtensionCategory{ ".bin", AssetImportCategory::Data },
    ExtensionCategory{ ".dat", AssetImportCategory::Data },
    ExtensionCategory{ ".ini", AssetImportCategory::Data },
    ExtensionCategory{ ".cfg", AssetImportCategory::Data },
    ExtensionCategory{ ".toml", AssetImportCategory::Data },
    ExtensionCategory{ ".21kbproject", AssetImportCategory::Data },
    ExtensionCategory{ ".asset", AssetImportCategory::Data },
    ExtensionCategory{ ".res", AssetImportCategory::Data },
    ExtensionCategory{ ".tres", AssetImportCategory::Data },
    ExtensionCategory{ ".import", AssetImportCategory::Data },
};

[[nodiscard]] std::string JoinExtensionsForFilter() {
    std::string filter;
    for (const ExtensionCategory& entry : KnownExtensions) {
        if (!filter.empty()) {
            filter += ';';
        }
        filter += '*';
        filter += entry.extension;
    }
    return filter;
}

} // namespace

AssetImportCategory AssetImportCatalog::ClassifyExtension(const std::filesystem::path& extension) {
    const std::string normalized = AssetPathUtilities::LowerExtension(extension);
    const auto item = std::ranges::find_if(KnownExtensions, [&normalized](const ExtensionCategory& entry) {
        return entry.extension == normalized;
    });
    return item == KnownExtensions.end() ? AssetImportCategory::Unknown : item->category;
}

bool AssetImportCatalog::IsMetaExtension(const std::filesystem::path& extension) {
    return AssetPathUtilities::LowerExtension(extension) == ".meta";
}

bool AssetImportCatalog::IsEngineAssetExtension(const std::filesystem::path& extension) {
    return AssetPathUtilities::LowerExtension(extension) == ".21kb";
}

std::vector<std::string> AssetImportCatalog::SupportedSourceExtensions() {
    std::vector<std::string> output;
    output.reserve(KnownExtensions.size());
    for (const ExtensionCategory& entry : KnownExtensions) {
        output.emplace_back(entry.extension);
    }
    return output;
}

std::string AssetImportCatalog::WindowsFileDialogFilter() {
    const std::string marketExtensions = JoinExtensionsForFilter();
    std::string filter = "Game asset sources (" + marketExtensions + ")";
    filter.push_back('\0');
    filter += marketExtensions;
    filter.push_back('\0');
    filter += "All files (*.*)";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');
    filter.push_back('\0');
    return filter;
}

} // namespace kb::assets
