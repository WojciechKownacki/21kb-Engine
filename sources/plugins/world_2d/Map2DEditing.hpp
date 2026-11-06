#pragma once

#include "Map2DTileset.hpp"

#include "engine/assets/Map2DAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::world2d {

// Map editing shared by the World 2D Map Editor window and kb_cli: ground brushes with
// Remere's-style automatic borders, sprites, eraser and flood fill, every change undoable.

// The project folder (under Assets) the Map Editor's palette is made from: every folder in it is a
// category (Nature, Ground, Buildings...), every PNG sprite sheet in a folder fills its category.
inline constexpr std::string_view kMap2DSpritesFolder = "2D Map Sprites";
// The category of sheets and brushes lying in the sprites folder itself.
inline constexpr std::string_view kMap2DGeneralCategory = "General";

// The palette category of a sheet or tileset file: its folder in the sprites folder, General
// for one directly in it, the file's name for one elsewhere in the project.
[[nodiscard]] std::string Map2DCategoryOf(const std::filesystem::path& assetsRoot, const std::filesystem::path& file);
// The tileset new brushes of a category are written to.
[[nodiscard]] std::filesystem::path Map2DCategoryTileset(const std::filesystem::path& assetsRoot, std::string_view category);

struct Map2DTilesetFile {
    std::filesystem::path path;
    std::string virtualPath;
    std::string category;
    Map2DTileset tileset;
};

// The brushes of every .kbtileset under a project's Assets folder.
class Map2DBrushLibrary {
public:
    [[nodiscard]] static Map2DBrushLibrary Load(const std::filesystem::path& assetsRoot, std::vector<std::string>* warnings = nullptr);
    [[nodiscard]] static Map2DBrushLibrary FromMap(const kb::assets::Map2DAsset& map);

    [[nodiscard]] const Map2DGroundBrush* Find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const Map2DGroundBrush*> Grounds() const;
    [[nodiscard]] const Map2DObjectBrush* FindObject(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const Map2DObjectBrush*> Objects() const;
    // How a sheet is cut (32 px sprites, joined when they touch, unless a tileset says otherwise).
    [[nodiscard]] Map2DSheetSetting SheetSetting(std::string_view texture) const;
    // The cells of a sheet that grounds and objects use, which the palette does not list again.
    [[nodiscard]] std::vector<std::uint32_t> UsedCells(std::string_view texture, std::uint32_t cellSize, std::uint32_t sheetColumns) const;
    // The category a brush was defined in.
    [[nodiscard]] std::string_view CategoryOf(const void* brush) const noexcept;

    std::vector<Map2DTilesetFile> files;
};

// Tiles as they were before and after one edit (a stroke, a fill), for undo and redo.
class Map2DChangeSet {
public:
    void Record(const kb::assets::Map2DAsset& map, std::uint64_t key);
    void RecordSettings(const kb::assets::Map2DAsset& map);
    // Captures the state after the edit; Undo/Redo use both.
    void Finish(const kb::assets::Map2DAsset& map);
    void Undo(kb::assets::Map2DAsset& map) const;
    void Redo(kb::assets::Map2DAsset& map) const;
    [[nodiscard]] bool Empty() const noexcept;

private:
    struct Settings {
        std::uint32_t width, height, viewWidth, viewHeight;
        float tileSize, floorHeight;
        kb::assets::Map2DOrientation orientation;
        bool background;
        std::vector<kb::assets::Map2DSheet> sheets;
        std::vector<kb::assets::Map2DGroundRule> groundRules;
        float parallaxX, parallaxY;
        bool sortByY;
        bool lit;
        kb::assets::Map2DCamera camera;
        std::uint32_t propertySchemaVersion;
        kb::assets::Map2DProperties propertyDefaults;
        explicit Settings(const kb::assets::Map2DAsset& map);
        void Apply(kb::assets::Map2DAsset& map) const;
        friend bool operator==(const Settings&, const Settings&) = default;
    };
    std::map<std::uint64_t, std::optional<kb::assets::Map2DTile>> before_;
    std::map<std::uint64_t, std::optional<kb::assets::Map2DTile>> after_;
    std::optional<Settings> settingsBefore_;
    std::optional<Settings> settingsAfter_;
};

// Inclusive tile rectangle.
struct Map2DRect {
    std::uint32_t x0 = 0U;
    std::uint32_t y0 = 0U;
    std::uint32_t x1 = 0U;
    std::uint32_t y1 = 0U;
};

// /Game/... <-> the file under the project's Assets folder.
[[nodiscard]] std::filesystem::path Map2DAssetFile(const std::filesystem::path& assetsRoot, std::string_view virtualPath);
[[nodiscard]] std::string Map2DVirtualPath(const std::filesystem::path& assetsRoot, const std::filesystem::path& file);
// Copies a sheet into the palette without replacing existing files. Returns its virtual path.
[[nodiscard]] std::optional<std::string> ImportMap2DImage(
    const std::filesystem::path& assetsRoot, const std::filesystem::path& source, std::string* error = nullptr);
// Pixel size of a PNG or BMP from its header.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> ReadMap2DImageSize(const std::filesystem::path& path);
// The sprite material of a sheet: unlit (black metal, the texture as emission), alpha
// masked so transparent pixels show what lies under them.
[[nodiscard]] bool WriteMap2DSpriteMaterial(const std::filesystem::path& file, std::string_view texture, std::string* error = nullptr);
// The index of the sheet cutting `texture` into cellSize cells, adding it and its material
// (/Game/Maps/Materials/M_<texture>_Sprites.kbmat, written once) when the map has none.
[[nodiscard]] std::optional<std::uint16_t> EnsureMap2DSheet(
    kb::assets::Map2DAsset& map,
    const std::filesystem::path& assetsRoot,
    std::string_view texture,
    std::uint32_t cellSize,
    std::string* error = nullptr,
    const Map2DSheetSetting& setting = {});
// Keeps the tiles that still fit.
void ResizeMap2D(kb::assets::Map2DAsset& map, std::uint32_t width, std::uint32_t height, Map2DChangeSet* changes = nullptr);

class Map2DEditor {
public:
    Map2DEditor(kb::assets::Map2DAsset& map, const Map2DBrushLibrary& brushes, std::filesystem::path assetsRoot);

    // Edits record what they change here; null records nothing.
    void SetChangeSet(Map2DChangeSet* changes) noexcept { changes_ = changes; }
    // Remere's Border Automagic: off, painting leaves the borders around it as they are.
    void SetAutoBorder(bool enabled) noexcept { autoBorder_ = enabled; }
    // Puts a whole tile (or clears it), as paste and delete do; borders are left to the caller.
    void SetTile(std::uint32_t x, std::uint32_t y, std::int32_t z, const std::optional<kb::assets::Map2DTile>& tile);

    [[nodiscard]] bool PaintGround(const Map2DGroundBrush& brush, Map2DRect area, std::int32_t z, std::string* error = nullptr);
    // A sprite as the ground (replacing it) or as an item stacked on top.
    [[nodiscard]] bool PaintSprite(
        std::string_view texture,
        std::uint32_t cellSize,
        std::uint32_t cell,
        bool asGround,
        Map2DRect area,
        std::int32_t z,
        std::string* error = nullptr,
        float animationPhaseSeconds = 0.0F);
    // The top item of each tile, or everything on it.
    void Erase(Map2DRect area, std::int32_t z, bool itemsOnly);
    void PaintCollision(Map2DRect area, std::int32_t z, bool solid);
    // Flood fills the connected ground under (x, y) with the brush; returns the tiles filled.
    [[nodiscard]] std::size_t FillGround(
        const Map2DGroundBrush& brush,
        std::uint32_t x,
        std::uint32_t y,
        std::int32_t z,
        std::string* error = nullptr);
    [[nodiscard]] std::size_t FillSprite(
        std::string_view texture, std::uint32_t cellSize, std::uint32_t cell,
        std::uint32_t x, std::uint32_t y, std::int32_t z, std::string* error = nullptr);
    // Recomputes the borders of every tile in the area from its neighbours' grounds.
    void RefreshBorders(Map2DRect area, std::int32_t z);
    // The borders around an edited area, when Border Automagic is on.
    void AutoBorder(Map2DRect area, std::int32_t z);

    // The brush whose ground this sprite is, or null.
    [[nodiscard]] const Map2DGroundBrush* BrushOf(kb::assets::Map2DSprite ground);
    [[nodiscard]] Map2DRect Clamp(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) const noexcept;

private:
    [[nodiscard]] std::size_t Fill(
        kb::assets::Map2DSprite sprite, const Map2DGroundBrush* brush,
        std::uint32_t x, std::uint32_t y, std::int32_t z, std::string* error);
    [[nodiscard]] kb::assets::Map2DTile& Mutable(std::uint64_t key);
    void DropIfEmpty(std::uint64_t key);
    void RefreshTileBorders(std::uint32_t x, std::uint32_t y, std::int32_t z);
    void RebuildBrushIndex();
    void EmbedRules();

    kb::assets::Map2DAsset& map_;
    const Map2DBrushLibrary& brushes_;
    std::filesystem::path assetsRoot_;
    Map2DChangeSet* changes_ = nullptr;
    bool autoBorder_ = true;
    std::unordered_map<std::uint64_t, const Map2DGroundBrush*> brushIndex_;
    std::size_t indexedSheets_ = static_cast<std::size_t>(-1);
};

} // namespace kb::world2d
