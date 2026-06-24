#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAssetBrowserState;

class EditorMaterialAssetGateway {
public:
    EditorMaterialAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept;

    [[nodiscard]] kb::scene::Scene& Scene() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> ResolveFolder(const std::filesystem::path& virtualFolder) const;
    [[nodiscard]] static std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName);
    [[nodiscard]] static std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName, std::string_view extension);
    [[nodiscard]] static std::optional<std::filesystem::path> ResolveFile(const kb::scene::Scene& scene, kb::assets::AssetId id);
    [[nodiscard]] static std::optional<kb::render::RenderMaterialAssetData> Read(const kb::scene::Scene& scene, kb::assets::AssetId id);
    [[nodiscard]] static bool WriteExisting(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::render::RenderMaterialAssetData& asset);
    [[nodiscard]] bool WriteNewMaterial(const std::filesystem::path& path, const kb::render::RenderMaterialAssetData& asset);
    [[nodiscard]] bool WriteNewMaterialInstance(const std::filesystem::path& path, const kb::render::RenderMaterialInstanceAssetData& asset);
    [[nodiscard]] bool Mutate(kb::assets::AssetId id, const std::function<void(kb::render::RenderMaterialAssetData&)>& mutate);

private:
    void DiscoverAndSelect(const std::filesystem::path& path);
    void EnsureMaterialLoader();
    void EnsureMaterialInstanceLoader();

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
};

} // namespace kb::editor
