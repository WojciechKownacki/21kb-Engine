#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/Sprite2DComponent.hpp"

#include <memory>
#include <optional>
#include <string>

namespace kb::scene { class Scene; }

namespace kb::world2d {

// Which cell a sprite shows at a time: the still Cell, or for an animation (FrameCount above 1) the frame its clock is at.
// A looping animation wraps; a non-looping one stops on its last frame.
[[nodiscard]] std::uint32_t Sprite2DFrameCell(const kb::scene::Sprite2DComponent& sprite, double seconds) noexcept;

// How a sprite sheet is cut: the cell size in pixels and how many cells the image holds each way. False when the image
// is smaller than one cell or the sheet is not made of whole cells.
struct Sprite2DSheetLayout {
    std::uint32_t cellWidth = 0U;
    std::uint32_t cellHeight = 0U;
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
};
[[nodiscard]] std::optional<Sprite2DSheetLayout> Sprite2DLayout(const kb::scene::Sprite2DComponent& sprite, std::uint32_t imageWidth, std::uint32_t imageHeight) noexcept;

// The one-tile map the sprite is drawn from, so a sprite uses the same mesh builder, materials and packaging as a
// tilemap. The tile sits at the entity's origin with the pivot on it.
[[nodiscard]] std::optional<kb::assets::Map2DAsset> BuildSprite2DMap(const kb::scene::Sprite2DComponent& sprite,
    std::string_view texturePath, std::string_view materialPath, std::uint32_t imageWidth, std::uint32_t imageHeight,
    std::uint32_t cell, std::string* error = nullptr);

// Chooses the picture of a sprite from a registered asset (null clears it) and records its size in pixels when the image file
// can be read, so the sprite can be cut into cells without opening the image at run time. False when the entity has no
// Sprite2D.
[[nodiscard]] bool AssignSprite2DImage(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::assets::AssetMetadata* image);

// Draws every Sprite2D of a scene: each gets a hidden child entity with a generated quad mesh that is rebuilt only when the
// sprite, its frame or its picture changes. Runs while editing (a still sprite) and while playing (the animation advances).
class Sprite2DRuntime final : public kb::scene::SceneSystem {
public:
    Sprite2DRuntime();
    ~Sprite2DRuntime() override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] std::size_t RebuiltLastUpdate() const noexcept;
    // The hidden child that draws the sprite, if there is one.
    [[nodiscard]] kb::scene::SceneEntity VisualOf(kb::scene::SceneEntity owner) const;
    // The frame cell a sprite shows now (its running clock, not the Time field).
    [[nodiscard]] std::optional<std::uint32_t> CurrentCell(kb::scene::SceneEntity owner) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::world2d
