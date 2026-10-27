#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace kb::world2d {

// The twelve border pieces of a ground brush, as Remere's Map Editor names them. A piece is drawn
// on a tile next to a higher ground: n when that ground lies north of the tile, cnw when it lies
// only diagonally north-west, dnw when it lies both north and west.
enum class Map2DBorderPiece : std::uint8_t {
    N, E, S, W,
    CornerNW, CornerNE, CornerSE, CornerSW,
    DiagonalNW, DiagonalNE, DiagonalSE, DiagonalSW,
    Count,
};

inline constexpr std::size_t kMap2DBorderPieceCount = static_cast<std::size_t>(Map2DBorderPiece::Count);
inline constexpr std::uint32_t kMap2DNoCell = 0xFFFFFFFFU;

[[nodiscard]] std::string_view Map2DBorderPieceName(Map2DBorderPiece piece) noexcept;
[[nodiscard]] std::optional<Map2DBorderPiece> ParseMap2DBorderPiece(std::string_view name) noexcept;

// A ground painted with weighted variants; a higher zOrder ground borders onto lower ones and
// onto empty tiles. Every cell index is into the brush's texture cut into cellSize squares.
struct Map2DGroundBrush {
    std::string name;
    std::int32_t zOrder = 0;
    std::string texture;
    std::uint32_t cellSize = 32U;
    std::vector<std::uint32_t> tiles;
    std::vector<std::uint32_t> weights;
    std::array<std::uint32_t, kMap2DBorderPieceCount> borders{
        kMap2DNoCell, kMap2DNoCell, kMap2DNoCell, kMap2DNoCell, kMap2DNoCell, kMap2DNoCell,
        kMap2DNoCell, kMap2DNoCell, kMap2DNoCell, kMap2DNoCell, kMap2DNoCell, kMap2DNoCell,
    };

    [[nodiscard]] std::uint32_t Border(Map2DBorderPiece piece) const noexcept { return borders[static_cast<std::size_t>(piece)]; }
    [[nodiscard]] bool HasBorders() const noexcept;
    // The variant for a tile, picked by weight and the same for the same tile every time.
    [[nodiscard]] std::uint32_t VariantFor(std::uint32_t x, std::uint32_t y, std::int32_t z) const noexcept;
};

// Fills the brush from a block of its sheet: a 3x3 island (the ground in the middle, n/e/s/w and
// the corners around it) whose top-left cell is `firstCell`, and optionally a 2x2 hole of the
// diagonal pieces. `columns` is the sheet width in cells.
void AssignMap2DIslandBorders(Map2DGroundBrush& brush, std::uint32_t firstCell, std::uint32_t columns);
void AssignMap2DHoleBorders(Map2DGroundBrush& brush, std::uint32_t firstCell, std::uint32_t columns);

// An object of several tiles (a tree, a house): a block of a sheet placed in one click, its
// bottom middle tile where the mouse is. Empty sprites of the block are left out.
struct Map2DObjectBrush {
    std::string name;
    std::string texture;
    std::uint32_t cellSize = 32U;
    std::uint32_t firstCell = 0U;
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
};

// How a sheet is cut: its sprite size, and whether sprites that touch stay single sprites
// instead of being joined into one object.
struct Map2DSheetSetting {
    std::string texture;
    std::uint32_t cellSize = 32U;
    bool separate = false;
    std::uint32_t cellHeight = 0U;
    std::uint32_t margin = 0U;
    std::uint32_t spacing = 0U;
    [[nodiscard]] std::uint32_t Height() const noexcept { return cellHeight == 0U ? cellSize : cellHeight; }
    [[nodiscard]] std::uint32_t Count(std::uint32_t extent, std::uint32_t cell) const noexcept {
        const auto border = static_cast<std::uint64_t>(margin) * 2U;
        return cell == 0U || extent < border + cell ? 0U : static_cast<std::uint32_t>((extent - border + spacing) / (static_cast<std::uint64_t>(cell) + spacing));
    }
    [[nodiscard]] bool Fits(std::uint32_t width, std::uint32_t height) const noexcept {
        const auto columns = Count(width, cellSize), rows = Count(height, Height());
        return columns > 0U && rows > 0U && static_cast<std::uint64_t>(columns) * rows <= 65536U &&
            static_cast<std::uint64_t>(margin) * 2U + static_cast<std::uint64_t>(columns) * cellSize + static_cast<std::uint64_t>(columns - 1U) * spacing == width &&
            static_cast<std::uint64_t>(margin) * 2U + static_cast<std::uint64_t>(rows) * Height() + static_cast<std::uint64_t>(rows - 1U) * spacing == height;
    }
};

// A .kbtileset text file: "21kb-tileset 1", then for every brush
//   ground "<name>" <zOrder> <cellSize> "<texture /Game path>"
//   tiles <cell> ...
//   weights <weight> ...
//   border n=<cell> e=<cell> ... dsw=<cell>
// for every object
//   object "<name>" <cellSize> "<texture>"
//   cells <firstCell> <columns> <rows>
// and for a sheet cut another way than in 32 px sprites
//   sheet "<texture>" <cellSize> [separate]
struct Map2DTileset {
    static constexpr std::uint32_t CurrentVersion = 2U;
    std::vector<Map2DGroundBrush> grounds;
    std::vector<Map2DObjectBrush> objects;
    std::vector<Map2DSheetSetting> sheets;

    [[nodiscard]] Map2DGroundBrush* Find(std::string_view name) noexcept;
    [[nodiscard]] const Map2DGroundBrush* Find(std::string_view name) const noexcept;
    [[nodiscard]] Map2DObjectBrush* FindObject(std::string_view name) noexcept;
    [[nodiscard]] const Map2DObjectBrush* FindObject(std::string_view name) const noexcept;
    [[nodiscard]] Map2DSheetSetting* FindSheet(std::string_view texture) noexcept;
    [[nodiscard]] const Map2DSheetSetting* FindSheet(std::string_view texture) const noexcept;
};

// A sprite or a group of sprites a sheet holds, found from its pixels: a single sprite, or
// sprites that touch (not filling their cells) joined into one object, as a tree of 2x2 cells.
// `cells` lists the non-empty cells of the block, relative to its top-left one.
struct Map2DSheetPiece {
    std::uint32_t firstCell = 0U;
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
    // Every pixel covered: a floor tile, painted as the ground.
    bool opaque = false;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> cells;
};

// The pieces of an ARGB (0xAARRGGBB) image cut into cellSize cells, in reading order; cells in
// `skip` (used by grounds and objects) are left out.
[[nodiscard]] std::vector<Map2DSheetPiece> FindMap2DSheetPieces(
    std::span<const std::uint32_t> pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t cellSize,
    bool separate,
    std::span<const std::uint32_t> skip,
    std::uint32_t cellHeight = 0U, std::uint32_t margin = 0U, std::uint32_t spacing = 0U);
// The top-left tile of an object placed with its bottom middle tile at (x, y).
[[nodiscard]] std::pair<std::int64_t, std::int64_t> Map2DObjectOrigin(std::uint32_t columns, std::uint32_t rows, std::int64_t x, std::int64_t y) noexcept;

inline constexpr std::string_view kMap2DTilesetExtension = ".kbtileset";

class Map2DTilesetIO final {
public:
    Map2DTilesetIO() = delete;

    [[nodiscard]] static std::optional<Map2DTileset> Load(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] static std::optional<Map2DTileset> Parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] static std::string Write(const Map2DTileset& tileset);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const Map2DTileset& tileset, std::string* error = nullptr);
};

} // namespace kb::world2d
