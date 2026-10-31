#include "CliCommands.hpp"

#include "Map2DEditing.hpp"
#include "Map2DTileset.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/Map2DNavigation.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// map2d: World 2D maps for agents - the same painting, automatic borders and ground brushes the
// World 2D Map Editor window offers, on .kbmap2d and .kbtileset files.
namespace kb::cli {
namespace {

constexpr std::string_view kUsage =
    "usage: kb_cli map2d <action> --project <dir> ...\n"
    "  new        --name <Entity> [--scene <path>] [--size 64x64] [--orientation flat|wall] [--view 15x11]\n"
    "  view       --entity <Name> [--scene <path>]      (the Map Camera again, after set)\n"
    "  import-image --source <PNG/BMP>  (copy into the palette without replacing files)\n"
    "  info       --map <path>\n"
    "  schema     --map <path> --name <field> --type bool|int|float|string --default <value> [--rename-from <field>]\n"
    "  path       --map <path> --from x,y --to x,y [--floor n] [--clearance 0..8]\n"
    "             [--diagonal none|safe|free] [--cost-property <name>] [--max-visited 1..1048576]\n"
    "             [--timeout-ms <milliseconds>] (JSON; default 25 ms; exits 0 found, 2 no path, 3 budget)\n"
    "  set        --map <path> [--size WxH] [--orientation flat|wall] [--view WxH]\n"
    "             [--camera-pixels-per-unit n] [--camera-follow <Name>|none] [--camera-smoothing seconds]\n"
    "             [--camera-dead-zone w,h] [--camera-clamp yes|no]   (the play camera: pixel perfect, follow, bounds)\n"
    "  paint      --map <path> --at x,y[,floor] [--to x,y] [--size n]\n"
    "             (--ground <Name> | --sprite <texture> --cell <n> [--cell-size 32] [--as-ground] [--phase seconds]) [--no-borders]\n"
    "  fill       --map <path> --ground <Name> --at x,y[,floor]\n"
    "  erase      --map <path> --at x,y[,floor] [--to x,y] [--items]\n"
    "  block      --map <path> --at x,y[,floor] [--to x,y] (solid walls or platforms)\n"
    "  unblock    --map <path> --at x,y[,floor] [--to x,y] (clear collision only)\n"
    "  borderize  --map <path> [--floor n]\n"
    "  ground     list | new --name <Name> --texture <texture> [--cell-size 32] [--z n] [--tiles a,b] [--weights a,b]\n"
    "             [--island <cell>] [--hole <cell>] [--border n=<cell>,cse=<cell>,...] [--tileset <path>]\n"
    "             | set --name <Name> (same options)\n"
    "  object     new --name <Name> --texture <texture> --cell <top-left sprite> [--size 2x2] [--cell-size 32]\n"
    "  palette    (the categories: grounds, objects and sprite sheets of Assets/2D Map Sprites)\n"
    "  paint      ... --object <Name> places an object with its bottom middle tile on --at\n";

[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

[[nodiscard]] std::filesystem::path AssetsRoot(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: --project <dir> is required\n";
        return {};
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return {};
    }
    return std::filesystem::absolute(projectFile.parent_path() / "Assets").lexically_normal();
}

// /Game/..., Assets/... or a path in the project.
[[nodiscard]] std::filesystem::path ProjectPath(const std::filesystem::path& assetsRoot, std::string_view text) {
    if (text.starts_with("/Game/") || text.starts_with("Assets/") || text.starts_with("Assets\\")) {
        return kb::world2d::Map2DAssetFile(assetsRoot, text);
    }
    const std::filesystem::path path = Utf8Path(text);
    return path.is_absolute() ? path : assetsRoot.parent_path() / path;
}

// A texture as /Game/<path>, whatever form it was given in.
[[nodiscard]] std::string TexturePath(const std::filesystem::path& assetsRoot, std::string_view text) {
    if (text.starts_with("/Game/")) return std::string{ text };
    return kb::world2d::Map2DVirtualPath(assetsRoot, ProjectPath(assetsRoot, text));
}

template <typename T>
[[nodiscard]] std::optional<std::vector<T>> Numbers(std::string_view text) {
    std::vector<T> values;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view part = text.substr(0U, comma);
        T value{};
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (error != std::errc{} || end != part.data() + part.size()) return std::nullopt;
        values.push_back(value);
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1U);
    }
    return values;
}

[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> Pair(std::string_view text) {
    std::string clean{ text };
    std::ranges::replace_if(clean, [](char character) { return character == 'x' || character == 'X'; }, ',');
    const std::optional<std::vector<std::uint32_t>> values = Numbers<std::uint32_t>(clean);
    if (!values.has_value() || values->size() != 2U || (*values)[0] == 0U || (*values)[1] == 0U) return std::nullopt;
    return std::pair{ (*values)[0], (*values)[1] };
}

struct Spot {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int32_t z = 0;
};

[[nodiscard]] std::optional<Spot> ParseSpot(std::string_view text) {
    const std::optional<std::vector<std::int64_t>> values = Numbers<std::int64_t>(text);
    if (!values.has_value() || values->size() < 2U || values->size() > 3U) return std::nullopt;
    if (values->size() == 3U && ((*values)[2] < kb::assets::Map2DAsset::MinimumFloor ||
        (*values)[2] > kb::assets::Map2DAsset::MaximumFloor)) return std::nullopt;
    Spot spot{ (*values)[0], (*values)[1], values->size() == 3U ? static_cast<std::int32_t>((*values)[2]) : 0 };
    if (spot.z < kb::assets::Map2DAsset::MinimumFloor || spot.z > kb::assets::Map2DAsset::MaximumFloor) return std::nullopt;
    return spot;
}

struct MapSession {
    std::filesystem::path assetsRoot;
    std::filesystem::path file;
    kb::assets::Map2DAsset map;
    kb::world2d::Map2DBrushLibrary library;
};

[[nodiscard]] std::optional<MapSession> OpenMap(const ArgumentList& arguments, CommandIo io) {
    MapSession session{};
    session.assetsRoot = AssetsRoot(arguments, io);
    if (session.assetsRoot.empty()) return std::nullopt;
    const std::optional<std::string> map = arguments.Option("--map");
    if (!map.has_value()) {
        io.err << "error: --map <path> is required (e.g. /Game/Maps/World.kbmap2d)\n";
        return std::nullopt;
    }
    session.file = ProjectPath(session.assetsRoot, *map);
    std::string error;
    std::optional<kb::assets::Map2DAsset> loaded = kb::assets::Map2DAssetIO::Load(session.file, &error);
    if (!loaded.has_value()) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    session.map = std::move(*loaded);
    std::vector<std::string> warnings;
    session.library = kb::world2d::Map2DBrushLibrary::Load(session.assetsRoot, &warnings);
    for (const std::string& warning : warnings) io.err << "warning: " << warning << '\n';
    return session;
}

[[nodiscard]] int SaveMap(const MapSession& session, std::string_view what, CommandIo io) {
    std::string error;
    if (!kb::assets::Map2DAssetIO::Save(session.file, session.map, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << what << " (" << kb::world2d::Map2DVirtualPath(session.assetsRoot, session.file) << ", " << session.map.tiles.size() << " tiles)\n";
    return 0;
}

[[nodiscard]] int FindPath(const ArgumentList& arguments, CommandIo io) {
    const auto from = Numbers<int>(arguments.Option("--from").value_or(""));
    const auto to = Numbers<int>(arguments.Option("--to").value_or(""));
    if (!from || !to || from->size() != 2U || to->size() != 2U) {
        io.err << "error: path requires --from x,y and --to x,y\n";
        return 1;
    }
    kb::assets::Map2DPathOptions options;
    options.maximumMilliseconds = 25.0;
    const auto integer = [&](std::string_view name, int low, int high, int fallback) -> std::optional<int> {
        const auto value = arguments.Option(name);
        if (!value) return fallback;
        const auto parsed = Numbers<int>(*value);
        if (parsed && parsed->size() == 1U && parsed->front() >= low && parsed->front() <= high) return parsed->front();
        io.err << "error: " << name << " expects " << low << ".." << high << '\n';
        return std::nullopt;
    };
    const auto floor = integer("--floor", -50, 50, 0);
    const auto clearance = integer("--clearance", 0, 8, 0);
    const auto visited = integer("--max-visited", 1, 1048576, 65536);
    if (!floor || !clearance || !visited) return 1;
    options.floor = *floor;
    options.clearanceCells = static_cast<std::uint32_t>(*clearance);
    options.maximumVisited = static_cast<std::size_t>(*visited);
    options.costProperty = arguments.Option("--cost-property").value_or("");
    const auto diagonal = arguments.Option("--diagonal").value_or("none");
    if (diagonal == "safe") options.diagonal = kb::assets::Map2DDiagonalPolicy::NoCornerCutting;
    else if (diagonal == "free") options.diagonal = kb::assets::Map2DDiagonalPolicy::Allowed;
    else if (diagonal != "none") { io.err << "error: --diagonal expects none, safe or free\n"; return 1; }
    if (const auto timeout = arguments.Option("--timeout-ms")) {
        const auto parsed = Numbers<double>(*timeout);
        if (!parsed || parsed->size() != 1U || !std::isfinite(parsed->front()) || parsed->front() <= 0.0 || parsed->front() > 60000.0) {
            io.err << "error: --timeout-ms expects a finite value in (0,60000]\n";
            return 1;
        }
        options.maximumMilliseconds = parsed->front();
    }
    const auto session = OpenMap(arguments, io);
    if (!session) return 1;
    const auto result = kb::assets::FindMap2DPathResult(session->map,
        {(*from)[0], (*from)[1]}, {(*to)[0], (*to)[1]}, options);
    using Status = kb::assets::Map2DPathStatus;
    const char* status = "invalid-options";
    switch (result.status) {
    case Status::Found: status = "found"; break;
    case Status::NoPath: status = "no-path"; break;
    case Status::StartBlocked: status = "start-blocked"; break;
    case Status::EndBlocked: status = "end-blocked"; break;
    case Status::BudgetExceeded: status = "budget-exceeded"; break;
    case Status::InvalidOptions: break;
    }
    using kb::core::JsonValue;
    auto report = JsonValue::MakeObject();
    report.Set("schema", JsonValue::MakeString("21kb.map-path/v1"));
    report.Set("succeeded", JsonValue::MakeBool(result.status == Status::Found));
    report.Set("status", JsonValue::MakeString(status));
    report.Set("visited", JsonValue::MakeNumber(static_cast<double>(result.visited)));
    report.Set("cost", JsonValue::MakeNumber(static_cast<double>(result.cost)));
    report.Set("elapsed_ms", JsonValue::MakeNumber(result.elapsedMilliseconds));
    report.Set("budget_exceeded", JsonValue::MakeString(result.status != Status::BudgetExceeded ? "none" :
        (result.timeBudgetExceeded ? "time" : "visited")));
    auto path = JsonValue::MakeArray();
    for (const auto& cell : result.path) {
        auto point = JsonValue::MakeArray();
        point.Append(JsonValue::MakeNumber(cell[0]));
        point.Append(JsonValue::MakeNumber(cell[1]));
        path.Append(std::move(point));
    }
    report.Set("path", std::move(path));
    io.out << report.Dump() << '\n';
    if (!io.out.good() || result.status == Status::InvalidOptions) return 1;
    return result.status == Status::Found ? 0 : (result.status == Status::BudgetExceeded ? 3 : 2);
}

[[nodiscard]] std::set<std::int32_t> Floors(const kb::assets::Map2DAsset& map) {
    std::set<std::int32_t> floors;
    for (const auto& [key, tile] : map.tiles) floors.insert(kb::assets::Map2DTileKeyZ(key));
    return floors;
}

[[nodiscard]] int Info(const ArgumentList& arguments, CommandIo io) {
    const std::optional<MapSession> session = OpenMap(arguments, io);
    if (!session.has_value()) return 1;
    const kb::assets::Map2DAsset& map = session->map;
    io.out << "map: " << kb::world2d::Map2DVirtualPath(session->assetsRoot, session->file) << '\n'
           << "size: " << map.width << " x " << map.height << " tiles of " << map.tileSize << " m, floor height " << map.floorHeight << " m\n"
           << "orientation: " << kb::assets::Map2DOrientationName(map.orientation) << '\n'
           << "client box: " << map.viewWidth << " x " << map.viewHeight << " tiles\n";
    if (map.camera != kb::assets::Map2DCamera{}) {
        const kb::assets::Map2DCamera& camera = map.camera;
        io.out << "play camera: " << (camera.pixelsPerUnit > 0.0F ? "pixel perfect at " + std::to_string(camera.pixelsPerUnit) + " px per unit" : std::string{ "plain" })
               << (camera.followTarget.empty() ? std::string{} : ", follows " + camera.followTarget + " (smoothing " + std::to_string(camera.smoothingSeconds) +
                   " s, drag margins " + std::to_string(camera.deadZoneWidth) + " x " + std::to_string(camera.deadZoneHeight) + " tiles)")
               << (camera.clampToMap ? ", kept inside the map" : "") << ", zoom " << camera.zoom << '\n';
    }
    io.out << "limits: size 1..4096, coordinates 0..size-1, floors -50..50, sheets 256, stack 32\n"
           << "storage: sparse cells; runtime chunks 16x16 per floor\n";
    io.out << "tiles: " << map.tiles.size() << " on floors";
    for (const std::int32_t floor : Floors(map)) io.out << ' ' << floor;
    io.out << '\n';
    for (std::size_t index = 0U; index < map.sheets.size(); ++index) {
        const kb::assets::Map2DSheet& sheet = map.sheets[index];
        io.out << "sheet " << index << ": " << sheet.texture << " (" << sheet.columns << " x " << sheet.rows << " sprites of " << sheet.cellSize << " px)\n";
    }
    for (const kb::world2d::Map2DGroundBrush* ground : session->library.Grounds()) {
        io.out << "ground: " << ground->name << " z " << ground->zOrder << (ground->HasBorders() ? ", with borders" : "") << '\n';
    }
    return 0;
}

[[nodiscard]] int Set(const ArgumentList& arguments, CommandIo io) {
    std::optional<MapSession> session = OpenMap(arguments, io);
    if (!session.has_value()) return 1;
    kb::assets::Map2DAsset& map = session->map;
    if (const std::optional<std::string> size = arguments.Option("--size"); size.has_value()) {
        const auto parsed = Pair(*size);
        if (!parsed.has_value()) {
            io.err << "error: --size takes <width>x<height>\n";
            return 1;
        }
        if (parsed->first == 0U || parsed->second == 0U ||
            parsed->first > kb::assets::Map2DAsset::MaximumSize || parsed->second > kb::assets::Map2DAsset::MaximumSize) {
            io.err << "error: --size dimensions must be in 1.." << kb::assets::Map2DAsset::MaximumSize << '\n';
            return 1;
        }
        kb::world2d::ResizeMap2D(map, parsed->first, parsed->second);
    }
    if (const std::optional<std::string> view = arguments.Option("--view"); view.has_value()) {
        const auto parsed = Pair(*view);
        if (!parsed.has_value() || parsed->first > 255U || parsed->second > 255U) {
            io.err << "error: --view takes <width>x<height> (1..255)\n";
            return 1;
        }
        map.viewWidth = parsed->first;
        map.viewHeight = parsed->second;
    }
    if (const std::optional<std::string> orientation = arguments.Option("--orientation"); orientation.has_value()) {
        if (*orientation != "flat" && *orientation != "wall") {
            io.err << "error: --orientation takes flat or wall\n";
            return 1;
        }
        map.orientation = *orientation == "wall" ? kb::assets::Map2DOrientation::Wall : kb::assets::Map2DOrientation::Flat;
    }
    kb::assets::Map2DCamera& camera = map.camera;
    const auto camera_number = [&](std::string_view option, float& target) {
        const std::optional<std::string> text = arguments.Option(option);
        if (!text.has_value()) return true;
        const std::optional<std::vector<float>> values = Numbers<float>(*text);
        if (!values.has_value() || values->size() != 1U) {
            io.err << "error: " << option << " takes one number\n";
            return false;
        }
        target = values->front();
        return true;
    };
    if (!camera_number("--camera-pixels-per-unit", camera.pixelsPerUnit) || !camera_number("--camera-smoothing", camera.smoothingSeconds) ||
        !camera_number("--camera-zoom", camera.zoom)) return 1;
    if (const std::optional<std::string> margins = arguments.Option("--camera-dead-zone"); margins.has_value()) {
        const std::optional<std::vector<float>> values = Numbers<float>(*margins);
        if (!values.has_value() || values->size() != 2U) {
            io.err << "error: --camera-dead-zone takes <width>,<height> in tiles\n";
            return 1;
        }
        camera.deadZoneWidth = (*values)[0];
        camera.deadZoneHeight = (*values)[1];
    }
    if (const std::optional<std::string> follow = arguments.Option("--camera-follow"); follow.has_value()) {
        camera.followTarget = *follow == "none" ? std::string{} : *follow;
    }
    if (const std::optional<std::string> clamp = arguments.Option("--camera-clamp"); clamp.has_value()) {
        if (*clamp != "yes" && *clamp != "no") {
            io.err << "error: --camera-clamp takes yes or no\n";
            return 1;
        }
        camera.clampToMap = *clamp == "yes";
    }
    std::string invalid;
    if (!kb::assets::IsMap2DAssetValid(map, &invalid)) {
        io.err << "error: " << invalid << '\n';
        return 1;
    }
    io.out << "the scene's Map Camera follows with: kb_cli map2d view --entity <Name>\n";
    return SaveMap(*session, "updated the map", io);
}

[[nodiscard]] std::optional<kb::world2d::Map2DRect> Area(const ArgumentList& arguments, const kb::world2d::Map2DEditor& editor, std::int32_t& floor, CommandIo io) {
    const std::optional<std::string> at = arguments.Option("--at");
    const std::optional<Spot> from = at.has_value() ? ParseSpot(*at) : std::nullopt;
    if (!from.has_value()) {
        io.err << "error: --at x,y[,floor] is required (floor -50..50)\n";
        return std::nullopt;
    }
    const auto bounds = editor.Clamp(0, 0, kb::assets::Map2DAsset::MaximumSize, kb::assets::Map2DAsset::MaximumSize);
    const auto inside = [&](const Spot& spot) { return spot.x >= 0 && spot.y >= 0 && spot.x <= bounds.x1 && spot.y <= bounds.y1; };
    if (!inside(*from)) { io.err << "error: --at is outside the map\n"; return std::nullopt; }
    floor = from->z;
    if (const std::optional<std::string> to = arguments.Option("--to"); to.has_value()) {
        const std::optional<Spot> end = ParseSpot(*to);
        if (!end.has_value()) {
            io.err << "error: --to takes x,y\n";
            return std::nullopt;
        }
        if (!inside(*end)) { io.err << "error: --to is outside the map\n"; return std::nullopt; }
        return editor.Clamp(from->x, from->y, end->x, end->y);
    }
    std::int64_t size = 1;
    if (const std::optional<std::string> text = arguments.Option("--size"); text.has_value()) {
        const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), size);
        if (error != std::errc{} || size < 1 || size > 64) {
            io.err << "error: --size takes 1..64 tiles\n";
            return std::nullopt;
        }
    }
    const std::int64_t half = (size - 1) / 2;
    return editor.Clamp(from->x - half, from->y - half, from->x - half + size - 1, from->y - half + size - 1);
}

[[nodiscard]] int Paint(const ArgumentList& arguments, CommandIo io) {
    std::optional<MapSession> session = OpenMap(arguments, io);
    if (!session.has_value()) return 1;
    kb::world2d::Map2DEditor editor{ session->map, session->library, session->assetsRoot };
    editor.SetAutoBorder(!arguments.Flag("--no-borders"));
    std::int32_t floor = 0;
    const std::optional<kb::world2d::Map2DRect> area = Area(arguments, editor, floor, io);
    if (!area.has_value()) return 1;
    std::string error;
    if (const std::optional<std::string> ground = arguments.Option("--ground"); ground.has_value()) {
        const kb::world2d::Map2DGroundBrush* brush = session->library.Find(*ground);
        if (brush == nullptr) {
            io.err << "error: no ground named '" << *ground << "' (kb_cli map2d ground list)\n";
            return 1;
        }
        if (!editor.PaintGround(*brush, *area, floor, &error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        return SaveMap(*session, "painted " + *ground, io);
    }
    if (const std::optional<std::string> objectName = arguments.Option("--object"); objectName.has_value()) {
        const kb::world2d::Map2DObjectBrush* object = session->library.FindObject(*objectName);
        if (object == nullptr) {
            io.err << "error: no object named '" << *objectName << "' (kb_cli map2d palette)\n";
            return 1;
        }
        const std::optional<std::pair<std::uint32_t, std::uint32_t>> image =
            kb::world2d::ReadMap2DImageSize(kb::world2d::Map2DAssetFile(session->assetsRoot, object->texture));
        const std::uint32_t columns = image.has_value() ? image->first / object->cellSize : 0U;
        if (columns == 0U) {
            io.err << "error: the object's sheet is missing or not a PNG/BMP: " << object->texture << '\n';
            return 1;
        }
        // Its bottom middle tile on --at, as the Map Editor places it.
        const auto [left, top] = kb::world2d::Map2DObjectOrigin(object->columns, object->rows, area->x0 + (area->x1 - area->x0) / 2, area->y1);
        for (std::uint32_t row = 0U; row < object->rows; ++row) {
            for (std::uint32_t column = 0U; column < object->columns; ++column) {
                const std::int64_t x = left + column;
                const std::int64_t y = top + row;
                if (x < 0 || y < 0 || x >= session->map.width || y >= session->map.height) continue;
                const std::uint32_t cell = (object->firstCell / columns + row) * columns + object->firstCell % columns + column;
                if (!editor.PaintSprite(object->texture, object->cellSize, cell, false, editor.Clamp(x, y, x, y), floor, &error)) {
                    io.err << "error: " << error << '\n';
                    return 1;
                }
            }
        }
        return SaveMap(*session, "placed " + *objectName, io);
    }
    const std::optional<std::string> texture = arguments.Option("--sprite");
    const std::optional<std::string> cellText = arguments.Option("--cell");
    if (!texture.has_value() || !cellText.has_value()) {
        io.err << "error: paint takes --ground <Name> or --sprite <texture> --cell <n>\n";
        return 1;
    }
    std::uint32_t cell = 0U;
    std::uint32_t cellSize = 32U;
    const auto parsedCell = std::from_chars(cellText->data(), cellText->data() + cellText->size(), cell);
    if (parsedCell.ec != std::errc{}) {
        io.err << "error: --cell takes the sprite's number in its sheet (row by row from 0)\n";
        return 1;
    }
    if (const std::optional<std::string> size = arguments.Option("--cell-size"); size.has_value()) {
        const auto parsed = std::from_chars(size->data(), size->data() + size->size(), cellSize);
        if (parsed.ec != std::errc{} || cellSize == 0U) {
            io.err << "error: --cell-size takes pixels\n";
            return 1;
        }
    }
    float phase = 0;
    if (const auto value = arguments.Option("--phase")) {
        const auto parsed = Numbers<float>(*value);
        if (!parsed || parsed->size() != 1 || !std::isfinite(parsed->front()) || parsed->front() < 0) {
            io.err << "error: --phase requires finite nonnegative seconds\n"; return 1;
        }
        phase = parsed->front();
    }
    if (!editor.PaintSprite(TexturePath(session->assetsRoot, *texture), cellSize, cell, arguments.Flag("--as-ground"), *area, floor, &error, phase)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    return SaveMap(*session, "painted sprite " + std::to_string(cell), io);
}

[[nodiscard]] int Fill(const ArgumentList& arguments, CommandIo io) {
    std::optional<MapSession> session = OpenMap(arguments, io);
    if (!session.has_value()) return 1;
    const std::optional<std::string> ground = arguments.Option("--ground");
    const kb::world2d::Map2DGroundBrush* brush = ground.has_value() ? session->library.Find(*ground) : nullptr;
    const std::optional<std::string> at = arguments.Option("--at");
    const std::optional<Spot> spot = at.has_value() ? ParseSpot(*at) : std::nullopt;
    if (brush == nullptr || !spot.has_value() || spot->x < 0 || spot->y < 0) {
        io.err << "error: fill takes --ground <Name> (an existing ground) and --at x,y[,floor]\n";
        return 1;
    }
    kb::world2d::Map2DEditor editor{ session->map, session->library, session->assetsRoot };
    std::string error;
    const std::size_t filled = editor.FillGround(*brush, static_cast<std::uint32_t>(spot->x), static_cast<std::uint32_t>(spot->y), spot->z, &error);
    if (!error.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    return SaveMap(*session, "filled " + std::to_string(filled) + " tiles with " + *ground, io);
}

[[nodiscard]] int Erase(const ArgumentList& arguments, CommandIo io) {
    std::optional<MapSession> session = OpenMap(arguments, io);
    if (!session.has_value()) return 1;
    kb::world2d::Map2DEditor editor{ session->map, session->library, session->assetsRoot };
    std::int32_t floor = 0;
    const std::optional<kb::world2d::Map2DRect> area = Area(arguments, editor, floor, io);
    if (!area.has_value()) return 1;
    editor.Erase(*area, floor, arguments.Flag("--items"));
    return SaveMap(*session, arguments.Flag("--items") ? "removed the top items" : "erased the tiles", io);
}

[[nodiscard]] int Borderize(const ArgumentList& arguments, CommandIo io) {
    std::optional<MapSession> session = OpenMap(arguments, io);
    if (!session.has_value()) return 1;
    kb::world2d::Map2DEditor editor{ session->map, session->library, session->assetsRoot };
    std::set<std::int32_t> floors = Floors(session->map);
    if (const std::optional<std::string> floor = arguments.Option("--floor"); floor.has_value()) {
        std::int32_t value = 0;
        const auto parsed = std::from_chars(floor->data(), floor->data() + floor->size(), value);
        if (parsed.ec != std::errc{}) {
            io.err << "error: --floor takes -50..50\n";
            return 1;
        }
        floors = { value };
    }
    for (const std::int32_t z : floors) editor.RefreshBorders(editor.Clamp(0, 0, session->map.width - 1, session->map.height - 1), z);
    return SaveMap(*session, "borderized", io);
}

// ground: the brushes of the project's tilesets.
[[nodiscard]] bool ApplyGroundOptions(const ArgumentList& arguments, const std::filesystem::path& assetsRoot,
    kb::world2d::Map2DGroundBrush& ground, CommandIo io) {
    if (const std::optional<std::string> texture = arguments.Option("--texture"); texture.has_value()) {
        ground.texture = TexturePath(assetsRoot, *texture);
        if (ground.texture.empty()) {
            io.err << "error: the texture is not inside the project's Assets\n";
            return false;
        }
    }
    if (const std::optional<std::string> size = arguments.Option("--cell-size"); size.has_value()) {
        const auto parsed = std::from_chars(size->data(), size->data() + size->size(), ground.cellSize);
        if (parsed.ec != std::errc{} || ground.cellSize == 0U) {
            io.err << "error: --cell-size takes pixels\n";
            return false;
        }
    }
    if (const std::optional<std::string> z = arguments.Option("--z"); z.has_value()) {
        const auto parsed = std::from_chars(z->data(), z->data() + z->size(), ground.zOrder);
        if (parsed.ec != std::errc{}) {
            io.err << "error: --z takes a whole number (a higher ground borders onto lower ones)\n";
            return false;
        }
    }
    if (const std::optional<std::string> tiles = arguments.Option("--tiles"); tiles.has_value()) {
        const std::optional<std::vector<std::uint32_t>> values = Numbers<std::uint32_t>(*tiles);
        if (!values.has_value() || values->empty()) {
            io.err << "error: --tiles takes sprite numbers, e.g. 0,1,2\n";
            return false;
        }
        ground.tiles = *values;
        ground.weights.clear();
    }
    if (const std::optional<std::string> weights = arguments.Option("--weights"); weights.has_value()) {
        const std::optional<std::vector<std::uint32_t>> values = Numbers<std::uint32_t>(*weights);
        if (!values.has_value() || values->size() != ground.tiles.size()) {
            io.err << "error: --weights takes one weight per tile\n";
            return false;
        }
        ground.weights = *values;
    }
    const std::optional<std::pair<std::uint32_t, std::uint32_t>> image =
        kb::world2d::ReadMap2DImageSize(kb::world2d::Map2DAssetFile(assetsRoot, ground.texture));
    const std::uint32_t columns = image.has_value() && ground.cellSize != 0U ? image->first / ground.cellSize : 0U;
    for (const auto& [option, island] : { std::pair{ "--island", true }, std::pair{ "--hole", false } }) {
        const std::optional<std::string> text = arguments.Option(option);
        if (!text.has_value()) continue;
        std::uint32_t cell = 0U;
        const auto parsed = std::from_chars(text->data(), text->data() + text->size(), cell);
        if (parsed.ec != std::errc{} || columns == 0U) {
            io.err << "error: " << option << " takes the top-left sprite of the block, and the texture must be a PNG or BMP\n";
            return false;
        }
        if (island) {
            if (!arguments.Option("--tiles").has_value()) {
                ground.tiles.clear();
                ground.weights.clear();
            }
            kb::world2d::AssignMap2DIslandBorders(ground, cell, columns);
        } else {
            kb::world2d::AssignMap2DHoleBorders(ground, cell, columns);
        }
    }
    if (const std::optional<std::string> borders = arguments.Option("--border"); borders.has_value()) {
        std::string_view rest{ *borders };
        while (!rest.empty()) {
            const std::size_t comma = rest.find(',');
            const std::string_view part = rest.substr(0U, comma);
            const std::size_t equals = part.find('=');
            const std::optional<kb::world2d::Map2DBorderPiece> piece =
                equals == std::string_view::npos ? std::nullopt : kb::world2d::ParseMap2DBorderPiece(part.substr(0U, equals));
            std::uint32_t cell = 0U;
            const std::string_view value = equals == std::string_view::npos ? std::string_view{} : part.substr(equals + 1U);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), cell);
            if (!piece.has_value() || parsed.ec != std::errc{}) {
                io.err << "error: --border takes piece=cell pairs: n e s w cnw cne cse csw dnw dne dse dsw\n";
                return false;
            }
            ground.borders[static_cast<std::size_t>(*piece)] = cell;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1U);
        }
    }
    if (ground.texture.empty() || ground.tiles.empty()) {
        io.err << "error: a ground needs --texture and --tiles (or --island)\n";
        return false;
    }
    return true;
}

void PrintGround(const kb::world2d::Map2DGroundBrush& ground, CommandIo io) {
    io.out << ground.name << ": z " << ground.zOrder << ", " << ground.texture << " in " << ground.cellSize << " px sprites, tiles";
    for (const std::uint32_t tile : ground.tiles) io.out << ' ' << tile;
    if (ground.HasBorders()) {
        io.out << ", borders";
        for (std::size_t index = 0U; index < kb::world2d::kMap2DBorderPieceCount; ++index) {
            if (ground.borders[index] != kb::world2d::kMap2DNoCell) {
                io.out << ' ' << kb::world2d::Map2DBorderPieceName(static_cast<kb::world2d::Map2DBorderPiece>(index)) << '=' << ground.borders[index];
            }
        }
    }
    io.out << '\n';
}

[[nodiscard]] int Ground(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    const std::filesystem::path assetsRoot = AssetsRoot(arguments, io);
    if (assetsRoot.empty()) return 1;
    std::vector<std::string> warnings;
    kb::world2d::Map2DBrushLibrary library = kb::world2d::Map2DBrushLibrary::Load(assetsRoot, &warnings);
    for (const std::string& warning : warnings) io.err << "warning: " << warning << '\n';
    if (action == "list") {
        for (const kb::world2d::Map2DTilesetFile& file : library.files) {
            io.out << file.virtualPath << '\n';
            for (const kb::world2d::Map2DGroundBrush& ground : file.tileset.grounds) {
                io.out << "  ";
                PrintGround(ground, io);
            }
        }
        if (library.files.empty()) io.out << "no tilesets yet: kb_cli map2d ground new --name Grass --texture <png> --island <cell>\n";
        return 0;
    }
    const std::optional<std::string> name = arguments.Option("--name");
    if ((action != "new" && action != "set") || !name.has_value() || name->empty()) {
        io.err << kUsage;
        return 1;
    }
    std::string error;
    for (kb::world2d::Map2DTilesetFile& file : library.files) {
        kb::world2d::Map2DGroundBrush* ground = file.tileset.Find(*name);
        if (ground == nullptr) continue;
        if (action == "new") {
            io.err << "error: a ground named '" << *name << "' already exists in " << file.virtualPath << " (use ground set)\n";
            return 1;
        }
        if (!ApplyGroundOptions(arguments, assetsRoot, *ground, io)) return 1;
        if (!kb::world2d::Map2DTilesetIO::Save(file.path, file.tileset, &error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        PrintGround(*ground, io);
        io.out << "maps painted before keep their borders until: kb_cli map2d borderize --map <path>\n";
        return 0;
    }
    if (action == "set") {
        io.err << "error: no ground named '" << *name << "'\n";
        return 1;
    }
    kb::world2d::Map2DGroundBrush ground{ .name = *name };
    std::int32_t highest = 0;
    for (const kb::world2d::Map2DGroundBrush* other : library.Grounds()) highest = std::max(highest, other->zOrder);
    ground.zOrder = highest + 10;
    if (!ApplyGroundOptions(arguments, assetsRoot, ground, io)) return 1;
    // By default the brush goes to the tileset of its sheet's category (its folder in Assets/2D Map Sprites).
    const std::filesystem::path tilesetFile = arguments.Option("--tileset").has_value()
        ? ProjectPath(assetsRoot, *arguments.Option("--tileset"))
        : kb::world2d::Map2DCategoryTileset(assetsRoot, kb::world2d::Map2DCategoryOf(assetsRoot, kb::world2d::Map2DAssetFile(assetsRoot, ground.texture)));
    kb::world2d::Map2DTileset tileset{};
    std::error_code fileError;
    if (std::filesystem::exists(tilesetFile, fileError)) {
        std::optional<kb::world2d::Map2DTileset> loaded = kb::world2d::Map2DTilesetIO::Load(tilesetFile, &error);
        if (!loaded.has_value()) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        tileset = std::move(*loaded);
    }
    tileset.grounds.push_back(ground);
    if (!kb::world2d::Map2DTilesetIO::Save(tilesetFile, tileset, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    PrintGround(ground, io);
    return 0;
}

// object new: a block of a sheet placed in one click (a tree, a house), in its sheet's category.
[[nodiscard]] int Object(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    const std::filesystem::path assetsRoot = AssetsRoot(arguments, io);
    if (assetsRoot.empty()) return 1;
    const std::optional<std::string> name = arguments.Option("--name");
    const std::optional<std::string> texture = arguments.Option("--texture");
    const std::optional<std::string> cell = arguments.Option("--cell");
    if (action != "new" || !name.has_value() || name->empty() || !texture.has_value() || !cell.has_value()) {
        io.err << kUsage;
        return 1;
    }
    kb::world2d::Map2DObjectBrush object{ .name = *name, .texture = TexturePath(assetsRoot, *texture) };
    const auto parsedCell = std::from_chars(cell->data(), cell->data() + cell->size(), object.firstCell);
    if (parsedCell.ec != std::errc{} || object.texture.empty()) {
        io.err << "error: --cell takes the top-left sprite of the object and --texture a sheet in the project's Assets\n";
        return 1;
    }
    if (const std::optional<std::string> size = arguments.Option("--size"); size.has_value()) {
        const auto parsed = Pair(*size);
        if (!parsed.has_value() || parsed->first > 32U || parsed->second > 32U) {
            io.err << "error: --size takes <columns>x<rows> of sprites (up to 32)\n";
            return 1;
        }
        object.columns = parsed->first;
        object.rows = parsed->second;
    }
    if (const std::optional<std::string> size = arguments.Option("--cell-size"); size.has_value()) {
        const auto parsed = std::from_chars(size->data(), size->data() + size->size(), object.cellSize);
        if (parsed.ec != std::errc{} || object.cellSize == 0U) {
            io.err << "error: --cell-size takes pixels\n";
            return 1;
        }
    }
    const kb::world2d::Map2DBrushLibrary library = kb::world2d::Map2DBrushLibrary::Load(assetsRoot);
    if (library.FindObject(*name) != nullptr || library.Find(*name) != nullptr) {
        io.err << "error: the name '" << *name << "' is already taken\n";
        return 1;
    }
    const std::filesystem::path tilesetFile = kb::world2d::Map2DCategoryTileset(assetsRoot,
        kb::world2d::Map2DCategoryOf(assetsRoot, kb::world2d::Map2DAssetFile(assetsRoot, object.texture)));
    kb::world2d::Map2DTileset tileset{};
    std::error_code fileError;
    std::string error;
    if (std::filesystem::exists(tilesetFile, fileError)) {
        std::optional<kb::world2d::Map2DTileset> loaded = kb::world2d::Map2DTilesetIO::Load(tilesetFile, &error);
        if (!loaded.has_value()) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        tileset = std::move(*loaded);
    }
    tileset.objects.push_back(object);
    if (!kb::world2d::Map2DTilesetIO::Save(tilesetFile, tileset, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << object.name << ": " << object.columns << " x " << object.rows << " sprites of " << object.texture << " from sprite " << object.firstCell
           << " (" << kb::world2d::Map2DVirtualPath(assetsRoot, tilesetFile) << ")\n";
    return 0;
}

// palette: what the Map Editor's palette offers, by category: the grounds and objects of the
// tilesets and the sprite sheets of Assets/2D Map Sprites.
[[nodiscard]] int Palette(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path assetsRoot = AssetsRoot(arguments, io);
    if (assetsRoot.empty()) return 1;
    const kb::world2d::Map2DBrushLibrary library = kb::world2d::Map2DBrushLibrary::Load(assetsRoot);
    std::map<std::string, std::vector<std::string>> categories;
    for (const kb::world2d::Map2DTilesetFile& file : library.files) {
        for (const kb::world2d::Map2DGroundBrush& ground : file.tileset.grounds) {
            categories[file.category].push_back("ground  " + ground.name + " (layer " + std::to_string(ground.zOrder) +
                (ground.HasBorders() ? ", with edges)" : ", no edges yet)"));
        }
        for (const kb::world2d::Map2DObjectBrush& object : file.tileset.objects) {
            categories[file.category].push_back("object  " + object.name + " (" + std::to_string(object.columns) + " x " + std::to_string(object.rows) + ")");
        }
    }
    const std::filesystem::path sprites = assetsRoot / kb::assets::Utf8Path(kb::world2d::kMap2DSpritesFolder);
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it{ sprites, error }, end; !error && it != end; it.increment(error)) {
        std::string extension = it->path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (extension != ".png" && extension != ".bmp") continue;
        const std::string texture = kb::world2d::Map2DVirtualPath(assetsRoot, it->path());
        const kb::world2d::Map2DSheetSetting setting = library.SheetSetting(texture);
        const std::optional<std::pair<std::uint32_t, std::uint32_t>> size = kb::world2d::ReadMap2DImageSize(it->path());
        std::string line = "sheet   " + texture;
        if (size.has_value() && setting.cellSize != 0U) {
            line += " (" + std::to_string(size->first / setting.cellSize) + " x " + std::to_string(size->second / setting.cellSize) +
                " sprites of " + std::to_string(setting.cellSize) + " px)";
        }
        categories[kb::world2d::Map2DCategoryOf(assetsRoot, it->path())].push_back(line);
    }
    if (categories.empty()) {
        io.out << "the palette is empty: put PNG sprite sheets into Assets/" << kb::world2d::kMap2DSpritesFolder << "/<Category>/\n";
        return 0;
    }
    for (const auto& [category, lines] : categories) {
        io.out << category << '\n';
        for (const std::string& line : lines) io.out << "  " << line << '\n';
    }
    return 0;
}

} // namespace

int RunMap2DCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "new" || action == "view") return RunMap2DSceneCommand(action, arguments, io);
    if (action == "import-image") {
        const auto assetsRoot = AssetsRoot(arguments, io);
        if (assetsRoot.empty()) return 1;
        const auto source = arguments.Option("--source");
        if (!source) { io.err << "error: import-image requires --source <PNG/BMP>\n"; return 1; }
        std::string error;
        const auto imported = kb::world2d::ImportMap2DImage(assetsRoot, Utf8Path(*source), &error);
        if (!imported) { io.err << "error: " << error << '\n'; return 1; }
        io.out << *imported << '\n';
        return 0;
    }
    if (action == "info") return Info(arguments, io);
    if (action == "schema") {
        auto session=OpenMap(arguments,io);
        if (!session) return 1;
        const auto name=arguments.Option("--name"), type=arguments.Option("--type"), value=arguments.Option("--default");
        if (!name || !type || !value) { io.err << "error: schema requires --name, --type bool|int|float|string and --default; optional --rename-from\n"; return 1; }
        const auto parsed=kb::assets::ParseMap2DPropertyValue(*type,*value);
        std::string error;
        if (!parsed || !kb::assets::SetMap2DPropertySchema(session->map,*name,*parsed,arguments.Option("--rename-from").value_or(""),&error)) {
            io.err << "error: " << (parsed ? error : "Invalid default value") << '\n'; return 1;
        }
        return SaveMap(*session,"updated property schema",io);
    }
    if (action == "set") return Set(arguments, io);
    if (action == "path") return FindPath(arguments, io);
    if (action == "paint") return Paint(arguments, io);
    if (action == "fill") return Fill(arguments, io);
    if (action == "erase") return Erase(arguments, io);
    if (action == "block" || action == "unblock") {
        auto session = OpenMap(arguments, io);
        if (!session) return 1;
        kb::world2d::Map2DEditor editor{ session->map, session->library, session->assetsRoot };
        std::int32_t floor = 0;
        const auto area = Area(arguments, editor, floor, io);
        if (!area) return 1;
        editor.PaintCollision(*area, floor, action == "block");
        io.out << "Update the scene colliders with: kb_cli map2d view --entity <Name>\n";
        return SaveMap(*session, action == "block" ? "blocked movement" : "cleared movement blocks", io);
    }
    if (action == "borderize") return Borderize(arguments, io);
    if (action == "ground") return Ground(arguments, io);
    if (action == "object") return Object(arguments, io);
    if (action == "palette") return Palette(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
