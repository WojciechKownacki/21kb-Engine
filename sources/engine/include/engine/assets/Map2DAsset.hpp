#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

namespace kb::assets {

// A 2D tile map (World 2D Map): a grid of tiles on floors -50..50 (0 is the ground floor, higher
// floors above it), each tile a stack of sprites cut from texture sheets. It renders as a render
// mesh, the same way a terrain does, centred on its entity. Laid flat (a table top) tile (x, y)
// covers X [x - w/2, x + 1 - w/2) and Z [h/2 - y - 1, h/2 - y) in tile units, so north (row 0) is
// +Z - the top of a camera looking straight down - and floor z is lifted by floorHeight along +Y.
// Hung on a wall (a picture) it covers the same X and Y [h - y - 1, h - y), the bottom row standing
// on Y = 0, facing -Z, and floor z comes forward along -Z. The text format is "21kb-map2d 2" followed by size,
// tileSize, floorHeight, orientation, view, background, sheet and t lines.
inline constexpr std::string_view kMap2DAssetType = "RenderMesh";
inline constexpr std::string_view kMap2DAssetExtension = ".kbmap2d";

struct Map2DSprite {
    static constexpr std::uint16_t kNoSheet = 0xFFFFU;
    std::uint16_t sheet = kNoSheet;
    std::uint32_t cell = 0U;
    std::uint8_t rotation = 0U; // Clockwise quarter turns.
    bool flipX = false;
    bool flipY = false;
    std::uint32_t tint = 0xFFFFFFFFU; // RGBA, red in the most significant byte.
    float offsetX = 0.0F; // Tile units, positive right/down in the map.
    float offsetY = 0.0F;

    [[nodiscard]] bool IsValid() const noexcept { return sheet != kNoSheet; }
    friend bool operator==(const Map2DSprite&, const Map2DSprite&) = default;
};

enum class Map2DCollisionKind : std::uint8_t { None, Solid, SlopeUp, SlopeDown, OneWay, Polygon };

struct Map2DCollision {
    Map2DCollisionKind kind = Map2DCollisionKind::None;
    bool trigger = false;
    float friction = 0.5F;
    float restitution = 0.0F;
    // A simple polygon inside the cell, in normalized coordinates (positive down).
    std::vector<std::array<float, 2>> polygon;
    friend bool operator==(const Map2DCollision&, const Map2DCollision&) = default;
};

using Map2DValue = std::variant<bool, std::int64_t, double, std::string>;
using Map2DProperties = std::map<std::string, Map2DValue>;

struct Map2DTileDefinition {
    std::vector<std::uint32_t> frames;
    float framesPerSecond = 8.0F;
    Map2DCollision collision;
    bool blocksNavigation = false;
    bool occludesLight = false;
    Map2DProperties properties;
    friend bool operator==(const Map2DTileDefinition&, const Map2DTileDefinition&) = default;
};

// Authored rules are embedded in maps, so packaged games use the same autoborder as the editor.
struct Map2DGroundRule {
    std::string name;
    std::int32_t order = 0;
    std::uint16_t sheet = 0U;
    std::vector<std::uint32_t> variants;
    std::vector<std::uint32_t> weights;
    std::array<std::uint32_t, 12> borders = [] { std::array<std::uint32_t, 12> value; value.fill(UINT32_MAX); return value; }();
    friend bool operator==(const Map2DGroundRule&, const Map2DGroundRule&) = default;
};

// A texture cut into rectangular cells, with optional margin and spacing, row by row from the
// top left. The material (a .kbmat made for the sheet) draws its sprites.
struct Map2DSheet {
    std::string texture;
    std::string material;
    std::uint32_t cellSize = 32U;
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
    std::uint32_t cellHeight = 0U; // Zero keeps the legacy square cell size.
    std::uint32_t margin = 0U;
    std::uint32_t spacing = 0U;
    std::uint32_t imageWidth = 0U;
    std::uint32_t imageHeight = 0U;
    std::map<std::uint32_t, Map2DTileDefinition> definitions;
    [[nodiscard]] std::uint32_t CellHeight() const noexcept { return cellHeight ? cellHeight : cellSize; }
    [[nodiscard]] std::uint32_t ImageWidth() const noexcept { return imageWidth ? imageWidth : 2U * margin + columns * (cellSize + spacing) - spacing; }
    [[nodiscard]] std::uint32_t ImageHeight() const noexcept { return imageHeight ? imageHeight : 2U * margin + rows * (CellHeight() + spacing) - spacing; }
    friend bool operator==(const Map2DSheet&, const Map2DSheet&) = default;
};

// Drawn bottom to top: the ground, the borders the editor derives from the neighbouring grounds,
// then the items placed on it.
struct Map2DTile {
    Map2DSprite ground{};
    std::vector<Map2DSprite> borders;
    std::vector<Map2DSprite> items;
    bool solid = false;
    std::optional<Map2DCollision> collision;
    Map2DProperties properties;
    std::string prefab; // Optional /Game/...kbprefab placed at this cell's footpoint.

    [[nodiscard]] bool IsEmpty() const noexcept { return !ground.IsValid() && borders.empty() && items.empty() && !solid &&
        !collision && properties.empty() && prefab.empty(); }
    friend bool operator==(const Map2DTile&, const Map2DTile&) = default;
};

enum class Map2DOrientation : std::uint8_t {
    Flat,
    Wall,
};

// How the map's play camera frames and follows. Every default keeps the plain camera: the view height
// of the map, still, centred on the map.
struct Map2DCamera {
    // Above zero the camera is pixel perfect: one world unit is this many texels of the sprites, the view
    // height is a whole number of screen pixels per texel, and the camera rests on whole screen pixels.
    float pixelsPerUnit = 0.0F;
    // The entity the camera follows, by name; empty keeps the camera still.
    std::string followTarget;
    // Seconds in which the camera closes about two thirds of its distance to the target (0 = at once).
    float smoothingSeconds = 0.0F;
    // The drag margins in tiles: the target may roam this far across and up from the camera's centre.
    float deadZoneWidth = 0.0F;
    float deadZoneHeight = 0.0F;
    // Keeps the view inside the map (a map smaller than the view stays centred).
    bool clampToMap = false;
    friend bool operator==(const Map2DCamera&, const Map2DCamera&) = default;
};

struct Map2DAsset {
    static constexpr std::uint32_t CurrentVersion = 2U;
    static constexpr std::uint32_t MaximumSize = 4096U;
    static constexpr std::int32_t MinimumFloor = -50;
    static constexpr std::int32_t MaximumFloor = 50;
    static constexpr std::size_t MaximumSheets = 256U;
    static constexpr std::size_t MaximumStack = 32U;

    std::uint32_t width = 64U;
    std::uint32_t height = 64U;
    float tileSize = 1.0F;
    float floorHeight = 3.0F;
    Map2DOrientation orientation = Map2DOrientation::Flat;
    // The tiles a player sees at once (Remere's client box): what a 2D camera frames.
    std::uint32_t viewWidth = 15U;
    std::uint32_t viewHeight = 11U;
    // A dark quad under the whole map, so its extent shows before anything is painted.
    bool background = true;
    std::vector<Map2DSheet> sheets;
    std::vector<Map2DGroundRule> groundRules;
    float parallaxX = 1.0F;
    float parallaxY = 1.0F;
    bool sortByY = false;
    bool lit = false;
    Map2DCamera camera;
    // Non-empty tiles by Map2DTileKey.
    std::map<std::uint64_t, Map2DTile> tiles;
};

// Ordered by floor, then row, then column. The floor is stored offset by 50 so lower floors sort first.
[[nodiscard]] constexpr std::uint64_t Map2DTileKey(std::uint32_t x, std::uint32_t y, std::int32_t z) noexcept {
    return (static_cast<std::uint64_t>(z - Map2DAsset::MinimumFloor) << 48U) | (static_cast<std::uint64_t>(y) << 24U) | static_cast<std::uint64_t>(x);
}
[[nodiscard]] constexpr std::uint32_t Map2DTileKeyX(std::uint64_t key) noexcept { return static_cast<std::uint32_t>(key & 0xFFFFFFU); }
[[nodiscard]] constexpr std::uint32_t Map2DTileKeyY(std::uint64_t key) noexcept { return static_cast<std::uint32_t>((key >> 24U) & 0xFFFFFFU); }
[[nodiscard]] constexpr std::int32_t Map2DTileKeyZ(std::uint64_t key) noexcept {
    return static_cast<std::int32_t>(key >> 48U) + Map2DAsset::MinimumFloor;
}
[[nodiscard]] std::string_view Map2DOrientationName(Map2DOrientation orientation) noexcept;

[[nodiscard]] bool IsMap2DAssetValid(const Map2DAsset& map, std::string* error = nullptr);
[[nodiscard]] bool IsMap2DCollisionValid(const Map2DCollision& collision) noexcept;
[[nodiscard]] Map2DCollision Map2DCellCollision(const Map2DAsset& map, const Map2DTile& tile);
[[nodiscard]] const Map2DTileDefinition* Map2DDefinition(const Map2DAsset& map, Map2DSprite sprite) noexcept;
[[nodiscard]] Map2DSprite Map2DAnimatedSprite(const Map2DAsset& map, Map2DSprite sprite, double seconds) noexcept;
// Destination-to-source UV mapping shared by the CPU editor and the renderer.
[[nodiscard]] std::array<float, 2> Map2DSpriteUV(Map2DSprite sprite, float u, float v) noexcept;
[[nodiscard]] std::optional<Map2DValue> Map2DCellProperty(const Map2DAsset& map, const Map2DTile& tile, std::string_view name);
[[nodiscard]] Map2DAsset MakeEmptyMap2DAsset(std::uint32_t width = 64U, std::uint32_t height = 64U);
// The sprite's texture rectangle in pixels; false for a sprite outside its sheet.
[[nodiscard]] bool Map2DSpriteRect(
    const Map2DAsset& map,
    Map2DSprite sprite,
    std::uint32_t& left,
    std::uint32_t& top,
    std::uint32_t& size) noexcept;
[[nodiscard]] bool Map2DSpriteRect(const Map2DAsset& map, Map2DSprite sprite, std::uint32_t& left,
    std::uint32_t& top, std::uint32_t& width, std::uint32_t& height) noexcept;
// "0:12" (sheet:cell).
[[nodiscard]] std::string FormatMap2DSprite(Map2DSprite sprite);
[[nodiscard]] std::optional<Map2DSprite> ParseMap2DSprite(std::string_view text) noexcept;

class Map2DAssetIO final {
public:
    Map2DAssetIO() = delete;

    [[nodiscard]] static std::optional<Map2DAsset> Load(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] static std::optional<Map2DAsset> Load(std::span<const std::uint8_t> bytes, std::string* error = nullptr);
    [[nodiscard]] static std::optional<Map2DAsset> Parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] static std::string Write(const Map2DAsset& map);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const Map2DAsset& map, std::string* error = nullptr);
};

} // namespace kb::assets
