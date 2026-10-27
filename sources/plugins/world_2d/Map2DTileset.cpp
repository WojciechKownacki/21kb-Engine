#include "Map2DTileset.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <numeric>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

namespace kb::world2d {
namespace {

constexpr std::string_view kHeader = "21kb-tileset";
constexpr std::array<std::string_view, kMap2DBorderPieceCount> kPieceNames{
    "n", "e", "s", "w", "cnw", "cne", "cse", "csw", "dnw", "dne", "dse", "dsw",
};

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

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

[[nodiscard]] std::uint32_t Hash(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    std::uint32_t value = x * 73856093U ^ y * 19349663U ^ z * 83492791U;
    value ^= value >> 13U;
    value *= 0x5bd1e995U;
    value ^= value >> 15U;
    return value;
}

void AssignBlock(
    Map2DGroundBrush& brush,
    std::uint32_t firstCell,
    std::uint32_t columns,
    std::uint32_t size,
    std::span<const std::optional<Map2DBorderPiece>> layout) {
    if (columns == 0U) return;
    const std::uint32_t left = firstCell % columns;
    const std::uint32_t top = firstCell / columns;
    for (std::uint32_t row = 0U; row < size; ++row) {
        for (std::uint32_t column = 0U; column < size; ++column) {
            const std::uint32_t cell = (top + row) * columns + left + column;
            const std::optional<Map2DBorderPiece> piece = layout[row * size + column];
            if (piece.has_value()) {
                brush.borders[static_cast<std::size_t>(*piece)] = cell;
            } else if (std::ranges::find(brush.tiles, cell) == brush.tiles.end()) {
                // The middle of an island is the ground itself.
                brush.tiles.insert(brush.tiles.begin(), cell);
                if (!brush.weights.empty()) brush.weights.insert(brush.weights.begin(), 1U);
            }
        }
    }
}

} // namespace

std::string_view Map2DBorderPieceName(Map2DBorderPiece piece) noexcept {
    const auto index = static_cast<std::size_t>(piece);
    return index < kPieceNames.size() ? kPieceNames[index] : std::string_view{};
}

std::optional<Map2DBorderPiece> ParseMap2DBorderPiece(std::string_view name) noexcept {
    for (std::size_t index = 0U; index < kPieceNames.size(); ++index) {
        if (kPieceNames[index] == name) return static_cast<Map2DBorderPiece>(index);
    }
    return std::nullopt;
}

bool Map2DGroundBrush::HasBorders() const noexcept {
    return std::ranges::any_of(borders, [](std::uint32_t cell) { return cell != kMap2DNoCell; });
}

std::uint32_t Map2DGroundBrush::VariantFor(std::uint32_t x, std::uint32_t y, std::int32_t z) const noexcept {
    if (tiles.empty()) return kMap2DNoCell;
    if (weights.size() != tiles.size()) return tiles[Hash(x, y, static_cast<std::uint32_t>(z)) % tiles.size()];
    const std::uint64_t total = std::accumulate(weights.begin(), weights.end(), std::uint64_t{ 0U });
    if (total == 0U) return tiles.front();
    std::uint64_t pick = Hash(x, y, static_cast<std::uint32_t>(z)) % total;
    for (std::size_t index = 0U; index < tiles.size(); ++index) {
        if (pick < weights[index]) return tiles[index];
        pick -= weights[index];
    }
    return tiles.back();
}

void AssignMap2DIslandBorders(Map2DGroundBrush& brush, std::uint32_t firstCell, std::uint32_t columns) {
    using P = Map2DBorderPiece;
    // Each piece of an island is drawn on the tile outside it: the top-left one lies north-west
    // of the island, so the ground is south-east of it.
    const std::array<std::optional<P>, 9U> layout{
        P::CornerSE, P::S, P::CornerSW,
        P::E, std::nullopt, P::W,
        P::CornerNE, P::N, P::CornerNW,
    };
    AssignBlock(brush, firstCell, columns, 3U, layout);
}

void AssignMap2DHoleBorders(Map2DGroundBrush& brush, std::uint32_t firstCell, std::uint32_t columns) {
    using P = Map2DBorderPiece;
    // A 2x2 hole in the ground: its top-left piece has the ground north and west of it.
    const std::array<std::optional<P>, 4U> layout{
        P::DiagonalNW, P::DiagonalNE,
        P::DiagonalSW, P::DiagonalSE,
    };
    AssignBlock(brush, firstCell, columns, 2U, layout);
}

Map2DObjectBrush* Map2DTileset::FindObject(std::string_view name) noexcept {
    const auto found = std::ranges::find_if(objects, [name](const Map2DObjectBrush& object) { return object.name == name; });
    return found == objects.end() ? nullptr : &*found;
}

const Map2DObjectBrush* Map2DTileset::FindObject(std::string_view name) const noexcept {
    const auto found = std::ranges::find_if(objects, [name](const Map2DObjectBrush& object) { return object.name == name; });
    return found == objects.end() ? nullptr : &*found;
}

Map2DSheetSetting* Map2DTileset::FindSheet(std::string_view texture) noexcept {
    const auto found = std::ranges::find_if(sheets, [texture](const Map2DSheetSetting& sheet) { return sheet.texture == texture; });
    return found == sheets.end() ? nullptr : &*found;
}

const Map2DSheetSetting* Map2DTileset::FindSheet(std::string_view texture) const noexcept {
    const auto found = std::ranges::find_if(sheets, [texture](const Map2DSheetSetting& sheet) { return sheet.texture == texture; });
    return found == sheets.end() ? nullptr : &*found;
}

std::pair<std::int64_t, std::int64_t> Map2DObjectOrigin(std::uint32_t columns, std::uint32_t rows, std::int64_t x, std::int64_t y) noexcept {
    return { x - static_cast<std::int64_t>((columns - 1U) / 2U), y - static_cast<std::int64_t>(rows) + 1 };
}

std::vector<Map2DSheetPiece> FindMap2DSheetPieces(
    std::span<const std::uint32_t> pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t cellSize,
    bool separate,
    std::span<const std::uint32_t> skip, std::uint32_t cellHeight, std::uint32_t margin, std::uint32_t spacing) {
    std::vector<Map2DSheetPiece> pieces;
    if (cellSize == 0U || width < cellSize || height < cellSize || pixels.size() < static_cast<std::size_t>(width) * height) return pieces;
    const Map2DSheetSetting layout{ .cellSize = cellSize, .cellHeight = cellHeight, .margin = margin, .spacing = spacing };
    if (!layout.Fits(width, height)) return pieces;
    cellHeight = layout.Height();
    const std::uint32_t columns = layout.Count(width, cellSize);
    const std::uint32_t rows = layout.Count(height, cellHeight);
    constexpr std::uint32_t kVisible = 16U;
    constexpr std::uint32_t kSolid = 250U;
    const auto alpha = [&](std::uint32_t x, std::uint32_t y) { return pixels[static_cast<std::size_t>(y) * width + x] >> 24U; };
    enum : std::uint8_t { kEmpty, kPartial, kFull };
    std::vector<std::uint8_t> kind(static_cast<std::size_t>(columns) * rows, kEmpty);
    for (std::uint32_t row = 0U; row < rows; ++row) {
        for (std::uint32_t column = 0U; column < columns; ++column) {
            std::uint32_t visible = 0U;
            std::uint32_t solid = 0U;
            for (std::uint32_t y = 0U; y < cellHeight; ++y) {
                for (std::uint32_t x = 0U; x < cellSize; ++x) {
                    const std::uint32_t a = alpha(margin + column * (cellSize + spacing) + x, margin + row * (cellHeight + spacing) + y);
                    visible += a >= kVisible ? 1U : 0U;
                    solid += a >= kSolid ? 1U : 0U;
                }
            }
            const std::uint32_t cell = row * columns + column;
            if (visible == 0U || std::ranges::find(skip, cell) != skip.end()) continue;
            kind[cell] = solid == cellSize * cellHeight ? kFull : kPartial;
        }
    }
    // Partial sprites whose pixels meet across a shared edge belong to one drawing.
    const auto touchesRight = [&](std::uint32_t column, std::uint32_t row) {
        const std::uint32_t edge = margin + column * (cellSize + spacing) + cellSize;
        for (std::uint32_t y = 0U; y < cellHeight; ++y) {
            if (alpha(edge - 1U, margin + row * (cellHeight + spacing) + y) >= kVisible && alpha(edge + spacing, margin + row * (cellHeight + spacing) + y) >= kVisible) return true;
        }
        return false;
    };
    const auto touchesDown = [&](std::uint32_t column, std::uint32_t row) {
        const std::uint32_t edge = margin + row * (cellHeight + spacing) + cellHeight;
        for (std::uint32_t x = 0U; x < cellSize; ++x) {
            if (alpha(margin + column * (cellSize + spacing) + x, edge - 1U) >= kVisible && alpha(margin + column * (cellSize + spacing) + x, edge + spacing) >= kVisible) return true;
        }
        return false;
    };
    std::vector<std::uint8_t> taken(kind.size(), 0U);
    for (std::uint32_t start = 0U; start < kind.size(); ++start) {
        if (kind[start] == kEmpty || taken[start] != 0U) continue;
        std::vector<std::uint32_t> group{ start };
        taken[start] = 1U;
        if (!separate && kind[start] == kPartial) {
            for (std::size_t index = 0U; index < group.size(); ++index) {
                const std::uint32_t column = group[index] % columns;
                const std::uint32_t row = group[index] / columns;
                const auto join = [&](std::uint32_t other, bool touching) {
                    if (touching && kind[other] == kPartial && taken[other] == 0U) {
                        taken[other] = 1U;
                        group.push_back(other);
                    }
                };
                if (column + 1U < columns) join(group[index] + 1U, touchesRight(column, row));
                if (row + 1U < rows) join(group[index] + columns, touchesDown(column, row));
                if (column > 0U) join(group[index] - 1U, touchesRight(column - 1U, row));
                if (row > 0U) join(group[index] - columns, touchesDown(column, row - 1U));
            }
        }
        std::uint32_t left = columns;
        std::uint32_t top = rows;
        std::uint32_t right = 0U;
        std::uint32_t bottom = 0U;
        for (const std::uint32_t cell : group) {
            left = std::min(left, cell % columns);
            right = std::max(right, cell % columns);
            top = std::min(top, cell / columns);
            bottom = std::max(bottom, cell / columns);
        }
        Map2DSheetPiece piece{ .firstCell = top * columns + left, .columns = right - left + 1U, .rows = bottom - top + 1U,
            .opaque = group.size() == 1U && kind[start] == kFull };
        for (const std::uint32_t cell : group) piece.cells.emplace_back(cell % columns - left, cell / columns - top);
        std::ranges::sort(piece.cells, [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
        pieces.push_back(std::move(piece));
    }
    std::ranges::sort(pieces, {}, &Map2DSheetPiece::firstCell);
    return pieces;
}

Map2DGroundBrush* Map2DTileset::Find(std::string_view name) noexcept {
    const auto found = std::ranges::find_if(grounds, [name](const Map2DGroundBrush& brush) { return brush.name == name; });
    return found == grounds.end() ? nullptr : &*found;
}

const Map2DGroundBrush* Map2DTileset::Find(std::string_view name) const noexcept {
    const auto found = std::ranges::find_if(grounds, [name](const Map2DGroundBrush& brush) { return brush.name == name; });
    return found == grounds.end() ? nullptr : &*found;
}

std::optional<Map2DTileset> Map2DTilesetIO::Load(const std::filesystem::path& path, std::string* error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        SetError(error, "Tileset could not be opened: " + path.string());
        return std::nullopt;
    }
    std::ostringstream text;
    text << input.rdbuf();
    return Parse(text.str(), error);
}

std::optional<Map2DTileset> Map2DTilesetIO::Parse(std::string_view text, std::string* error) {
    Map2DTileset tileset{};
    bool headerSeen = false;
    bool lastObject = false;
    std::size_t lineNumber = 0U;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = text.substr(0U, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1U);
        ++lineNumber;
        const std::vector<std::string_view> words = Words(line);
        if (words.empty()) continue;
        const auto fail = [&](std::string_view what) {
            SetError(error, "Tileset line " + std::to_string(lineNumber) + ": " + std::string{ what });
            return std::nullopt;
        };
        if (!headerSeen) {
            std::uint32_t version = 0U;
            if (words.size() != 2U || words[0] != kHeader || !ParseNumber(words[1], version) ||
                version == 0U || version > Map2DTileset::CurrentVersion) {
                return fail("expected \"21kb-tileset 1\"");
            }
            headerSeen = true;
            continue;
        }
        if (words[0] == "ground") {
            Map2DGroundBrush brush{};
            if (words.size() != 5U || words[1].empty() || !ParseNumber(words[2], brush.zOrder) ||
                !ParseNumber(words[3], brush.cellSize) || brush.cellSize == 0U || words[4].empty()) {
                return fail("ground needs \"<name>\" <zOrder> <cellSize> \"<texture>\"");
            }
            brush.name = std::string{ words[1] };
            brush.texture = std::string{ words[4] };
            if (tileset.Find(brush.name) != nullptr) return fail("ground " + brush.name + " is defined twice");
            tileset.grounds.push_back(std::move(brush));
            lastObject = false;
            continue;
        }
        if (words[0] == "object") {
            Map2DObjectBrush object{};
            if (words.size() != 4U || words[1].empty() || !ParseNumber(words[2], object.cellSize) || object.cellSize == 0U || words[3].empty()) {
                return fail("object needs \"<name>\" <cellSize> \"<texture>\"");
            }
            object.name = std::string{ words[1] };
            object.texture = std::string{ words[3] };
            if (tileset.FindObject(object.name) != nullptr) return fail("object " + object.name + " is defined twice");
            tileset.objects.push_back(std::move(object));
            lastObject = true;
            continue;
        }
        if (words[0] == "sheet") {
            Map2DSheetSetting sheet{};
            if (words.size() < 3U || words.size() > 4U || words[1].empty() || !ParseNumber(words[2], sheet.cellSize) || sheet.cellSize == 0U ||
                (words.size() == 4U && words[3] != "separate")) {
                return fail("sheet needs \"<texture>\" <cellSize> [separate]");
            }
            sheet.texture = std::string{ words[1] };
            sheet.separate = words.size() == 4U;
            if (tileset.FindSheet(sheet.texture) != nullptr) return fail("sheet " + sheet.texture + " is set twice");
            tileset.sheets.push_back(std::move(sheet));
            continue;
        }
        if (words[0] == "layout") {
            if (words.size() != 5U) return fail("layout needs texture height margin spacing");
            auto* sheet = tileset.FindSheet(words[1]);
            if (sheet == nullptr || !ParseNumber(words[2], sheet->cellHeight) || !ParseNumber(words[3], sheet->margin) || !ParseNumber(words[4], sheet->spacing) ||
                sheet->Height() > 16384U || sheet->margin > 16384U || sheet->spacing > 16384U) return fail("invalid sheet layout");
            continue;
        }
        if (words[0] == "cells") {
            if (!lastObject || tileset.objects.empty()) return fail("cells comes before any object");
            Map2DObjectBrush& object = tileset.objects.back();
            if (words.size() != 4U || !ParseNumber(words[1], object.firstCell) || !ParseNumber(words[2], object.columns) ||
                !ParseNumber(words[3], object.rows) || object.columns == 0U || object.rows == 0U || object.columns > 32U || object.rows > 32U) {
                return fail("cells needs <firstCell> <columns> <rows> (1..32 each way)");
            }
            continue;
        }
        if (tileset.grounds.empty() || lastObject) return fail(std::string{ words[0] } + " comes before any ground");
        Map2DGroundBrush& brush = tileset.grounds.back();
        if (words[0] == "tiles" || words[0] == "weights") {
            std::vector<std::uint32_t>& values = words[0] == "tiles" ? brush.tiles : brush.weights;
            values.clear();
            for (std::size_t index = 1U; index < words.size(); ++index) {
                std::uint32_t value = 0U;
                if (!ParseNumber(words[index], value)) return fail(std::string{ words[0] } + " takes whole numbers");
                values.push_back(value);
            }
        } else if (words[0] == "border") {
            for (std::size_t index = 1U; index < words.size(); ++index) {
                const std::size_t equals = words[index].find('=');
                const std::optional<Map2DBorderPiece> piece = equals == std::string_view::npos
                    ? std::nullopt : ParseMap2DBorderPiece(words[index].substr(0U, equals));
                std::uint32_t cell = 0U;
                if (!piece.has_value() || !ParseNumber(words[index].substr(equals + 1U), cell)) {
                    return fail("border takes <piece>=<cell> with n e s w cnw cne cse csw dnw dne dse dsw");
                }
                brush.borders[static_cast<std::size_t>(*piece)] = cell;
            }
        } else {
            return fail("unknown keyword " + std::string{ words[0] });
        }
    }
    if (!headerSeen) {
        SetError(error, "Tileset is empty");
        return std::nullopt;
    }
    for (const Map2DGroundBrush& brush : tileset.grounds) {
        if (!brush.weights.empty() && brush.weights.size() != brush.tiles.size()) {
            SetError(error, "Ground " + brush.name + " needs one weight per tile");
            return std::nullopt;
        }
    }
    return tileset;
}

std::string Map2DTilesetIO::Write(const Map2DTileset& tileset) {
    std::ostringstream output;
    output << kHeader << ' ' << Map2DTileset::CurrentVersion << '\n';
    for (const Map2DGroundBrush& brush : tileset.grounds) {
        output << "\nground \"" << brush.name << "\" " << brush.zOrder << ' ' << brush.cellSize << " \"" << brush.texture << "\"\n";
        if (!brush.tiles.empty()) {
            output << "tiles";
            for (const std::uint32_t cell : brush.tiles) output << ' ' << cell;
            output << '\n';
        }
        if (!brush.weights.empty()) {
            output << "weights";
            for (const std::uint32_t weight : brush.weights) output << ' ' << weight;
            output << '\n';
        }
        if (brush.HasBorders()) {
            output << "border";
            for (std::size_t index = 0U; index < kMap2DBorderPieceCount; ++index) {
                if (brush.borders[index] != kMap2DNoCell) output << ' ' << kPieceNames[index] << '=' << brush.borders[index];
            }
            output << '\n';
        }
    }
    for (const Map2DObjectBrush& object : tileset.objects) {
        output << "\nobject \"" << object.name << "\" " << object.cellSize << " \"" << object.texture << "\"\n";
        output << "cells " << object.firstCell << ' ' << object.columns << ' ' << object.rows << '\n';
    }
    if (!tileset.sheets.empty()) output << '\n';
    for (const Map2DSheetSetting& sheet : tileset.sheets) {
        output << "sheet \"" << sheet.texture << "\" " << sheet.cellSize << (sheet.separate ? " separate" : "") << '\n';
        if (sheet.cellHeight != 0U || sheet.margin != 0U || sheet.spacing != 0U) output << "layout \"" << sheet.texture << "\" " << sheet.cellHeight << ' ' << sheet.margin << ' ' << sheet.spacing << '\n';
    }
    return output.str();
}

bool Map2DTilesetIO::Save(const std::filesystem::path& path, const Map2DTileset& tileset, std::string* error) {
    const std::string text = Write(tileset);
    if (!Parse(text, error).has_value()) return false;
    std::error_code fileError;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), fileError);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        SetError(error, "Tileset could not be written: " + path.string());
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output.good()) {
        SetError(error, "Tileset could not be written completely: " + path.string());
        return false;
    }
    output.close();
    if (output.fail()) {
        SetError(error, "Tileset could not be closed: " + path.string());
        return false;
    }
    std::filesystem::rename(temporary, path, fileError);
    if (fileError) {
        std::filesystem::remove(temporary, fileError);
        SetError(error, "Tileset could not replace " + path.string());
        return false;
    }
    return true;
}

} // namespace kb::world2d
