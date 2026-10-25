#include "engine/assets/Map2DAsset.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace kb::assets {
namespace {

constexpr std::string_view kHeader = "21kb-map2d";
constexpr std::size_t kMaximumFileBytes = 256U * 1024U * 1024U;

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

// Whitespace separated words; a "quoted word" may hold spaces.
[[nodiscard]] std::vector<std::string_view> Words(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t index = 0U;
    while (index < line.size()) {
        while (index < line.size() && (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) ++index;
        if (index >= line.size() || line[index] == '#') break;
        if (line[index] == '"') {
            const std::size_t end = line.find('"', index + 1U);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end;
            words.push_back(line.substr(index + 1U, stop - index - 1U));
            index = stop + 1U;
            continue;
        }
        const std::size_t start = index;
        while (index < line.size() && line[index] != ' ' && line[index] != '\t' && line[index] != '\r') ++index;
        words.push_back(line.substr(start, index - start));
    }
    return words;
}

template <typename T>
[[nodiscard]] bool ParseNumber(std::string_view text, T& output) noexcept {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& output) noexcept {
    if (text == "true" || text == "1" || text == "on") {
        output = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "off") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseSpriteList(std::string_view text, std::vector<Map2DSprite>& output) {
    output.clear();
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::optional<Map2DSprite> sprite = ParseMap2DSprite(text.substr(0U, comma));
        if (!sprite.has_value()) return false;
        output.push_back(*sprite);
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1U);
    }
    return true;
}

void WriteQuoted(std::ostream& output, std::string_view text) {
    output << '"' << text << '"';
}

void WriteSpriteList(std::ostream& output, const std::vector<Map2DSprite>& sprites) {
    for (std::size_t index = 0U; index < sprites.size(); ++index) {
        if (index != 0U) output << ',';
        output << FormatMap2DSprite(sprites[index]);
    }
}

[[nodiscard]] std::string FloatText(float value) {
    std::array<char, 32U> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return error == std::errc{} ? std::string{ buffer.data(), end } : std::string{ "0" };
}

std::string Hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    for (unsigned char byte : value) { output += digits[byte >> 4U]; output += digits[byte & 15U]; }
    return output.empty() ? "-" : output;
}
std::optional<std::string> Unhex(std::string_view value) {
    if (value == "-") return std::string{};
    if (value.size() % 2U != 0U) return {};
    std::string output;
    for (std::size_t i = 0; i < value.size(); i += 2U) {
        unsigned byte = 0;
        const auto [end, error] = std::from_chars(value.data() + i, value.data() + i + 2, byte, 16);
        if (error != std::errc{} || end != value.data() + i + 2) return {};
        output += static_cast<char>(byte);
    }
    return output;
}
bool ReadCollision(const std::vector<std::string_view>& words, std::size_t begin, Map2DCollision& output) {
    unsigned kind = 0;
    if (words.size() < begin + 4U || !ParseNumber(words[begin], kind) || kind > 5U ||
        !ParseBool(words[begin + 1], output.trigger) || !ParseNumber(words[begin + 2], output.friction) ||
        !ParseNumber(words[begin + 3], output.restitution) || (words.size() - begin - 4U) % 2U) return false;
    output.kind = static_cast<Map2DCollisionKind>(kind);
    for (std::size_t i = begin + 4U; i < words.size(); i += 2U) {
        std::array<float, 2> point{};
        if (!ParseNumber(words[i], point[0]) || !ParseNumber(words[i + 1], point[1])) return false;
        output.polygon.push_back(point);
    }
    return IsMap2DCollisionValid(output);
}
void WriteCollision(std::ostream& out, const Map2DCollision& value) {
    out << static_cast<unsigned>(value.kind) << ' ' << value.trigger << ' ' << FloatText(value.friction) << ' ' << FloatText(value.restitution);
    for (const auto& point : value.polygon) out << ' ' << FloatText(point[0]) << ' ' << FloatText(point[1]);
    out << '\n';
}
bool ReadProperty(const std::vector<std::string_view>& words, std::size_t begin, Map2DProperties& output) {
    if (words.size() != begin + 3U) return false;
    const auto key = Unhex(words[begin + 1]);
    if (!key) return false;
    const auto value = words[begin + 2];
    if (words[begin] == "bool") { bool parsed = false; if (!ParseBool(value, parsed)) return false; output[*key] = parsed; }
    else if (words[begin] == "int") { std::int64_t parsed = 0; if (!ParseNumber(value, parsed)) return false; output[*key] = parsed; }
    else if (words[begin] == "float") { double parsed = 0; if (!ParseNumber(value, parsed) || !std::isfinite(parsed)) return false; output[*key] = parsed; }
    else if (words[begin] == "string") { const auto parsed = Unhex(value); if (!parsed) return false; output[*key] = *parsed; }
    else return false;
    return true;
}
void WriteProperties(std::ostream& out, std::string_view prefix, const Map2DProperties& properties) {
    for (const auto& [key, value] : properties) {
        out << prefix;
        if (const auto* item = std::get_if<bool>(&value)) out << " bool " << Hex(key) << ' ' << *item;
        else if (const auto* integer = std::get_if<std::int64_t>(&value)) out << " int " << Hex(key) << ' ' << *integer;
        else if (const auto* number = std::get_if<double>(&value)) {
            std::array<char, 64> buffer{};
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *number);
            out << " float " << Hex(key) << ' ' << std::string_view(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
        } else out << " string " << Hex(key) << ' ' << Hex(std::get<std::string>(value));
        out << '\n';
    }
}
bool PropertiesValid(const Map2DProperties& properties) {
    if (properties.size() > 64U) return false;
    for (const auto& [key, value] : properties) {
        if (key.empty() || key.size() > 128U) return false;
        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number)) return false;
        if (const auto* string = std::get_if<std::string>(&value); string && string->size() > 4096U) return false;
    }
    return true;
}

} // namespace

bool IsMap2DCollisionValid(const Map2DCollision& value) noexcept {
    if (value.kind > Map2DCollisionKind::Polygon || !std::isfinite(value.friction) || value.friction < 0.0F || value.friction > 10.0F ||
        !std::isfinite(value.restitution) || value.restitution < 0.0F || value.restitution > 1.0F) return false;
    if (value.kind != Map2DCollisionKind::Polygon) return value.polygon.empty();
    if (value.polygon.size() < 3U || value.polygon.size() > 16U) return false;
    float area = 0.0F;
    const auto cross = [](auto a, auto b, auto c) { return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]); };
    for (std::size_t i = 0; i < value.polygon.size(); ++i) {
        const auto a = value.polygon[i], b = value.polygon[(i + 1) % value.polygon.size()];
        for (float coordinate : a) if (!std::isfinite(coordinate) || coordinate < 0.0F || coordinate > 1.0F) return false;
        if (a == b) return false;
        area += a[0]*b[1] - a[1]*b[0];
        for (std::size_t j = i + 2; j < value.polygon.size(); ++j) {
            if ((j + 1) % value.polygon.size() == i) continue;
            const auto c = value.polygon[j], d = value.polygon[(j + 1) % value.polygon.size()];
            if (std::max(std::min(a[0], b[0]), std::min(c[0], d[0])) <= std::min(std::max(a[0], b[0]), std::max(c[0], d[0])) &&
                std::max(std::min(a[1], b[1]), std::min(c[1], d[1])) <= std::min(std::max(a[1], b[1]), std::max(c[1], d[1])) &&
                cross(a,b,c)*cross(a,b,d) <= 0 && cross(c,d,a)*cross(c,d,b) <= 0) return false;
        }
    }
    return std::abs(area) > 0.0001F;
}

const Map2DTileDefinition* Map2DDefinition(const Map2DAsset& map, Map2DSprite sprite) noexcept {
    if (!sprite.IsValid() || sprite.sheet >= map.sheets.size()) return nullptr;
    const auto& definitions = map.sheets[sprite.sheet].definitions;
    const auto found = definitions.find(sprite.cell);
    return found == definitions.end() ? nullptr : &found->second;
}
Map2DSprite Map2DAnimatedSprite(const Map2DAsset& map, Map2DSprite sprite, double seconds) noexcept {
    if (const auto* definition = Map2DDefinition(map, sprite); definition && !definition->frames.empty() && std::isfinite(seconds)) {
        const auto frame = static_cast<std::size_t>(std::fmod(std::max(0.0, seconds) * definition->framesPerSecond, static_cast<double>(definition->frames.size())));
        sprite.cell = definition->frames[frame];
    }
    return sprite;
}
std::array<float, 2> Map2DSpriteUV(Map2DSprite sprite, float u, float v) noexcept {
    for (unsigned turn = 0; turn < sprite.rotation; ++turn) { const auto previous = u; u = v; v = 1.0F - previous; }
    return { sprite.flipX ? 1.0F - u : u, sprite.flipY ? 1.0F - v : v };
}
namespace {
Map2DCollision GraphicCollision(const Map2DTileDefinition& definition, Map2DSprite sprite) {
    auto shape = definition.collision;
    if ((!sprite.rotation && !sprite.flipX && !sprite.flipY) || shape.kind == Map2DCollisionKind::None || shape.kind == Map2DCollisionKind::Solid || shape.kind == Map2DCollisionKind::OneWay) return shape;
    if (shape.kind == Map2DCollisionKind::SlopeUp) shape.polygon = {{0,1},{1,0},{1,1}};
    if (shape.kind == Map2DCollisionKind::SlopeDown) shape.polygon = {{0,0},{1,1},{0,1}};
    shape.kind = Map2DCollisionKind::Polygon;
    for (auto& point : shape.polygon) {
        if (sprite.flipX) point[0] = 1.0F - point[0];
        if (sprite.flipY) point[1] = 1.0F - point[1];
        for (unsigned turn = 0; turn < sprite.rotation; ++turn) { const auto x = point[0]; point[0] = 1.0F - point[1]; point[1] = x; }
    }
    return shape;
}
}
Map2DCollision Map2DCellCollision(const Map2DAsset& map, const Map2DTile& tile) {
    if (tile.collision) return *tile.collision;
    if (tile.solid) return Map2DCollision{ .kind = Map2DCollisionKind::Solid };
    for (auto it = tile.items.rbegin(); it != tile.items.rend(); ++it) {
        if (const auto* definition = Map2DDefinition(map, *it); definition && definition->collision.kind != Map2DCollisionKind::None) return GraphicCollision(*definition, *it);
    }
    if (const auto* definition = Map2DDefinition(map, tile.ground)) return GraphicCollision(*definition, tile.ground);
    return {};
}
std::optional<Map2DValue> Map2DCellProperty(const Map2DAsset& map, const Map2DTile& tile, std::string_view name) {
    const auto own = tile.properties.find(std::string(name));
    if (own != tile.properties.end()) return own->second;
    const auto lookup = [&](Map2DSprite sprite) -> std::optional<Map2DValue> {
        const auto* definition = Map2DDefinition(map, sprite);
        if (!definition) return {};
        const auto found = definition->properties.find(std::string(name));
        return found == definition->properties.end() ? std::nullopt : std::optional<Map2DValue>(found->second);
    };
    for (auto it = tile.items.rbegin(); it != tile.items.rend(); ++it) if (auto value = lookup(*it)) return value;
    return lookup(tile.ground);
}

bool IsMap2DAssetValid(const Map2DAsset& map, std::string* error) {
    if (map.width == 0U || map.height == 0U || map.width > Map2DAsset::MaximumSize || map.height > Map2DAsset::MaximumSize) {
        SetError(error, "Map size must be 1.." + std::to_string(Map2DAsset::MaximumSize) + " tiles each way");
        return false;
    }
    if (!std::isfinite(map.tileSize) || map.tileSize <= 0.0F || !std::isfinite(map.floorHeight) || map.floorHeight < 0.0F) {
        SetError(error, "Map tileSize must be positive and floorHeight not negative");
        return false;
    }
    if (map.viewWidth == 0U || map.viewHeight == 0U || map.viewWidth > 255U || map.viewHeight > 255U) {
        SetError(error, "Map view must be 1..255 tiles each way");
        return false;
    }
    if (!std::isfinite(map.parallaxX) || !std::isfinite(map.parallaxY) || std::abs(map.parallaxX) > 100.0F || std::abs(map.parallaxY) > 100.0F) { SetError(error, "Invalid parallax factor"); return false; }
    if (map.sheets.size() > Map2DAsset::MaximumSheets) {
        SetError(error, "Map uses more than " + std::to_string(Map2DAsset::MaximumSheets) + " sprite sheets");
        return false;
    }
    for (const Map2DSheet& sheet : map.sheets) {
        if (sheet.texture.empty() || sheet.cellSize == 0U || sheet.cellSize > 16384U || sheet.columns == 0U || sheet.rows == 0U ||
            sheet.margin > 16384U || sheet.spacing > 16384U || sheet.CellHeight() > 16384U ||
            static_cast<std::uint64_t>(sheet.columns) * (sheet.cellSize + sheet.spacing) + 2ULL * sheet.margin - sheet.spacing > sheet.ImageWidth() ||
            static_cast<std::uint64_t>(sheet.rows) * (sheet.CellHeight() + sheet.spacing) + 2ULL * sheet.margin - sheet.spacing > sheet.ImageHeight() ||
            sheet.ImageWidth() > 16384U || sheet.ImageHeight() > 16384U ||
            sheet.texture.find_first_of("\"\r\n") != std::string::npos || sheet.material.find_first_of("\"\r\n") != std::string::npos) {
            SetError(error, "Map sheet needs a texture, a cell size and its columns and rows");
            return false;
        }
    }
    for (const auto& sheet : map.sheets) for (const auto& [cell, definition] : sheet.definitions) {
        if (cell >= sheet.columns * sheet.rows || definition.frames.size() > 256U || !std::isfinite(definition.framesPerSecond) ||
            definition.framesPerSecond <= 0.0F || definition.framesPerSecond > 240.0F || !IsMap2DCollisionValid(definition.collision) ||
            !PropertiesValid(definition.properties)) { SetError(error, "Invalid tile definition"); return false; }
        for (auto frame : definition.frames) if (frame >= sheet.columns * sheet.rows) { SetError(error, "Animation frame is outside the sheet"); return false; }
    }
    if (map.groundRules.size() > 4096U) { SetError(error, "Too many ground rules"); return false; }
    for (const auto& rule : map.groundRules) {
        if (rule.sheet >= map.sheets.size() || rule.name.empty() || rule.name.find_first_of("\"\r\n") != std::string::npos || rule.variants.empty() ||
            (!rule.weights.empty() && rule.weights.size() != rule.variants.size())) { SetError(error, "Invalid ground rule"); return false; }
        const auto count = map.sheets[rule.sheet].columns * map.sheets[rule.sheet].rows;
        for (auto cell : rule.variants) if (cell >= count) { SetError(error, "Ground variant is outside its sheet"); return false; }
        for (auto cell : rule.borders) if (cell != UINT32_MAX && cell >= count) { SetError(error, "Ground border is outside its sheet"); return false; }
    }
    const auto spriteValid = [&map](Map2DSprite sprite) {
        return sprite.rotation < 4U && std::isfinite(sprite.offsetX) && std::isfinite(sprite.offsetY) && std::abs(sprite.offsetX) <= 16.0F && std::abs(sprite.offsetY) <= 16.0F && sprite.sheet < map.sheets.size() &&
            sprite.cell < map.sheets[sprite.sheet].columns * map.sheets[sprite.sheet].rows;
    };
    for (const auto& [key, tile] : map.tiles) {
        if (Map2DTileKeyX(key) >= map.width || Map2DTileKeyY(key) >= map.height ||
            Map2DTileKeyZ(key) < Map2DAsset::MinimumFloor || Map2DTileKeyZ(key) > Map2DAsset::MaximumFloor) {
            SetError(error, "Map tile lies outside the map");
            return false;
        }
        if ((tile.collision && !IsMap2DCollisionValid(*tile.collision)) || !PropertiesValid(tile.properties) ||
            (!tile.prefab.empty() && (!tile.prefab.starts_with("/Game/") || !tile.prefab.ends_with(".kbprefab") || tile.prefab.find_first_of("\"\r\n") != std::string::npos))) {
            SetError(error, "Invalid tile collision, properties or prefab"); return false;
        }
        if ((tile.ground.IsValid() && !spriteValid(tile.ground)) ||
            tile.borders.size() + tile.items.size() > Map2DAsset::MaximumStack) {
            SetError(error, "Map tile has an invalid sprite or too many sprites");
            return false;
        }
        for (const Map2DSprite sprite : tile.borders) {
            if (!spriteValid(sprite)) {
                SetError(error, "Map tile border uses a sprite outside its sheet");
                return false;
            }
        }
        for (const Map2DSprite sprite : tile.items) {
            if (!spriteValid(sprite)) {
                SetError(error, "Map tile item uses a sprite outside its sheet");
                return false;
            }
        }
    }
    return true;
}

std::string_view Map2DOrientationName(Map2DOrientation orientation) noexcept {
    return orientation == Map2DOrientation::Wall ? "wall" : "flat";
}

Map2DAsset MakeEmptyMap2DAsset(std::uint32_t width, std::uint32_t height) {
    Map2DAsset map{};
    map.width = width;
    map.height = height;
    return map;
}

bool Map2DSpriteRect(
    const Map2DAsset& map,
    Map2DSprite sprite,
    std::uint32_t& left,
    std::uint32_t& top,
    std::uint32_t& size) noexcept {
    std::uint32_t height = 0U;
    return Map2DSpriteRect(map, sprite, left, top, size, height);
}

bool Map2DSpriteRect(const Map2DAsset& map, Map2DSprite sprite, std::uint32_t& left, std::uint32_t& top,
    std::uint32_t& width, std::uint32_t& height) noexcept {
    if (!sprite.IsValid() || sprite.sheet >= map.sheets.size()) return false;
    const auto& sheet = map.sheets[sprite.sheet];
    if (!sheet.columns || sprite.cell >= sheet.columns * sheet.rows) return false;
    width = sheet.cellSize; height = sheet.CellHeight();
    left = sheet.margin + (sprite.cell % sheet.columns) * (width + sheet.spacing);
    top = sheet.margin + (sprite.cell / sheet.columns) * (height + sheet.spacing);
    return true;
}
std::string FormatMap2DSprite(Map2DSprite sprite) {
    auto text = std::to_string(sprite.sheet) + ":" + std::to_string(sprite.cell);
    if (sprite.rotation || sprite.flipX || sprite.flipY || sprite.tint != 0xFFFFFFFFU || sprite.offsetX != 0.0F || sprite.offsetY != 0.0F)
        text += "@" + std::to_string(sprite.rotation) + "/" + std::to_string((sprite.flipX ? 1 : 0) | (sprite.flipY ? 2 : 0)) + "/" +
            std::to_string(sprite.tint) + "/" + FloatText(sprite.offsetX) + "/" + FloatText(sprite.offsetY);
    return text;
}
std::optional<Map2DSprite> ParseMap2DSprite(std::string_view text) noexcept {
    const auto at = text.find('@');
    const auto base = text.substr(0, at);
    const auto colon = base.find(':');
    Map2DSprite sprite{};
    if (colon == std::string_view::npos || !ParseNumber(base.substr(0, colon), sprite.sheet) ||
        !ParseNumber(base.substr(colon + 1), sprite.cell) || sprite.sheet == Map2DSprite::kNoSheet) return {};
    if (at != std::string_view::npos) {
        text.remove_prefix(at + 1);
        std::array<std::string_view, 5> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto slash = text.find('/'); values[i] = text.substr(0, slash);
            if (i != values.size() - 1 && slash == std::string_view::npos) return {};
            text.remove_prefix(slash == std::string_view::npos ? text.size() : slash + 1);
        }
        unsigned rotation = 0, flip = 0;
        if (!text.empty() || !ParseNumber(values[0], rotation) || rotation > 3 || !ParseNumber(values[1], flip) || flip > 3 ||
            !ParseNumber(values[2], sprite.tint) || !ParseNumber(values[3], sprite.offsetX) || !ParseNumber(values[4], sprite.offsetY)) return {};
        sprite.rotation = static_cast<std::uint8_t>(rotation); sprite.flipX = (flip & 1U) != 0; sprite.flipY = (flip & 2U) != 0;
    }
    return sprite;
}

std::optional<Map2DAsset> Map2DAssetIO::Load(const std::filesystem::path& path, std::string* error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        SetError(error, "Map asset could not be opened: " + path.string());
        return std::nullopt;
    }
    std::ostringstream text;
    text << input.rdbuf();
    const std::string bytes = text.str();
    if (bytes.size() > kMaximumFileBytes) {
        SetError(error, "Map asset is too large");
        return std::nullopt;
    }
    return Parse(bytes, error);
}

std::optional<Map2DAsset> Map2DAssetIO::Load(std::span<const std::uint8_t> bytes, std::string* error) {
    if (bytes.size() > kMaximumFileBytes) {
        SetError(error, "Map asset is too large");
        return std::nullopt;
    }
    return Parse(std::string_view{ reinterpret_cast<const char*>(bytes.data()), bytes.size() }, error);
}

std::optional<Map2DAsset> Map2DAssetIO::Parse(std::string_view text, std::string* error) {
    Map2DAsset map{};
    map.width = 0U;
    map.height = 0U;
    bool headerSeen = false;
    std::size_t lineNumber = 0U;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = text.substr(0U, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1U);
        ++lineNumber;
        const std::vector<std::string_view> words = Words(line);
        if (words.empty()) continue;
        const auto fail = [&](std::string_view what) {
            SetError(error, "Map line " + std::to_string(lineNumber) + ": " + std::string{ what });
            return std::nullopt;
        };
        if (!headerSeen) {
            std::uint32_t version = 0U;
            if (words.size() != 2U || words[0] != kHeader || !ParseNumber(words[1], version)) {
                return fail("expected \"21kb-map2d 1\"");
            }
            if (version == 0U || version > Map2DAsset::CurrentVersion) return fail("unsupported map version");
            headerSeen = true;
            continue;
        }
        const std::string_view keyword = words[0];
        if (keyword == "size") {
            if (words.size() != 3U || !ParseNumber(words[1], map.width) || !ParseNumber(words[2], map.height)) {
                return fail("size needs <width> <height>");
            }
        } else if (keyword == "tileSize") {
            if (words.size() != 2U || !ParseNumber(words[1], map.tileSize)) return fail("tileSize needs a number");
        } else if (keyword == "floorHeight") {
            if (words.size() != 2U || !ParseNumber(words[1], map.floorHeight)) return fail("floorHeight needs a number");
        } else if (keyword == "orientation") {
            if (words.size() != 2U || (words[1] != "flat" && words[1] != "wall")) return fail("orientation needs flat or wall");
            map.orientation = words[1] == "wall" ? Map2DOrientation::Wall : Map2DOrientation::Flat;
        } else if (keyword == "view") {
            if (words.size() != 3U || !ParseNumber(words[1], map.viewWidth) || !ParseNumber(words[2], map.viewHeight)) {
                return fail("view needs <width> <height> in tiles");
            }
        } else if (keyword == "background") {
            if (words.size() != 2U || !ParseBool(words[1], map.background)) return fail("background needs true or false");
        } else if (keyword == "sheet") {
            // sheet <cellSize> <columns> <rows> "<texture>" "<material>"
            Map2DSheet sheet{};
            if (words.size() < 5U || words.size() > 6U || !ParseNumber(words[1], sheet.cellSize) ||
                !ParseNumber(words[2], sheet.columns) || !ParseNumber(words[3], sheet.rows)) {
                return fail("sheet needs <cellSize> <columns> <rows> \"<texture>\" [\"<material>\"]");
            }
            sheet.texture = std::string{ words[4] };
            if (words.size() == 6U) sheet.material = std::string{ words[5] };
            map.sheets.push_back(std::move(sheet));
        } else if (keyword == "parallax") {
            if (words.size() != 3U || !ParseNumber(words[1], map.parallaxX) || !ParseNumber(words[2], map.parallaxY)) return fail("parallax needs x and y factors");
        } else if (keyword == "sortByY") {
            if (words.size() != 2U || !ParseBool(words[1], map.sortByY)) return fail("sortByY needs true or false");
        } else if (keyword == "lit") {
            if (words.size() != 2U || !ParseBool(words[1], map.lit)) return fail("lit needs true or false");
        } else if (keyword == "layout" || keyword == "definition" || keyword == "frames" || keyword == "shape" || keyword == "defaultData") {
            std::uint32_t sheetIndex = 0U, cell = 0U;
            if (words.size() < 3U || !ParseNumber(words[1], sheetIndex) || sheetIndex >= map.sheets.size()) return fail("unknown sheet");
            auto& sheet = map.sheets[sheetIndex];
            if (keyword == "layout") {
                if (words.size() != 7U || !ParseNumber(words[2], sheet.cellHeight) || !ParseNumber(words[3], sheet.margin) ||
                    !ParseNumber(words[4], sheet.spacing) || !ParseNumber(words[5], sheet.imageWidth) || !ParseNumber(words[6], sheet.imageHeight)) return fail("invalid sheet layout");
            } else {
                if (!ParseNumber(words[2], cell) || cell >= static_cast<std::uint64_t>(sheet.columns) * sheet.rows) return fail("unknown sheet cell");
                auto& definition = sheet.definitions[cell];
                if (keyword == "definition") {
                    if (words.size() != 6U || !ParseNumber(words[3], definition.framesPerSecond) ||
                        !ParseBool(words[4], definition.blocksNavigation) || !ParseBool(words[5], definition.occludesLight)) return fail("invalid tile definition");
                } else if (keyword == "frames") {
                    definition.frames.clear();
                    for (std::size_t i = 3; i < words.size(); ++i) { std::uint32_t frame = 0; if (!ParseNumber(words[i], frame)) return fail("invalid animation frame"); definition.frames.push_back(frame); }
                } else if (keyword == "shape") {
                    definition.collision = {};
                    if (!ReadCollision(words, 3, definition.collision)) return fail("invalid collision shape");
                } else if (!ReadProperty(words, 3, definition.properties)) return fail("invalid default property");
            }
        } else if (keyword == "rule") {
            Map2DGroundRule rule{};
            std::uint32_t count = 0;
            if (words.size() < 5U || !ParseNumber(words[2], rule.sheet) || !ParseNumber(words[3], rule.order) || !ParseNumber(words[4], count) ||
                count > 256U || words.size() != 5U + 2U * count + rule.borders.size()) return fail("invalid ground rule");
            rule.name = words[1];
            for (std::size_t i = 0; i < count; ++i) {
                std::uint32_t cell = 0, weight = 0;
                if (!ParseNumber(words[5 + i * 2], cell) || !ParseNumber(words[6 + i * 2], weight)) return fail("invalid ground variant");
                rule.variants.push_back(cell); rule.weights.push_back(weight);
            }
            for (std::size_t i = 0; i < rule.borders.size(); ++i) if (!ParseNumber(words[5 + 2 * count + i], rule.borders[i])) return fail("invalid border rule");
            map.groundRules.push_back(std::move(rule));
        } else if (keyword == "collision" || keyword == "data" || keyword == "prefab") {
            std::uint32_t x = 0, y = 0; std::int32_t z = 0;
            if (words.size() < 5U || !ParseNumber(words[1], x) || !ParseNumber(words[2], y) || !ParseNumber(words[3], z) ||
                x >= map.width || y >= map.height || z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) return fail("invalid cell coordinates");
            auto& tile = map.tiles[Map2DTileKey(x,y,z)];
            if (keyword == "collision") {
                tile.collision.emplace();
                if (!ReadCollision(words, 4, *tile.collision)) return fail("invalid cell collision");
            } else if (keyword == "data") {
                if (!ReadProperty(words, 4, tile.properties)) return fail("invalid cell property");
            } else {
                if (words.size() != 5U) return fail("invalid prefab reference");
                tile.prefab = words[4];
            }
        } else if (keyword == "t") {
            // t <x> <y> <z> [g=<sprite>] [b=<sprite>,...] [i=<sprite>,...]
            std::uint32_t x = 0U;
            std::uint32_t y = 0U;
            std::int32_t z = 0;
            if (words.size() < 4U || !ParseNumber(words[1], x) || !ParseNumber(words[2], y) || !ParseNumber(words[3], z) ||
                x >= Map2DAsset::MaximumSize || y >= Map2DAsset::MaximumSize || z < Map2DAsset::MinimumFloor || z > Map2DAsset::MaximumFloor) {
                return fail("t needs <x> <y> <z> inside the map");
            }
            Map2DTile tile{};
            for (std::size_t index = 4U; index < words.size(); ++index) {
                const std::string_view word = words[index];
                if (word.size() < 2U || word[1] != '=') return fail("tile layer must be g=, b= or i=");
                const std::string_view value = word.substr(2U);
                if (word[0] == 'g') {
                    const std::optional<Map2DSprite> sprite = ParseMap2DSprite(value);
                    if (!sprite.has_value()) return fail("g= needs <sheet>:<cell>");
                    tile.ground = *sprite;
                } else if (word[0] == 'b') {
                    if (!ParseSpriteList(value, tile.borders)) return fail("b= needs <sheet>:<cell>,...");
                } else if (word[0] == 'i') {
                    if (!ParseSpriteList(value, tile.items)) return fail("i= needs <sheet>:<cell>,...");
                } else if (word[0] == 's') {
                    if (!ParseBool(value, tile.solid)) return fail("s= needs true or false");
                } else {
                    return fail("tile layer must be g=, b=, i= or s=");
                }
            }
            if (!tile.IsEmpty()) map.tiles[Map2DTileKey(x, y, z)] = std::move(tile);
        } else {
            return fail("unknown keyword " + std::string{ keyword });
        }
    }
    if (!headerSeen) {
        SetError(error, "Map asset is empty");
        return std::nullopt;
    }
    if (!IsMap2DAssetValid(map, error)) return std::nullopt;
    return map;
}

std::string Map2DAssetIO::Write(const Map2DAsset& map) {
    std::ostringstream output;
    output << kHeader << ' ' << Map2DAsset::CurrentVersion << '\n';
    output << "size " << map.width << ' ' << map.height << '\n';
    output << "tileSize " << FloatText(map.tileSize) << '\n';
    output << "floorHeight " << FloatText(map.floorHeight) << '\n';
    output << "orientation " << Map2DOrientationName(map.orientation) << '\n';
    output << "view " << map.viewWidth << ' ' << map.viewHeight << '\n';
    output << "background " << (map.background ? "true" : "false") << '\n';
    output << "parallax " << FloatText(map.parallaxX) << ' ' << FloatText(map.parallaxY) << '\n';
    output << "sortByY " << map.sortByY << '\n';
    output << "lit " << map.lit << '\n';
    std::size_t sheetIndex = 0;
    for (const Map2DSheet& sheet : map.sheets) {
        output << "sheet " << sheet.cellSize << ' ' << sheet.columns << ' ' << sheet.rows << ' ';
        WriteQuoted(output, sheet.texture);
        if (!sheet.material.empty()) {
            output << ' ';
            WriteQuoted(output, sheet.material);
        }
        output << '\n';
        output << "layout " << sheetIndex << ' ' << sheet.cellHeight << ' ' << sheet.margin << ' ' << sheet.spacing << ' ' << sheet.imageWidth << ' ' << sheet.imageHeight << '\n';
        for (const auto& [cell, definition] : sheet.definitions) {
            const auto id = std::to_string(sheetIndex) + " " + std::to_string(cell);
            output << "definition " << id << ' ' << FloatText(definition.framesPerSecond) << ' ' << definition.blocksNavigation << ' ' << definition.occludesLight << '\n';
            output << "shape " << id << ' '; WriteCollision(output, definition.collision);
            if (!definition.frames.empty()) {
                output << "frames " << id; for (auto frame : definition.frames) output << ' ' << frame; output << '\n';
            }
            WriteProperties(output, "defaultData " + id, definition.properties);
        }
        ++sheetIndex;
    }
    for (const auto& rule : map.groundRules) {
        output << "rule "; WriteQuoted(output, rule.name);
        output << ' ' << rule.sheet << ' ' << rule.order << ' ' << rule.variants.size();
        for (std::size_t i = 0; i < rule.variants.size(); ++i) output << ' ' << rule.variants[i] << ' ' << (rule.weights.empty() ? 1U : rule.weights[i]);
        for (auto cell : rule.borders) output << ' ' << cell;
        output << '\n';
    }
    for (const auto& [key, tile] : map.tiles) {
        if (tile.IsEmpty()) continue;
        output << "t " << Map2DTileKeyX(key) << ' ' << Map2DTileKeyY(key) << ' ' << Map2DTileKeyZ(key);
        if (tile.ground.IsValid()) output << " g=" << FormatMap2DSprite(tile.ground);
        if (!tile.borders.empty()) {
            output << " b=";
            WriteSpriteList(output, tile.borders);
        }
        if (!tile.items.empty()) {
            output << " i=";
            WriteSpriteList(output, tile.items);
        }
        if (tile.solid) output << " s=true";
        output << '\n';
        const auto position = std::to_string(Map2DTileKeyX(key)) + " " + std::to_string(Map2DTileKeyY(key)) + " " + std::to_string(Map2DTileKeyZ(key));
        if (tile.collision) { output << "collision " << position << ' '; WriteCollision(output, *tile.collision); }
        WriteProperties(output, "data " + position, tile.properties);
        if (!tile.prefab.empty()) { output << "prefab " << position << ' '; WriteQuoted(output, tile.prefab); output << '\n'; }
    }
    return output.str();
}

bool Map2DAssetIO::Save(const std::filesystem::path& path, const Map2DAsset& map, std::string* error) {
    if (!IsMap2DAssetValid(map, error)) return false;
    std::error_code fileError;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), fileError);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            SetError(error, "Map asset could not be written: " + path.string());
            return false;
        }
        const std::string text = Write(map);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output.good()) {
            SetError(error, "Map asset could not be written completely: " + path.string());
            return false;
        }
    }
    std::filesystem::rename(temporary, path, fileError);
    if (fileError) {
        std::filesystem::remove(temporary, fileError);
        SetError(error, "Map asset could not replace " + path.string());
        return false;
    }
    return true;
}

} // namespace kb::assets
