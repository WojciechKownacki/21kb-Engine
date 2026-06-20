#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace kb::render {
struct RenderMaterialAssetData;
}

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAssetBrowserState;

class EditorMaterialAssetGateway {
public:
    EditorMaterialAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept;

    [[nodiscard]] std::optional<std::filesystem::path> ResolveFolder(const std::filesystem::path& virtualFolder) const;
    [[nodiscard]] static std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName);
    [[nodiscard]] bool WriteNewMaterial(const std::filesystem::path& path, const kb::render::RenderMaterialAssetData& asset);

private:
    void DiscoverAndSelect(const std::filesystem::path& path);
    void EnsureMaterialLoader();

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
};

} // namespace kb::editor
