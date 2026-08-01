#pragma once

#include "TerrainBrush.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::scene { class Scene; }

namespace kb::editor {

struct EditorTerrainToolState {
    kb::terrain_editor::TerrainBrushSettings brush{};
    bool editingEnabled = false;
    bool strokeActive = false;
    float lastStampX = 0.0F;
    float lastStampZ = 0.0F;
};

class EditorTerrainService final {
public:
    EditorTerrainService() = delete;
    [[nodiscard]] static EditorTerrainToolState& ToolState() noexcept;

    [[nodiscard]] static bool IsTerrainEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static std::optional<kb::assets::TerrainAsset> Load(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::string* error = nullptr);
    [[nodiscard]] static bool Create(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const std::filesystem::path& assetsRoot,
        std::string* error = nullptr);
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static bool ApplyBrush(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::terrain_editor::TerrainBrushSettings& settings,
        const kb::terrain_editor::TerrainBrushStamp& stamp,
        std::string* error = nullptr);
    [[nodiscard]] static bool ImportHeightmap(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const std::filesystem::path& heightmapPath,
        std::string* error = nullptr);
};

} // namespace kb::editor
