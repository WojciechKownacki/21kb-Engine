#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/post/ScenePostProcessSettings.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr const char* kPostProcessProfileAssetExtension = ".kbppfx";
inline constexpr const char* kPostProcessProfileAssetType = "PostProcessProfile";

// LIB-142: the ONLY asset-based way to configure scene post-processing. Its payload IS a
// plain kb::render::ScenePostProcessSettings value (no separate wrapper/schema struct to
// keep in sync) - a text asset that a script assigns wholesale to a scene via
// kb::scene::ScenePostProcessAccess::SetActiveProfile, mirroring MeshRenderer.SetMaterial's
// "assign a whole asset reference, never poke individual PBR fields from script" convention
// (see ScriptPostProcessApi.hpp's own doc comment) rather than the generic per-field
// reflection mutation Light/Camera/MeshRenderer use for their own component fields.
class PostProcessProfileAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    [[nodiscard]] static std::optional<ScenePostProcessSettings> LoadProfile(const std::filesystem::path& path);
    [[nodiscard]] static bool SaveProfile(const std::filesystem::path& path, const ScenePostProcessSettings& settings);
};

} // namespace kb::render
