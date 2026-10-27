#include "engine/script/ScriptMap2DApi.hpp"
#include "engine/assets/Map2DNavigation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTilemapRuntime.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include <algorithm>
#include <cmath>

namespace kb::script {
namespace {
using namespace kb::assets;
using Args = std::span<const ScriptFunctionArgument>;
const ScriptValue& Arg(Args args, std::string_view key) {
    static const ScriptValue empty;
    for (const auto& arg : args) if (arg.name == key) return arg.value;
    return empty;
}
ScriptFunctionCallResult Error(std::string message) { return {.executed = false, .errors = {std::move(message)}}; }
struct Cell {
    kb::scene::SceneTilemapRuntime* runtime = nullptr;
    kb::scene::SceneEntity entity{};
    const Map2DAsset* map = nullptr;
    std::uint32_t x = 0, y = 0;
    std::int32_t floor = 0;
    Map2DTile tile;
    bool present = false;
    Cell(const ScriptFunctionCallContext& context, Args args) {
        if (!context.scene) return;
        runtime = context.scene->Runtime().Tilemaps();
        if (!runtime) return;
        entity = kb::scene::SceneEntity{Arg(args, "entity").AsUInt64()};
        map = runtime->Map(entity);
        x = static_cast<std::uint32_t>(Arg(args, "x").AsInt());
        y = static_cast<std::uint32_t>(Arg(args, "y").AsInt());
        floor = Arg(args, "floor").AsInt();
        if (!map || x >= map->width || y >= map->height || floor < Map2DAsset::MinimumFloor || floor > Map2DAsset::MaximumFloor) { map = nullptr; return; }
        const auto found = map->tiles.find(Map2DTileKey(x, y, floor));
        present = found != map->tiles.end();
        if (present) tile = found->second;
    }
    ScriptFunctionCallResult Save() {
        std::string error;
        if (!runtime->SetTile(entity, x, y, floor, tile, &error)) return Error(error);
        return {.executed = true, .outputs = {{"applied", ScriptValue{true}}}};
    }
};
ScriptFunctionCallResult Get(const ScriptFunctionCallContext& context, Args args) {
    const Cell cell(context, args);
    if (!cell.map) return Error("Tilemap entity or coordinates are invalid; World.Map2D must be active");
    return {.executed = true, .outputs = {{"present", ScriptValue{cell.present}},
        {"sprite", ScriptValue{cell.tile.ground.IsValid() ? FormatMap2DSprite(cell.tile.ground) : std::string{}}},
        {"blocked", ScriptValue{Map2DCellBlocked(*cell.map, static_cast<int>(cell.x), static_cast<int>(cell.y), cell.floor)}}}};
}
ScriptFunctionCallResult Set(const ScriptFunctionCallContext& context, Args args) {
    Cell cell(context, args);
    if (!cell.map) return Error("Tilemap entity or coordinates are invalid");
    const auto sprite = ParseMap2DSprite(Arg(args, "sprite").AsString());
    if (!sprite || !sprite->IsValid()) return Error("Sprite must identify a sheet and cell, for example 0:12");
    cell.tile.ground = *sprite;
    return cell.Save();
}
ScriptFunctionCallResult Erase(const ScriptFunctionCallContext& context, Args args) {
    Cell cell(context, args); if (!cell.map) return Error("Tilemap entity or coordinates are invalid");
    cell.tile = {}; return cell.Save();
}
ScriptFunctionCallResult Paint(const ScriptFunctionCallContext& context, Args args) {
    Cell cell(context, args); if (!cell.map) return Error("Tilemap entity or coordinates are invalid");
    std::string error;
    if (!cell.runtime->PaintGround(cell.entity, cell.x, cell.y, cell.floor, Arg(args, "brush").AsString(), &error)) return Error(error);
    return {.executed = true, .outputs = {{"applied", ScriptValue{true}}}};
}
template <typename Value> ScriptFunctionCallResult Property(const ScriptFunctionCallContext& context, Args args) {
    const Cell cell(context, args); if (!cell.map) return Error("Tilemap entity or coordinates are invalid");
    const auto value = Map2DCellProperty(*cell.map, cell.tile, Arg(args, "name").AsString());
    const auto* typed = value ? std::get_if<Value>(&*value) : nullptr;
    return {.executed = true, .outputs = {{"found", ScriptValue{typed != nullptr}}, {"value", ScriptValue{typed ? *typed : Value{}}}}};
}
template <typename Value> ScriptFunctionCallResult SetProperty(const ScriptFunctionCallContext& context, Args args) {
    Cell cell(context, args); if (!cell.map) return Error("Tilemap entity or coordinates are invalid");
    const auto& value = Arg(args, "value");
    Value converted{};
    if constexpr (std::is_same_v<Value, bool>) converted = value.AsBool();
    else if constexpr (std::is_same_v<Value, std::int64_t>) converted = value.AsInt64();
    else if constexpr (std::is_same_v<Value, double>) converted = value.AsDouble();
    else converted = value.AsString();
    cell.tile.properties[Arg(args, "name").AsString()] = converted;
    return cell.Save();
}
ScriptFunctionCallResult NextStepDetailed(const ScriptFunctionCallContext& context, Args args) {
    const Cell cell(context, args); if (!cell.map) return Error("Tilemap entity or coordinates are invalid");
    Map2DPathOptions options;
    options.floor = cell.floor; options.platformer = cell.map->orientation == Map2DOrientation::Wall;
    options.jumpHeight = static_cast<unsigned>(std::clamp(Arg(args, "jumpHeight").AsInt(3), 0, 16));
    options.jumpDistance = static_cast<unsigned>(std::clamp(Arg(args, "jumpDistance").AsInt(4), 0, 16));
    const int clearance = Arg(args, "clearance").AsInt(0);
    const int maximumVisited = Arg(args, "maximumVisited").AsInt(65536);
    const int diagonal = Arg(args, "diagonal").AsInt(0);
    options.maximumMilliseconds = Arg(args, "maximumMilliseconds").AsDouble(0.0);
    if (clearance < 0 || clearance > 8 || maximumVisited < 1 || maximumVisited > 1048576 ||
        diagonal < 0 || diagonal > 2 || !std::isfinite(options.maximumMilliseconds) ||
        options.maximumMilliseconds < 0.0 || options.maximumMilliseconds > 60000.0)
        return Error("Path options require clearance 0..8, diagonal 0..2, maximumVisited 1..1048576 and maximumMilliseconds 0..60000");
    options.clearanceCells = static_cast<unsigned>(clearance);
    options.maximumVisited = static_cast<std::size_t>(maximumVisited);
    options.diagonal = static_cast<Map2DDiagonalPolicy>(diagonal);
    options.costProperty = Arg(args, "costProperty").AsString();
    const auto result = FindMap2DPathResult(*cell.map, {static_cast<int>(cell.x), static_cast<int>(cell.y)},
        {Arg(args, "targetX").AsInt(), Arg(args, "targetY").AsInt()}, options);
    const auto& path = result.path;
    const auto next = path.empty() ? std::array<int, 2>{static_cast<int>(cell.x), static_cast<int>(cell.y)} : path[std::min<std::size_t>(1, path.size() - 1)];
    return {.executed = true, .outputs = {{"found", ScriptValue{!path.empty()}}, {"x", ScriptValue{next[0]}}, {"y", ScriptValue{next[1]}},
        {"cells", ScriptValue{static_cast<int>(path.size())}}, {"budgetExceeded", ScriptValue{result.status == Map2DPathStatus::BudgetExceeded}},
        {"cost", ScriptValue{result.cost}}, {"elapsedMilliseconds", ScriptValue{result.elapsedMilliseconds}}}};
}
ScriptFunctionCallResult NextStep(const ScriptFunctionCallContext& context, Args args) {
    auto result = NextStepDetailed(context, args);
    // Preserve the positional result contract of existing scripts and graphs.
    if (result.executed) result.outputs.resize(4U);
    return result;
}
}

bool ScriptMap2DApi::Register(ScriptRuntimeHost& host) {
    using T = ScriptValueType;
    const std::vector<ScriptFunctionPin> cell{{"entity", T::Entity, true}, {"x", T::Int, true}, {"y", T::Int, true}, {"floor", T::Int, false}};
    const auto add = [&](std::string name, std::vector<ScriptFunctionPin> extra, std::vector<ScriptFunctionPin> out, ScriptFunctionCallback callback) {
        ScriptFunctionDesc desc; desc.signature.name = "Map2D." + name; desc.signature.inputs = cell;
        if (name == "Get") desc.signature.description = "Read a live tilemap cell, its ground sprite identifier and pathfinding obstruction. Coordinates are zero-based; floor defaults to zero.";
        else if (name == "Set") desc.signature.description = "Replace a live cell's ground sprite using sheet:cell, preserving items and metadata. Rebuilds surrounding automatic edges and the affected render and physics chunks before simulation.";
        else if (name == "Erase") desc.signature.description = "Clear a live cell including items and metadata, then rebuild neighboring automatic edges. Does not modify the source asset.";
        else if (name == "PaintGround") desc.signature.description = "Paint a named embedded ground brush with its weighted variation and automatic edges. Does not modify the source asset.";
        else if (name == "NextStep" || name == "NextStepDetailed") desc.signature.description = "Find the next cell on a complete grid path to targetX,targetY. Side-view maps include clear jumps and falls using jumpHeight and jumpDistance in cells. Optional clearance, diagonal (0 orthogonal, 1 safe, 2 free), costProperty, maximumVisited and maximumMilliseconds control top-down queries. Zero milliseconds disables the time limit. NextStepDetailed also reports budget exhaustion, accumulated cost and elapsed milliseconds.";
        else if (name.starts_with("Get")) desc.signature.description = "Read typed tile data by name, using the cell override then the top graphic's defaults. Returns found=false when absent or of a different type.";
        else desc.signature.description = "Set a typed live cell data value by name. This overrides the graphic default and does not modify the source asset.";
        desc.signature.inputs.insert(desc.signature.inputs.end(), extra.begin(), extra.end());
        desc.signature.outputs = std::move(out); desc.callback = callback;
        return host.RegisterFunction(std::move(desc));
    };
    bool ok = add("Get", {}, {{"present", T::Bool, true}, {"sprite", T::String, true}, {"blocked", T::Bool, true}}, &Get);
    ok = add("Set", {{"sprite", T::String, true}}, {{"applied", T::Bool, true}}, &Set) && ok;
    ok = add("Erase", {}, {{"applied", T::Bool, true}}, &Erase) && ok;
    ok = add("PaintGround", {{"brush", T::String, true}}, {{"applied", T::Bool, true}}, &Paint) && ok;
    const std::vector<ScriptFunctionPin> pathInputs{{"targetX", T::Int, true}, {"targetY", T::Int, true}, {"jumpHeight", T::Int, false}, {"jumpDistance", T::Int, false},
        {"clearance", T::Int, false}, {"diagonal", T::Int, false}, {"costProperty", T::String, false},
        {"maximumVisited", T::Int, false}, {"maximumMilliseconds", T::Double, false}};
    ok = add("NextStep", pathInputs,
        {{"found", T::Bool, true}, {"x", T::Int, true}, {"y", T::Int, true}, {"cells", T::Int, true}}, &NextStep) && ok;
    ok = add("NextStepDetailed", pathInputs,
        {{"found", T::Bool, true}, {"x", T::Int, true}, {"y", T::Int, true}, {"cells", T::Int, true},
         {"budgetExceeded", T::Bool, true}, {"cost", T::Int64, true}, {"elapsedMilliseconds", T::Double, true}}, &NextStepDetailed) && ok;
    const auto property = [&]<typename Value>(std::string suffix, T type) {
        ok = add("Get" + suffix, {{"name", T::String, true}}, {{"found", T::Bool, true}, {"value", type, true}}, &Property<Value>) && ok;
        ok = add("Set" + suffix, {{"name", T::String, true}, {"value", type, true}}, {{"applied", T::Bool, true}}, &SetProperty<Value>) && ok;
    };
    property.operator()<bool>("Bool", T::Bool);
    property.operator()<std::int64_t>("Integer", T::Int64);
    property.operator()<double>("Number", T::Double);
    property.operator()<std::string>("Text", T::String);
    return ok;
}
} // namespace kb::script
