#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "resources/RenderMeshAssetFinalizer.hpp"

#include <memory>
#include <algorithm>
#include <cctype>
#include <string>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <system_error>
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

[[nodiscard]] std::string LowerExtensionText(std::string extension) {
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadObjPayload(const kb::assets::ImportedAsset& imported) {
    std::string text;
    text.resize(imported.payload.size());
    std::ranges::transform(imported.payload, text.begin(), [](std::byte value) {
        return static_cast<char>(value);
    });

    std::istringstream input{ text };
    return RenderMeshAssetBuilder::LoadObj(input);
}

[[nodiscard]] std::filesystem::path TempImportedMeshPath(const kb::assets::AssetLoadRequest& request, const kb::assets::ImportedAsset& imported) {
    std::filesystem::path extension = imported.sourceExtension.empty() ? std::filesystem::path{ ".gltf" } : std::filesystem::path{ imported.sourceExtension };
    const std::string filename = "21kb_imported_mesh_"
        + std::to_string(request.metadata.id.value)
        + "_"
        + std::to_string(request.metadata.contentHash)
        + extension.string();
    return (std::filesystem::temp_directory_path() / filename).lexically_normal();
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadGltfPayload(const kb::assets::AssetLoadRequest& request, const kb::assets::ImportedAsset& imported) {
    const std::filesystem::path tempPath = TempImportedMeshPath(request, imported);
    std::error_code error;
    bool wrote = false;
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return std::nullopt;
        }
        output.write(reinterpret_cast<const char*>(imported.payload.data()), static_cast<std::streamsize>(imported.payload.size()));
        wrote = output.good();
    }
    if (!wrote) {
        std::filesystem::remove(tempPath, error);
        return std::nullopt;
    }

    std::optional<RenderMeshAssetData> mesh = RenderMeshAssetBuilder::LoadGltf(tempPath);
    std::filesystem::remove(tempPath, error);
    return mesh;
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadImportedMesh(const kb::assets::AssetLoadRequest& request) {
    kb::assets::ImportedAssetLoader importedLoader;
    kb::assets::AssetLoadResult result = importedLoader.Load(request);
    if (!result.Succeeded()) {
        return std::nullopt;
    }

    const std::shared_ptr<kb::assets::ImportedAsset> imported = std::static_pointer_cast<kb::assets::ImportedAsset>(result.asset);
    if (imported == nullptr || imported->category != kb::assets::AssetImportCategory::Model) {
        return std::nullopt;
    }

    const std::string sourceExtension = LowerExtensionText(imported->sourceExtension);
    if (sourceExtension == ".obj") {
        return LoadObjPayload(*imported);
    }
    if (sourceExtension == ".gltf" || sourceExtension == ".glb") {
        return LoadGltfPayload(request, *imported);
    }
    if (sourceExtension == ".fbx") {
        return RenderMeshAssetBuilder::LoadFbx(std::span<const std::byte>{ imported->payload.data(), imported->payload.size() });
    }
    return std::nullopt;
}

} // namespace

std::string_view RenderMeshAssetLoader::Type() const noexcept {
    return "RenderMesh";
}

std::type_index RenderMeshAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMeshAssetData);
}

std::vector<std::string> RenderMeshAssetLoader::Extensions() const {
    return { ".obj", ".gltf", ".glb", ".fbx" };
}

kb::assets::AssetLoadResult RenderMeshAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    const std::string extension = LowerExtension(request.resolvedPath);
    std::optional<RenderMeshAssetData> mesh;
    if (extension == ".21kb") {
        mesh = LoadImportedMesh(request);
    } else {
        if (extension == ".gltf" || extension == ".glb") {
            mesh = RenderMeshAssetBuilder::LoadGltf(request.resolvedPath);
        } else if (extension == ".fbx") {
            mesh = RenderMeshAssetBuilder::LoadFbx(request.resolvedPath);
        } else {
            mesh = RenderMeshAssetBuilder::LoadObj(request.resolvedPath);
        }
    }
    if (!mesh.has_value()) {
        return kb::assets::AssetLoadResult{
            .error = "Render mesh import failed: " + request.resolvedPath.string(),
        };
    }
    RenderMeshAssetFinalizer::EnsureTangentVertexStorage(*mesh);
    mesh->RefreshDesc();

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMeshAssetData>(std::move(*mesh)),
    };
}

} // namespace kb::render
