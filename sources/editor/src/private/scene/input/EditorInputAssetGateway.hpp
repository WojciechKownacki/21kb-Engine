#pragma once

#include "engine/assets/AssetId.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::input {

struct InputActionAsset;
struct InputMappingContextAsset;

} // namespace kb::input

namespace kb::editor {

class EditorAssetBrowserState;

// Owns every filesystem/registry interaction for input assets: resolving the
// physical file for an asset id, reading/mutating it on disk, refreshing the
// asset manager afterwards, and creating + selecting new asset files.
//
// Single responsibility: input-asset persistence. Authoring services build the
// default/edited data and delegate all I/O here.
class EditorInputAssetGateway {
public:
    EditorInputAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept;

    [[nodiscard]] std::optional<std::filesystem::path> ResolveFolder(const std::filesystem::path& virtualFolder) const;
    [[nodiscard]] static std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName, std::string_view extension);
    void DiscoverAndSelect(const std::filesystem::path& path);
    [[nodiscard]] bool WriteNewAction(const std::filesystem::path& path, const kb::input::InputActionAsset& asset);
    [[nodiscard]] bool WriteNewContext(const std::filesystem::path& path, const kb::input::InputMappingContextAsset& asset);

    // Reads are pure (no manager mutation), so they are exposed as static helpers
    // taking a const scene — callable from const contexts without a const_cast.
    [[nodiscard]] static std::optional<kb::input::InputActionAsset> ReadAction(const kb::scene::Scene& scene, kb::assets::AssetId id);
    [[nodiscard]] static std::optional<kb::input::InputMappingContextAsset> ReadContext(const kb::scene::Scene& scene, kb::assets::AssetId id);

    [[nodiscard]] bool MutateAction(kb::assets::AssetId id, const std::function<void(kb::input::InputActionAsset&)>& mutate);
    [[nodiscard]] bool MutateContext(kb::assets::AssetId id, const std::function<void(kb::input::InputMappingContextAsset&)>& mutate);

private:
    [[nodiscard]] static std::optional<std::filesystem::path> ResolveFile(const kb::scene::Scene& scene, kb::assets::AssetId id, std::string_view type);
    void RefreshAfterWrite(kb::assets::AssetId id);

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
};

} // namespace kb::editor
