#include "Map2DEditing.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"

#include <algorithm>
#include <string>
#include <array>
#include <climits>
#include <cctype>
#include <deque>
#include <fstream>
#include <iterator>
#include <system_error>

namespace kb::world2d {
namespace {

using kb::assets::Map2DAsset;
using kb::assets::Map2DSprite;
using kb::assets::Map2DTile;
using kb::assets::Map2DTileKey;

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

[[nodiscard]] std::uint64_t SpriteKey(Map2DSprite sprite) noexcept {
    return (static_cast<std::uint64_t>(sprite.sheet) << 32U) | sprite.cell;
}

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::uint32_t ReadBigEndian32(const std::array<unsigned char, 32U>& bytes, std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::int32_t ReadLittleEndian32(const std::array<unsigned char, 32U>& bytes, std::size_t offset) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U));
}

// Neighbour bits, Remere's TileAlignment order.
enum NeighbourBit : std::uint8_t {
    kNorthWest = 1U, kNorth = 2U, kNorthEast = 4U, kWest = 8U, kEast = 16U, kSouthWest = 32U, kSouth = 64U, kSouthEast = 128U,
};

struct Neighbour {
    std::int32_t dx;
    std::int32_t dy;
    std::uint8_t bit;
};

constexpr std::array<Neighbour, 8U> kNeighbours{ {
    { -1, -1, kNorthWest }, { 0, -1, kNorth }, { 1, -1, kNorthEast }, { -1, 0, kWest },
    { 1, 0, kEast }, { -1, 1, kSouthWest }, { 0, 1, kSouth }, { 1, 1, kSouthEast },
} };

// The pieces a tile shows for the higher ground around it: a diagonal where it lies on two
// sides, an edge where it lies on one, an outer corner where it lies only diagonally.
void BorderCells(std::uint8_t mask, const Map2DGroundBrush& brush, std::vector<std::uint32_t>& cells) {
    using P = Map2DBorderPiece;
    const auto add = [&brush, &cells](P piece) {
        const std::uint32_t cell = brush.Border(piece);
        if (cell == kMap2DNoCell) return false;
        cells.push_back(cell);
        return true;
    };
    const bool north = (mask & kNorth) != 0U;
    const bool east = (mask & kEast) != 0U;
    const bool south = (mask & kSouth) != 0U;
    const bool west = (mask & kWest) != 0U;
    bool northDone = false;
    bool eastDone = false;
    bool southDone = false;
    bool westDone = false;
    if (north && west && add(P::DiagonalNW)) northDone = westDone = true;
    if (north && east && add(P::DiagonalNE)) northDone = eastDone = true;
    if (south && east && add(P::DiagonalSE)) southDone = eastDone = true;
    if (south && west && add(P::DiagonalSW)) southDone = westDone = true;
    if (north && !northDone) static_cast<void>(add(P::N));
    if (east && !eastDone) static_cast<void>(add(P::E));
    if (south && !southDone) static_cast<void>(add(P::S));
    if (west && !westDone) static_cast<void>(add(P::W));
    if ((mask & kNorthWest) != 0U && !north && !west) static_cast<void>(add(P::CornerNW));
    if ((mask & kNorthEast) != 0U && !north && !east) static_cast<void>(add(P::CornerNE));
    if ((mask & kSouthEast) != 0U && !south && !east) static_cast<void>(add(P::CornerSE));
    if ((mask & kSouthWest) != 0U && !south && !west) static_cast<void>(add(P::CornerSW));
}

} // namespace

Map2DBrushLibrary Map2DBrushLibrary::Load(const std::filesystem::path& assetsRoot, std::vector<std::string>* warnings) {
    Map2DBrushLibrary library;
    std::error_code error;
    if (!std::filesystem::is_directory(assetsRoot, error)) return library;
    for (std::filesystem::recursive_directory_iterator it{ assetsRoot, std::filesystem::directory_options::skip_permission_denied, error }, end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != kMap2DTilesetExtension) continue;
        std::string loadError;
        std::optional<Map2DTileset> tileset = Map2DTilesetIO::Load(it->path(), &loadError);
        if (!tileset.has_value()) {
            if (warnings != nullptr) warnings->push_back(loadError);
            continue;
        }
        library.files.push_back(Map2DTilesetFile{
            it->path(), Map2DVirtualPath(assetsRoot, it->path()), Map2DCategoryOf(assetsRoot, it->path()), std::move(*tileset) });
    }
    std::ranges::sort(library.files, {}, &Map2DTilesetFile::virtualPath);
    return library;
}

const Map2DGroundBrush* Map2DBrushLibrary::Find(std::string_view name) const noexcept {
    for (const Map2DTilesetFile& file : files) {
        if (const Map2DGroundBrush* brush = file.tileset.Find(name); brush != nullptr) return brush;
    }
    return nullptr;
}

std::string Map2DCategoryOf(const std::filesystem::path& assetsRoot, const std::filesystem::path& file) {
    std::error_code error;
    const std::filesystem::path sprites = std::filesystem::weakly_canonical(assetsRoot / kb::assets::Utf8Path(kMap2DSpritesFolder), error);
    const std::filesystem::path relative = std::filesystem::weakly_canonical(file, error).lexically_relative(sprites);
    const std::string text = Utf8(relative);
    if (relative.empty() || text.starts_with("..")) return Utf8(file.stem());
    const auto first = relative.begin();
    return std::next(first) == relative.end() ? std::string{ kMap2DGeneralCategory } : Utf8(*first);
}

std::filesystem::path Map2DCategoryTileset(const std::filesystem::path& assetsRoot, std::string_view category) {
    const std::filesystem::path sprites = assetsRoot / kb::assets::Utf8Path(kMap2DSpritesFolder);
    if (category.empty() || category == kMap2DGeneralCategory) return sprites / "Brushes.kbtileset";
    return sprites / kb::assets::Utf8Path(category) / (kb::assets::Utf8Path(category).string() + ".kbtileset");
}

const Map2DObjectBrush* Map2DBrushLibrary::FindObject(std::string_view name) const noexcept {
    for (const Map2DTilesetFile& file : files) {
        if (const Map2DObjectBrush* object = file.tileset.FindObject(name); object != nullptr) return object;
    }
    return nullptr;
}

std::vector<const Map2DObjectBrush*> Map2DBrushLibrary::Objects() const {
    std::vector<const Map2DObjectBrush*> objects;
    for (const Map2DTilesetFile& file : files) {
        for (const Map2DObjectBrush& object : file.tileset.objects) objects.push_back(&object);
    }
    return objects;
}

Map2DSheetSetting Map2DBrushLibrary::SheetSetting(std::string_view texture) const {
    for (const Map2DTilesetFile& file : files) {
        if (const Map2DSheetSetting* sheet = file.tileset.FindSheet(texture); sheet != nullptr) return *sheet;
    }
    return Map2DSheetSetting{ .texture = std::string{ texture } };
}

std::vector<std::uint32_t> Map2DBrushLibrary::UsedCells(std::string_view texture, std::uint32_t cellSize, std::uint32_t sheetColumns) const {
    std::vector<std::uint32_t> cells;
    for (const Map2DTilesetFile& file : files) {
        for (const Map2DGroundBrush& ground : file.tileset.grounds) {
            if (ground.texture != texture || ground.cellSize != cellSize) continue;
            cells.insert(cells.end(), ground.tiles.begin(), ground.tiles.end());
            for (const std::uint32_t cell : ground.borders) {
                if (cell != kMap2DNoCell) cells.push_back(cell);
            }
        }
        for (const Map2DObjectBrush& object : file.tileset.objects) {
            if (object.texture != texture || object.cellSize != cellSize) continue;
            if (sheetColumns == 0U) continue;
            for (std::uint32_t row = 0U; row < object.rows; ++row) {
                for (std::uint32_t column = 0U; column < object.columns; ++column) {
                    cells.push_back((object.firstCell / sheetColumns + row) * sheetColumns + object.firstCell % sheetColumns + column);
                }
            }
        }
    }
    return cells;
}

std::string_view Map2DBrushLibrary::CategoryOf(const void* brush) const noexcept {
    for (const Map2DTilesetFile& file : files) {
        for (const Map2DGroundBrush& ground : file.tileset.grounds) {
            if (&ground == brush) return file.category;
        }
        for (const Map2DObjectBrush& object : file.tileset.objects) {
            if (&object == brush) return file.category;
        }
    }
    return kMap2DGeneralCategory;
}

std::vector<const Map2DGroundBrush*> Map2DBrushLibrary::Grounds() const {
    std::vector<const Map2DGroundBrush*> grounds;
    for (const Map2DTilesetFile& file : files) {
        for (const Map2DGroundBrush& brush : file.tileset.grounds) grounds.push_back(&brush);
    }
    return grounds;
}

void Map2DChangeSet::Record(const Map2DAsset& map, std::uint64_t key) {
    if (before_.contains(key)) return;
    const auto found = map.tiles.find(key);
    before_.emplace(key, found == map.tiles.end() ? std::nullopt : std::optional<Map2DTile>{ found->second });
}

Map2DChangeSet::Settings::Settings(const Map2DAsset& map)
    : width(map.width), height(map.height), viewWidth(map.viewWidth), viewHeight(map.viewHeight),
      tileSize(map.tileSize), floorHeight(map.floorHeight), orientation(map.orientation),
      background(map.background), sheets(map.sheets), groundRules(map.groundRules),
      parallaxX(map.parallaxX), parallaxY(map.parallaxY), sortByY(map.sortByY), lit(map.lit) {}

void Map2DChangeSet::Settings::Apply(Map2DAsset& map) const {
    map.width = width; map.height = height; map.viewWidth = viewWidth; map.viewHeight = viewHeight;
    map.tileSize = tileSize; map.floorHeight = floorHeight; map.orientation = orientation;
    map.background = background; map.sheets = sheets;
    map.groundRules = groundRules; map.parallaxX = parallaxX; map.parallaxY = parallaxY; map.sortByY = sortByY; map.lit = lit;
}

void Map2DChangeSet::RecordSettings(const Map2DAsset& map) {
    if (!settingsBefore_) settingsBefore_.emplace(map);
}

void Map2DChangeSet::Finish(const Map2DAsset& map) {
    if (settingsBefore_) {
        settingsAfter_.emplace(map);
        if (settingsBefore_ == settingsAfter_) {
            settingsBefore_.reset();
            settingsAfter_.reset();
        }
    }
    after_.clear();
    for (auto it = before_.begin(); it != before_.end();) {
        const auto found = map.tiles.find(it->first);
        std::optional<Map2DTile> now = found == map.tiles.end() ? std::nullopt : std::optional<Map2DTile>{ found->second };
        if (now == it->second) {
            it = before_.erase(it);
            continue;
        }
        after_.emplace(it->first, std::move(now));
        ++it;
    }
}

void Map2DChangeSet::Undo(Map2DAsset& map) const {
    if (settingsBefore_) settingsBefore_->Apply(map);
    for (const auto& [key, tile] : before_) {
        if (tile.has_value()) map.tiles[key] = *tile;
        else map.tiles.erase(key);
    }
}

void Map2DChangeSet::Redo(Map2DAsset& map) const {
    if (settingsAfter_) settingsAfter_->Apply(map);
    for (const auto& [key, tile] : after_) {
        if (tile.has_value()) map.tiles[key] = *tile;
        else map.tiles.erase(key);
    }
}

bool Map2DChangeSet::Empty() const noexcept {
    return before_.empty() && !settingsBefore_.has_value();
}

std::filesystem::path Map2DAssetFile(const std::filesystem::path& assetsRoot, std::string_view virtualPath) {
    std::string_view relative = virtualPath;
    if (relative.starts_with("/Game/")) relative.remove_prefix(6U);
    else if (relative.starts_with("Assets/") || relative.starts_with("Assets\\")) relative.remove_prefix(7U);
    while (relative.starts_with('/')) relative.remove_prefix(1U);
    return assetsRoot / kb::assets::Utf8Path(relative);
}

std::string Map2DVirtualPath(const std::filesystem::path& assetsRoot, const std::filesystem::path& file) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::weakly_canonical(assetsRoot, error);
    const std::filesystem::path target = std::filesystem::weakly_canonical(file, error);
    const std::filesystem::path relative = target.lexically_relative(root);
    if (relative.empty() || Utf8(relative).starts_with("..")) return {};
    return "/Game/" + Utf8(relative);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> ReadMap2DImageSize(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    std::array<unsigned char, 32U> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()))) return std::nullopt;
    constexpr std::array<unsigned char, 8U> kPng{ 0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU };
    if (std::equal(kPng.begin(), kPng.end(), header.begin())) {
        return std::pair{ ReadBigEndian32(header, 16U), ReadBigEndian32(header, 20U) };
    }
    if (header[0] == 'B' && header[1] == 'M') {
        const std::int32_t width = ReadLittleEndian32(header, 18U);
        const std::int32_t height = ReadLittleEndian32(header, 22U);
        if (width > 0 && height != 0 && height != INT_MIN) {
            return std::pair{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height < 0 ? -height : height) };
        }
    }
    return std::nullopt;
}

std::optional<std::string> ImportMap2DImage(
    const std::filesystem::path& assetsRoot, const std::filesystem::path& source, std::string* error) {
    std::string extension = source.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto size = ReadMap2DImageSize(source);
    if ((extension != ".png" && extension != ".bmp") || !size || size->first == 0U || size->second == 0U ||
        size->first > 16384U || size->second > 16384U) {
        SetError(error, "Choose a PNG or BMP image up to 16384 pixels per side: " + Utf8(source.filename()));
        return std::nullopt;
    }
    const std::string existing = Map2DVirtualPath(assetsRoot, source);
    if (existing.starts_with("/Game/" + std::string{ kMap2DSpritesFolder } + "/")) return existing;
    const auto directory = assetsRoot / kb::assets::Utf8Path(kMap2DSpritesFolder) / "Imported";
    std::error_code fileError;
    std::filesystem::create_directories(directory, fileError);
    if (fileError) {
        SetError(error, "The graphics folder could not be created");
        return std::nullopt;
    }
    for (std::uint32_t suffix = 0U; suffix < 100000U; ++suffix) {
        const auto filename = Utf8(source.stem()) + (suffix == 0U ? std::string{} : " " + std::to_string(suffix + 1U)) + extension;
        const auto destination = directory / kb::assets::Utf8Path(filename);
        if (std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, fileError)) {
            return Map2DVirtualPath(assetsRoot, destination);
        }
        if (fileError != std::errc::file_exists) break;
        fileError.clear();
    }
    SetError(error, "The image could not be copied: " + Utf8(source.filename()));
    return std::nullopt;
}

bool WriteMap2DSpriteMaterial(const std::filesystem::path& file, std::string_view texture, std::string* error) {
    const std::uint64_t textureId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath(kb::assets::Utf8Path(texture)) + ":RenderTexture").value;
    kb::render::RenderMaterialAssetData material{};
    kb::render::RenderMaterialDesc& desc = material.desc;
    desc.baseColor[0] = 0.0F;
    desc.baseColor[1] = 0.0F;
    desc.baseColor[2] = 0.0F;
    desc.baseColor[3] = 1.0F;
    desc.emissiveColor[0] = 1.0F;
    desc.emissiveColor[1] = 1.0F;
    desc.emissiveColor[2] = 1.0F;
    desc.emissiveStrength = 1.0F;
    // Metal with a black base reflects nothing (its specular colour is its base colour): light
    // and sky add nothing, so a sprite shows exactly its own pixels, as in the Map Editor.
    desc.metallicFactor = 1.0F;
    desc.roughnessFactor = 1.0F;
    desc.alphaMode = kb::render::RenderMaterialAlphaMode::Mask;
    desc.alphaCutoff = 0.5F;
    desc.doubleSided = true;
    desc.albedoTextureAssetId = textureId;
    desc.emissiveTextureAssetId = textureId;
    std::error_code fileError;
    std::filesystem::create_directories(file.parent_path(), fileError);
    if (!kb::render::RenderMaterialAssetWriter::Save(file, material)) {
        SetError(error, "Sprite material could not be written: " + Utf8(file));
        return false;
    }
    return true;
}

std::optional<std::uint16_t> EnsureMap2DSheet(
    Map2DAsset& map,
    const std::filesystem::path& assetsRoot,
    std::string_view texture,
    std::uint32_t cellSize,
    std::string* error, const Map2DSheetSetting& setting) {
    auto layout = setting; layout.cellSize = cellSize;
    for (std::size_t index = 0U; index < map.sheets.size(); ++index) {
        if (map.sheets[index].texture == texture && map.sheets[index].cellSize == cellSize && map.sheets[index].CellHeight() == layout.Height() && map.sheets[index].margin == layout.margin && map.sheets[index].spacing == layout.spacing) return static_cast<std::uint16_t>(index);
    }
    if (map.sheets.size() >= Map2DAsset::MaximumSheets) {
        SetError(error, "The map already uses " + std::to_string(Map2DAsset::MaximumSheets) + " sprite sheets");
        return std::nullopt;
    }
    if (cellSize == 0U) {
        SetError(error, "Sprite cell size must be positive");
        return std::nullopt;
    }
    const std::filesystem::path textureFile = Map2DAssetFile(assetsRoot, texture);
    const std::optional<std::pair<std::uint32_t, std::uint32_t>> size = ReadMap2DImageSize(textureFile);
    if (!size.has_value()) {
        SetError(error, "Texture is missing or not a PNG/BMP: " + std::string{ texture });
        return std::nullopt;
    }
    if (!layout.Fits(size->first, size->second)) {
        SetError(error, "Choose a sprite size that divides both image dimensions: " + std::string{ texture });
        return std::nullopt;
    }
    const std::string stem = Utf8(std::filesystem::path{ textureFile.stem() });
    const std::string textureKey = kb::assets::NormalizeAssetPath(kb::assets::Utf8Path(texture));
    const std::string material = "/Game/Maps/Materials/M_" + stem + "_" +
        std::to_string(kb::assets::MakeAssetId(textureKey).value) + "_Sprites.kbmat";
    const std::filesystem::path materialFile = Map2DAssetFile(assetsRoot, material);
    std::error_code fileError;
    if (!std::filesystem::exists(materialFile, fileError) && !WriteMap2DSpriteMaterial(materialFile, texture, error)) return std::nullopt;
    map.sheets.push_back(kb::assets::Map2DSheet{
        .texture = std::string{ texture },
        .material = material,
        .cellSize = cellSize,
        .columns = layout.Count(size->first, cellSize),
        .rows = layout.Count(size->second, layout.Height()),
        .cellHeight = layout.cellHeight, .margin = layout.margin, .spacing = layout.spacing,
        .imageWidth = size->first, .imageHeight = size->second,
    });
    return static_cast<std::uint16_t>(map.sheets.size() - 1U);
}

void ResizeMap2D(Map2DAsset& map, std::uint32_t width, std::uint32_t height, Map2DChangeSet* changes) {
    if (changes != nullptr) changes->RecordSettings(map);
    map.width = std::clamp(width, 1U, Map2DAsset::MaximumSize);
    map.height = std::clamp(height, 1U, Map2DAsset::MaximumSize);
    std::erase_if(map.tiles, [&map, changes](const auto& entry) {
        const bool outside = kb::assets::Map2DTileKeyX(entry.first) >= map.width || kb::assets::Map2DTileKeyY(entry.first) >= map.height;
        if (outside && changes != nullptr) changes->Record(map, entry.first);
        return outside;
    });
}

Map2DEditor::Map2DEditor(Map2DAsset& map, const Map2DBrushLibrary& brushes, std::filesystem::path assetsRoot)
    : map_(map), brushes_(brushes), assetsRoot_(std::move(assetsRoot)) {}

Map2DBrushLibrary Map2DBrushLibrary::FromMap(const Map2DAsset& map) {
    Map2DBrushLibrary result;
    result.files.emplace_back();
    for (const auto& sheet : map.sheets) result.files.back().tileset.sheets.push_back({ .texture = sheet.texture, .cellSize = sheet.cellSize, .cellHeight = sheet.cellHeight, .margin = sheet.margin, .spacing = sheet.spacing });
    for (const auto& rule : map.groundRules) {
        if (rule.sheet >= map.sheets.size()) continue;
        const auto& sheet = map.sheets[rule.sheet];
        result.files.back().tileset.grounds.push_back(Map2DGroundBrush{ .name = rule.name,
            .zOrder = rule.order, .texture = sheet.texture, .cellSize = sheet.cellSize,
            .tiles = rule.variants, .weights = rule.weights, .borders = rule.borders });
    }
    return result;
}

void Map2DEditor::EmbedRules() {
    for (const auto* brush : brushes_.Grounds()) {
        const auto sheet = std::ranges::find_if(map_.sheets, [brush](const auto& item) {
            return item.texture == brush->texture && item.cellSize == brush->cellSize;
        });
        if (sheet == map_.sheets.end()) continue;
        kb::assets::Map2DGroundRule rule{ .name = brush->name, .order = brush->zOrder,
            .sheet = static_cast<std::uint16_t>(sheet - map_.sheets.begin()), .variants = brush->tiles,
            .weights = brush->weights, .borders = brush->borders };
        if (rule.weights.empty()) rule.weights.assign(rule.variants.size(), 1U);
        const auto existing = std::ranges::find(map_.groundRules, rule.name, &kb::assets::Map2DGroundRule::name);
        if (existing != map_.groundRules.end() && *existing == rule) continue;
        if (changes_) changes_->RecordSettings(map_);
        if (existing == map_.groundRules.end()) map_.groundRules.push_back(std::move(rule));
        else *existing = std::move(rule);
    }
}

Map2DRect Map2DEditor::Clamp(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) const noexcept {
    const auto clampX = [this](std::int64_t value) { return static_cast<std::uint32_t>(std::clamp<std::int64_t>(value, 0, map_.width - 1U)); };
    const auto clampY = [this](std::int64_t value) { return static_cast<std::uint32_t>(std::clamp<std::int64_t>(value, 0, map_.height - 1U)); };
    return Map2DRect{ clampX(std::min(x0, x1)), clampY(std::min(y0, y1)), clampX(std::max(x0, x1)), clampY(std::max(y0, y1)) };
}

void Map2DEditor::RebuildBrushIndex() {
    if (indexedSheets_ == map_.sheets.size()) return;
    indexedSheets_ = map_.sheets.size();
    brushIndex_.clear();
    for (const Map2DGroundBrush* brush : brushes_.Grounds()) {
        for (std::size_t sheet = 0U; sheet < map_.sheets.size(); ++sheet) {
            if (map_.sheets[sheet].texture != brush->texture || map_.sheets[sheet].cellSize != brush->cellSize) continue;
            for (const std::uint32_t cell : brush->tiles) {
                brushIndex_.try_emplace(SpriteKey(Map2DSprite{ static_cast<std::uint16_t>(sheet), cell }), brush);
            }
        }
    }
}

const Map2DGroundBrush* Map2DEditor::BrushOf(Map2DSprite ground) {
    if (!ground.IsValid()) return nullptr;
    RebuildBrushIndex();
    const auto found = brushIndex_.find(SpriteKey(ground));
    return found == brushIndex_.end() ? nullptr : found->second;
}

Map2DTile& Map2DEditor::Mutable(std::uint64_t key) {
    if (changes_ != nullptr) changes_->Record(map_, key);
    return map_.tiles[key];
}

void Map2DEditor::DropIfEmpty(std::uint64_t key) {
    const auto found = map_.tiles.find(key);
    if (found != map_.tiles.end() && found->second.IsEmpty()) map_.tiles.erase(found);
}

bool Map2DEditor::PaintGround(const Map2DGroundBrush& brush, Map2DRect area, std::int32_t z, std::string* error) {
    if (z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) return false;
    // An area that lies wholly outside the map is refused; it is not squeezed onto the edge cells.
    if (std::min(area.x0, area.x1) >= map_.width || std::min(area.y0, area.y1) >= map_.height) {
        if (error != nullptr) *error = "the area lies outside the map (cells 0.." + std::to_string(map_.width - 1U) + " by 0.." + std::to_string(map_.height - 1U) + ")";
        return false;
    }
    area = Clamp(area.x0, area.y0, area.x1, area.y1);
    if (brush.tiles.empty()) {
        SetError(error, "Ground " + brush.name + " has no tiles");
        return false;
    }
    const std::optional<std::uint16_t> sheet = EnsureMap2DSheet(map_, assetsRoot_, brush.texture, brush.cellSize, error, brushes_.SheetSetting(brush.texture));
    if (!sheet.has_value()) return false;
    const auto& info = map_.sheets[*sheet];
    if (std::ranges::any_of(brush.tiles, [&info](std::uint32_t cell) { return cell >= info.columns * info.rows; })) {
        SetError(error, "Ground " + brush.name + " uses a sprite outside its sheet");
        return false;
    }
    for (std::uint32_t y = area.y0; y <= area.y1; ++y) {
        for (std::uint32_t x = area.x0; x <= area.x1; ++x) {
            const Map2DSprite sprite{ *sheet, brush.VariantFor(x, y, z) };
            const std::uint64_t key = Map2DTileKey(x, y, z);
            const auto found = map_.tiles.find(key);
            if (found != map_.tiles.end() && BrushOf(found->second.ground) == &brush) continue;
            Mutable(key).ground = sprite;
        }
    }
    AutoBorder(Clamp(std::int64_t{ area.x0 } - 1, std::int64_t{ area.y0 } - 1, std::int64_t{ area.x1 } + 1, std::int64_t{ area.y1 } + 1), z);
    return true;
}

bool Map2DEditor::PaintSprite(
    std::string_view texture,
    std::uint32_t cellSize,
    std::uint32_t cell,
    bool asGround,
    Map2DRect area,
    std::int32_t z,
    std::string* error) {
    if (z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) return false;
    // An area that lies wholly outside the map is refused; it is not squeezed onto the edge cells.
    if (std::min(area.x0, area.x1) >= map_.width || std::min(area.y0, area.y1) >= map_.height) {
        if (error != nullptr) *error = "the area lies outside the map (cells 0.." + std::to_string(map_.width - 1U) + " by 0.." + std::to_string(map_.height - 1U) + ")";
        return false;
    }
    area = Clamp(area.x0, area.y0, area.x1, area.y1);
    const std::optional<std::uint16_t> sheet = EnsureMap2DSheet(map_, assetsRoot_, texture, cellSize, error, brushes_.SheetSetting(texture));
    if (!sheet.has_value()) return false;
    const kb::assets::Map2DSheet& info = map_.sheets[*sheet];
    if (cell >= info.columns * info.rows) {
        SetError(error, "Cell " + std::to_string(cell) + " is outside " + std::string{ texture });
        return false;
    }
    const Map2DSprite sprite{ *sheet, cell };
    for (std::uint32_t y = area.y0; y <= area.y1; ++y) {
        for (std::uint32_t x = area.x0; x <= area.x1; ++x) {
            const std::uint64_t key = Map2DTileKey(x, y, z);
            const auto found = map_.tiles.find(key);
            if (asGround) {
                if (found != map_.tiles.end() && found->second.ground == sprite) continue;
                Mutable(key).ground = sprite;
                continue;
            }
            if (found != map_.tiles.end() &&
                ((!found->second.items.empty() && found->second.items.back() == sprite) ||
                 found->second.borders.size() + found->second.items.size() >= Map2DAsset::MaximumStack)) {
                continue;
            }
            Mutable(key).items.push_back(sprite);
        }
    }
    if (asGround) {
        AutoBorder(Clamp(std::int64_t{ area.x0 } - 1, std::int64_t{ area.y0 } - 1, std::int64_t{ area.x1 } + 1, std::int64_t{ area.y1 } + 1), z);
    }
    return true;
}

void Map2DEditor::Erase(Map2DRect area, std::int32_t z, bool itemsOnly) {
    // An area that lies wholly outside the map changes nothing; it is not squeezed onto the edge cells.
    if (std::min(area.x0, area.x1) >= map_.width || std::min(area.y0, area.y1) >= map_.height) return;
    area = Clamp(area.x0, area.y0, area.x1, area.y1);
    bool groundChanged = false;
    for (std::uint32_t y = area.y0; y <= area.y1; ++y) {
        for (std::uint32_t x = area.x0; x <= area.x1; ++x) {
            const std::uint64_t key = Map2DTileKey(x, y, z);
            const auto found = map_.tiles.find(key);
            if (found == map_.tiles.end()) continue;
            if (itemsOnly) {
                if (found->second.items.empty()) continue;
                Mutable(key).items.pop_back();
            } else {
                groundChanged = groundChanged || found->second.ground.IsValid();
                Map2DTile& tile = Mutable(key);
                tile.ground = {};
                tile.items.clear();
                tile.solid = false;
            }
            DropIfEmpty(key);
        }
    }
    if (groundChanged) {
        AutoBorder(Clamp(std::int64_t{ area.x0 } - 1, std::int64_t{ area.y0 } - 1, std::int64_t{ area.x1 } + 1, std::int64_t{ area.y1 } + 1), z);
    }
}

void Map2DEditor::PaintCollision(Map2DRect area, std::int32_t z, bool solid) {
    if (z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) return;
    // An area that lies wholly outside the map changes nothing; it is not squeezed onto the edge cells.
    if (std::min(area.x0, area.x1) >= map_.width || std::min(area.y0, area.y1) >= map_.height) return;
    area = Clamp(area.x0, area.y0, area.x1, area.y1);
    for (std::uint32_t y = area.y0; y <= area.y1; ++y) {
        for (std::uint32_t x = area.x0; x <= area.x1; ++x) {
            const auto key = Map2DTileKey(x, y, z);
            const auto found = map_.tiles.find(key);
            if ((found != map_.tiles.end() && found->second.solid) == solid) continue;
            Mutable(key).solid = solid;
            DropIfEmpty(key);
        }
    }
}

std::size_t Map2DEditor::FillGround(
    const Map2DGroundBrush& brush,
    std::uint32_t x,
    std::uint32_t y,
    std::int32_t z,
    std::string* error) {
    const auto sheet = EnsureMap2DSheet(map_, assetsRoot_, brush.texture, brush.cellSize, error, brushes_.SheetSetting(brush.texture));
    if (!sheet || brush.tiles.empty()) return 0U;
    const auto& info = map_.sheets[*sheet];
    if (std::ranges::any_of(brush.tiles, [&info](std::uint32_t cell) { return cell >= info.columns * info.rows; })) {
        SetError(error, "Ground " + brush.name + " uses a sprite outside its sheet");
        return 0U;
    }
    return Fill(Map2DSprite{ *sheet, brush.tiles.front() }, &brush, x, y, z, error);
}

std::size_t Map2DEditor::FillSprite(
    std::string_view texture, std::uint32_t cellSize, std::uint32_t cell,
    std::uint32_t x, std::uint32_t y, std::int32_t z, std::string* error) {
    const auto sheet = EnsureMap2DSheet(map_, assetsRoot_, texture, cellSize, error, brushes_.SheetSetting(texture));
    if (!sheet) return 0U;
    const auto& info = map_.sheets[*sheet];
    if (cell >= info.columns * info.rows) {
        SetError(error, "The sprite is outside its sheet");
        return 0U;
    }
    return Fill(Map2DSprite{ *sheet, cell }, nullptr, x, y, z, error);
}

std::size_t Map2DEditor::Fill(
    Map2DSprite sprite, const Map2DGroundBrush* brush,
    std::uint32_t x, std::uint32_t y, std::int32_t z, std::string* error) {
    if (x >= map_.width || y >= map_.height || z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) return 0U;
    const auto groundAt = [this, z](std::uint32_t tileX, std::uint32_t tileY) {
        const auto found = map_.tiles.find(Map2DTileKey(tileX, tileY, z));
        return found == map_.tiles.end() ? Map2DSprite{} : found->second.ground;
    };
    // Grounds of one brush are one region whatever their variant; other grounds only match themselves.
    const Map2DSprite start = groundAt(x, y);
    const Map2DGroundBrush* startBrush = BrushOf(start);
    if ((brush != nullptr && startBrush == brush) || (brush == nullptr && start == sprite)) return 0U;
    const auto matches = [&](Map2DSprite ground) {
        const Map2DGroundBrush* groundBrush = BrushOf(ground);
        return startBrush != nullptr ? groundBrush == startBrush : (groundBrush == nullptr && ground == start);
    };
    constexpr std::size_t kLimit = 1U << 20U;
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(map_.width) * map_.height, 0U);
    std::deque<std::pair<std::uint32_t, std::uint32_t>> queue{ { x, y } };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> region;
    visited[static_cast<std::size_t>(y) * map_.width + x] = 1U;
    Map2DRect bounds{ x, y, x, y };
    while (!queue.empty() && region.size() < kLimit) {
        const auto [tileX, tileY] = queue.front();
        queue.pop_front();
        region.emplace_back(tileX, tileY);
        bounds = Map2DRect{ std::min(bounds.x0, tileX), std::min(bounds.y0, tileY), std::max(bounds.x1, tileX), std::max(bounds.y1, tileY) };
        const std::array<std::pair<std::int64_t, std::int64_t>, 4U> steps{ { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } } };
        for (const auto [dx, dy] : steps) {
            const std::int64_t nextX = std::int64_t{ tileX } + dx;
            const std::int64_t nextY = std::int64_t{ tileY } + dy;
            if (nextX < 0 || nextY < 0 || nextX >= map_.width || nextY >= map_.height) continue;
            std::uint8_t& seen = visited[static_cast<std::size_t>(nextY) * map_.width + static_cast<std::size_t>(nextX)];
            if (seen != 0U || !matches(groundAt(static_cast<std::uint32_t>(nextX), static_cast<std::uint32_t>(nextY)))) continue;
            seen = 1U;
            queue.emplace_back(static_cast<std::uint32_t>(nextX), static_cast<std::uint32_t>(nextY));
        }
    }
    if (!queue.empty()) {
        SetError(error, "This area is too large to fill at once; paint a smaller enclosed area first");
        return 0U;
    }
    for (const auto [tileX, tileY] : region) {
        Mutable(Map2DTileKey(tileX, tileY, z)).ground = brush != nullptr
            ? Map2DSprite{ sprite.sheet, brush->VariantFor(tileX, tileY, z) } : sprite;
    }
    AutoBorder(Clamp(std::int64_t{ bounds.x0 } - 1, std::int64_t{ bounds.y0 } - 1, std::int64_t{ bounds.x1 } + 1, std::int64_t{ bounds.y1 } + 1), z);
    return region.size();
}

void Map2DEditor::AutoBorder(Map2DRect area, std::int32_t z) {
    if (autoBorder_) RefreshBorders(area, z);
}

void Map2DEditor::SetTile(std::uint32_t x, std::uint32_t y, std::int32_t z, const std::optional<Map2DTile>& tile) {
    if (x >= map_.width || y >= map_.height || z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) return;
    const std::uint64_t key = Map2DTileKey(x, y, z);
    const auto found = map_.tiles.find(key);
    if (!tile.has_value() || tile->IsEmpty()) {
        if (found == map_.tiles.end()) return;
        if (changes_ != nullptr) changes_->Record(map_, key);
        map_.tiles.erase(key);
        return;
    }
    if (found != map_.tiles.end() && found->second == *tile) return;
    Mutable(key) = *tile;
}

void Map2DEditor::RefreshBorders(Map2DRect area, std::int32_t z) {
    EmbedRules();
    for (std::uint32_t y = area.y0; y <= area.y1; ++y) {
        for (std::uint32_t x = area.x0; x <= area.x1; ++x) RefreshTileBorders(x, y, z);
    }
}

void Map2DEditor::RefreshTileBorders(std::uint32_t x, std::uint32_t y, std::int32_t z) {
    const std::uint64_t key = Map2DTileKey(x, y, z);
    const auto found = map_.tiles.find(key);
    const Map2DSprite ground = found == map_.tiles.end() ? Map2DSprite{} : found->second.ground;
    const Map2DGroundBrush* own = BrushOf(ground);
    // A ground no brush knows sits at z-order 0; an empty tile is below every ground.
    const std::int64_t ownZ = own != nullptr ? own->zOrder : (ground.IsValid() ? 0 : std::int64_t{ INT_MIN } - 1);

    std::vector<std::pair<const Map2DGroundBrush*, std::uint8_t>> higher;
    for (const Neighbour& neighbour : kNeighbours) {
        const std::int64_t nextX = std::int64_t{ x } + neighbour.dx;
        const std::int64_t nextY = std::int64_t{ y } + neighbour.dy;
        if (nextX < 0 || nextY < 0 || nextX >= map_.width || nextY >= map_.height) continue;
        const auto next = map_.tiles.find(Map2DTileKey(static_cast<std::uint32_t>(nextX), static_cast<std::uint32_t>(nextY), z));
        if (next == map_.tiles.end()) continue;
        const Map2DGroundBrush* brush = BrushOf(next->second.ground);
        if (brush == nullptr || brush == own || brush->zOrder <= ownZ || !brush->HasBorders()) continue;
        const auto entry = std::ranges::find(higher, brush, &std::pair<const Map2DGroundBrush*, std::uint8_t>::first);
        if (entry == higher.end()) higher.emplace_back(brush, neighbour.bit);
        else entry->second = static_cast<std::uint8_t>(entry->second | neighbour.bit);
    }
    std::ranges::sort(higher, [](const auto& left, const auto& right) {
        return left.first->zOrder != right.first->zOrder ? left.first->zOrder < right.first->zOrder : left.first->name < right.first->name;
    });
    std::vector<Map2DSprite> borders;
    std::vector<std::uint32_t> cells;
    for (const auto& [brush, mask] : higher) {
        const std::optional<std::uint16_t> sheet = EnsureMap2DSheet(map_, assetsRoot_, brush->texture, brush->cellSize, nullptr, brushes_.SheetSetting(brush->texture));
        if (!sheet.has_value()) continue;
        const kb::assets::Map2DSheet& info = map_.sheets[*sheet];
        cells.clear();
        BorderCells(mask, *brush, cells);
        for (const std::uint32_t cell : cells) {
            if (cell < info.columns * info.rows) borders.push_back(Map2DSprite{ *sheet, cell });
        }
    }
    const auto current = map_.tiles.find(key);
    const std::size_t items = current == map_.tiles.end() ? 0U : current->second.items.size();
    if (borders.size() + items > Map2DAsset::MaximumStack) borders.resize(Map2DAsset::MaximumStack - std::min(items, Map2DAsset::MaximumStack));
    if (current == map_.tiles.end() ? borders.empty() : current->second.borders == borders) return;
    Map2DTile& tile = Mutable(key);
    tile.borders = std::move(borders);
    DropIfEmpty(key);
}

} // namespace kb::world2d
