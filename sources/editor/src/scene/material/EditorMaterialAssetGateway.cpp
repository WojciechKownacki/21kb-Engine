#include "scene/material/EditorMaterialAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr std::string_view kMaterialExtension = ".kbmat";

} // namespace

EditorMaterialAssetGateway::EditorMaterialAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept
    : scene_(scene)
    , browser_(browser) {}

kb::scene::Scene& EditorMaterialAssetGateway::Scene() const noexcept {
    return scene_;
}

std::optional<std::filesystem::path> EditorMaterialAssetGateway::ResolveFolder(const std::filesystem::path& virtualFolder) const {
    if (virtualFolder.empty()) {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> probe = scene_.Assets().Manager().Mounts().Resolve(virtualFolder / "probe");
    return probe.has_value() ? std::optional<std::filesystem::path>{ probe->parent_path() } : std::nullopt;
}

std::filesystem::path EditorMaterialAssetGateway::UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName) {
    return UniqueFilePath(folder, baseName, kMaterialExtension);
}

std::filesystem::path EditorMaterialAssetGateway::UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName, std::string_view extension) {
    std::filesystem::path candidate = folder / (std::string{ baseName } + std::string{ extension });
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = folder / (std::string{ baseName } + std::to_string(suffix) + std::string{ extension });
        ++suffix;
    }
    return candidate;
}

std::optional<std::filesystem::path> EditorMaterialAssetGateway::ResolveFile(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        return std::nullopt;
    }
    if (!metadata->physicalPath.empty()) {
        return metadata->physicalPath;
    }
    return manager.Mounts().Resolve(metadata->virtualPath);
}

std::optional<kb::render::RenderMaterialAssetData> EditorMaterialAssetGateway::Read(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> path = ResolveFile(scene, id);
    return path.has_value() ? kb::render::RenderMaterialAssetLoader::LoadMaterial(*path) : std::nullopt;
}

bool EditorMaterialAssetGateway::WriteExisting(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::render::RenderMaterialAssetData& asset) {
    const std::optional<std::filesystem::path> path = ResolveFile(scene, id);
    if (!path.has_value()) {
        return false;
    }
    if (!kb::render::RenderMaterialAssetWriter::Save(*path, asset)) {
        return false;
    }
    static_cast<void>(scene.Assets().Manager().Unload(id));
    static_cast<void>(scene.Assets().Discover());
    return true;
}

void EditorMaterialAssetGateway::EnsureMaterialLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
}

void EditorMaterialAssetGateway::EnsureMaterialInstanceLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()));
}

void EditorMaterialAssetGateway::DiscoverAndSelect(const std::filesystem::path& path) {
    EnsureMaterialLoader();
    EnsureMaterialInstanceLoader();
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(scene_.Assets().Discover());
    if (const std::optional<std::filesystem::path> created = manager.Mounts().ToVirtual(path)) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*created); metadata != nullptr) {
            static_cast<void>(browser_.SelectAsset(metadata->id, manager));
        }
    }
}

bool EditorMaterialAssetGateway::WriteNewMaterial(const std::filesystem::path& path, const kb::render::RenderMaterialAssetData& asset) {
    if (!kb::render::RenderMaterialAssetWriter::Save(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorMaterialAssetGateway::WriteNewMaterialInstance(const std::filesystem::path& path, const kb::render::RenderMaterialInstanceAssetData& asset) {
    if (!kb::render::RenderMaterialInstanceAssetWriter::Save(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorMaterialAssetGateway::Mutate(kb::assets::AssetId id, const std::function<void(kb::render::RenderMaterialAssetData&)>& mutate) {
    std::optional<kb::render::RenderMaterialAssetData> asset = Read(scene_, id);
    if (!asset.has_value()) {
        return false;
    }
    mutate(*asset);
    return WriteExisting(scene_, id, *asset);
}

} // namespace kb::editor
