#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <memory>
#include <algorithm>
#include <cctype>
#include <string>
#include <filesystem>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

} // namespace

std::string_view RenderMeshAssetLoader::Type() const noexcept {
    return "RenderMesh";
}

std::type_index RenderMeshAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMeshAssetData);
}

std::vector<std::string> RenderMeshAssetLoader::Extensions() const {
    return { ".obj", ".gltf", ".glb" };
}

kb::assets::AssetLoadResult RenderMeshAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    const std::string extension = LowerExtension(request.resolvedPath);
    std::optional<RenderMeshAssetData> mesh = extension == ".gltf" || extension == ".glb"
        ? RenderMeshAssetBuilder::LoadGltf(request.resolvedPath)
        : RenderMeshAssetBuilder::LoadObj(request.resolvedPath);
    if (!mesh.has_value()) {
        return kb::assets::AssetLoadResult{
            .error = "Render mesh import failed: " + request.resolvedPath.string(),
        };
    }

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMeshAssetData>(std::move(*mesh)),
    };
}

} // namespace kb::render
