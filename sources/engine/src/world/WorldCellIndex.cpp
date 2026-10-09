#include "engine/world/WorldCellIndex.hpp"

#include "world/WorldTextFormat.hpp"

#include <algorithm>
#include <charconv>
#include <set>
#include <tuple>

namespace kb::world {
namespace {

using kb::core::JsonValue;

[[nodiscard]] bool IsSafeRelativeFile(std::string_view value) {
    if (value.empty() || value.size() > 1024U || value.find('\\') != std::string_view::npos || value.front() == '/') {
        return false;
    }
    const std::filesystem::path path{ std::string{ value } };
    if (path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part == ".." || part == ".") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> ParseUInt64(std::string_view value) {
    std::uint64_t parsed = 0U;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() || value.empty()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] bool ReadCoord(const JsonValue& object, WorldCellCoord& coord) {
    const std::optional<std::int64_t> x = text::Int(object, "x", -kMaxSerializedCellCoordinate, kMaxSerializedCellCoordinate);
    const std::optional<std::int64_t> z = text::Int(object, "z", -kMaxSerializedCellCoordinate, kMaxSerializedCellCoordinate);
    if (!x.has_value() || !z.has_value()) {
        return false;
    }
    coord = { *x, *z };
    return true;
}

constexpr std::int64_t kMaxCount = std::int64_t{ 1 } << 32;
constexpr std::int64_t kMaxBytes = std::int64_t{ 1 } << 52;

} // namespace

std::string ResolveWorldCellPath(std::string_view indexPath, std::string_view relative) {
    const std::filesystem::path base = std::filesystem::path{ std::string{ indexPath } }.parent_path();
    return (base / std::filesystem::path{ std::string{ relative } }).lexically_normal().generic_string();
}

std::string WorldCellIndexIO::Validate(const WorldCellIndex& index) {
    if (index.worldGuid.empty() || index.worldName.empty()) {
        return "cell index needs the world's guid and name";
    }
    if (!IsValidCellSize(index.cellSize)) {
        return "cell index cell size is invalid";
    }
    if (index.regionCells == 0U || index.regionCells > WorldDescriptor::MaxRegionCells) {
        return "cell index region size is invalid";
    }
    if (!std::isfinite(index.hlodRange) || index.hlodRange < 0.0) {
        return "cell index HLOD range is invalid";
    }
    std::set<std::string, std::less<>> layers;
    for (const WorldDataLayerDesc& layer : index.dataLayers) {
        if (!IsValidDataLayerName(layer.name) || !layers.insert(layer.name).second) {
            return "cell index data layer \"" + layer.name + "\" is invalid or repeated";
        }
    }
    std::set<std::tuple<bool, std::int64_t, std::int64_t, std::string>> units;
    for (const WorldCellUnit& unit : index.units) {
        if (!unit.dataLayer.empty() && !layers.contains(unit.dataLayer)) {
            return "cell unit uses undeclared data layer \"" + unit.dataLayer + "\"";
        }
        if (!IsSafeRelativeFile(unit.scene)) {
            return "cell unit scene path \"" + unit.scene + "\" must be relative to the index";
        }
        if (!unit.persistent && !IsSerializableCellCoord(unit.coord)) {
            return "cell unit coordinate is out of range";
        }
        const WorldCellCoord key = unit.persistent ? WorldCellCoord{} : unit.coord;
        if (!units.emplace(unit.persistent, key.x, key.z, unit.dataLayer).second) {
            return "cell index lists one cell and layer twice";
        }
    }
    std::set<WorldCellCoord> hlods;
    for (const WorldCellHlod& hlod : index.hlods) {
        if (!IsSafeRelativeFile(hlod.mesh) || !IsSerializableCellCoord(hlod.coord) || !hlods.insert(hlod.coord).second) {
            return "cell index HLOD entry is invalid or repeated";
        }
        if (hlod.materials.size() > 8U) {
            return "cell index HLOD uses more than 8 material slots";
        }
    }
    std::set<WorldCellCoord> navMeshes;
    for (const WorldCellNavMesh& navMesh : index.navMeshes) {
        if (!IsSafeRelativeFile(navMesh.mesh) || !IsSerializableCellCoord(navMesh.coord) || !navMeshes.insert(navMesh.coord).second) {
            return "cell index navigation mesh entry is invalid or repeated";
        }
    }
    return {};
}

WorldCellIndexReadResult WorldCellIndexIO::Parse(std::string_view source) {
    JsonValue root;
    std::string error;
    if (!JsonValue::Parse(source, root, error)) {
        return { .succeeded = false, .index = {}, .error = "cell index is not valid JSON: " + error };
    }
    const std::string* schema = root.GetKind() == JsonValue::Kind::Object ? text::String(root, "schema") : nullptr;
    if (schema == nullptr || *schema != WorldCellIndex::Schema) {
        return { .succeeded = false, .index = {}, .error = "cell index schema must be \"" + std::string{ WorldCellIndex::Schema } + "\"" };
    }
    WorldCellIndex index;
    const std::string* guid = text::String(root, "worldGuid");
    const std::string* name = text::String(root, "worldName");
    const std::optional<double> cellSize = text::Double(root, "cellSize");
    const std::optional<double> hlodRange = text::Double(root, "hlodRange");
    const JsonValue* layers = root.Find("dataLayers");
    const JsonValue* units = root.Find("units");
    const JsonValue* hlods = root.Find("hlods");
    if (guid == nullptr || name == nullptr || !cellSize || !hlodRange || layers == nullptr || units == nullptr || hlods == nullptr ||
        layers->GetKind() != JsonValue::Kind::Array || units->GetKind() != JsonValue::Kind::Array || hlods->GetKind() != JsonValue::Kind::Array) {
        return { .succeeded = false, .index = {}, .error = "cell index is missing required fields" };
    }
    index.worldGuid = *guid;
    index.worldName = *name;
    index.cellSize = *cellSize;
    if (root.Find("regionCells") != nullptr) {
        const std::optional<std::int64_t> regionCells = text::Int(root, "regionCells", 1, WorldDescriptor::MaxRegionCells);
        if (!regionCells.has_value()) {
            return { .succeeded = false, .index = {}, .error = "cell index region size is invalid" };
        }
        index.regionCells = static_cast<std::uint32_t>(*regionCells);
    }
    index.hlodRange = *hlodRange;
    for (std::size_t item = 0U; item < layers->Size(); ++item) {
        const JsonValue& layer = *layers->At(item);
        const std::string* layerName = layer.GetKind() == JsonValue::Kind::Object ? text::String(layer, "name") : nullptr;
        const std::optional<bool> active = layerName != nullptr ? text::Bool(layer, "initiallyActive") : std::nullopt;
        if (layerName == nullptr || !active.has_value()) {
            return { .succeeded = false, .index = {}, .error = "cell index data layer entry is invalid" };
        }
        index.dataLayers.push_back({ .name = *layerName, .initiallyActive = *active });
    }
    index.units.reserve(units->Size());
    for (std::size_t item = 0U; item < units->Size(); ++item) {
        const JsonValue& entry = *units->At(item);
        WorldCellUnit unit;
        const std::optional<bool> persistent = entry.GetKind() == JsonValue::Kind::Object ? text::Bool(entry, "persistent") : std::nullopt;
        const std::string* layer = persistent ? text::String(entry, "layer") : nullptr;
        const std::string* scene = persistent ? text::String(entry, "scene") : nullptr;
        const std::optional<std::int64_t> objects = persistent ? text::Int(entry, "objects", 0, kMaxCount) : std::nullopt;
        const std::optional<std::int64_t> nodes = persistent ? text::Int(entry, "nodes", 0, kMaxCount) : std::nullopt;
        const std::optional<std::int64_t> bytes = persistent ? text::Int(entry, "bytes", 0, kMaxBytes) : std::nullopt;
        if (!persistent || layer == nullptr || scene == nullptr || !objects || !nodes || !bytes ||
            (!*persistent && !ReadCoord(entry, unit.coord))) {
            return { .succeeded = false, .index = {}, .error = "cell index unit " + std::to_string(item) + " is invalid" };
        }
        unit.persistent = *persistent;
        unit.dataLayer = *layer;
        unit.scene = *scene;
        unit.objectCount = static_cast<std::uint32_t>(std::min<std::int64_t>(*objects, UINT32_MAX));
        unit.nodeCount = static_cast<std::uint32_t>(std::min<std::int64_t>(*nodes, UINT32_MAX));
        unit.estimatedBytes = static_cast<std::uint64_t>(*bytes);
        index.units.push_back(std::move(unit));
    }
    index.hlods.reserve(hlods->Size());
    for (std::size_t item = 0U; item < hlods->Size(); ++item) {
        const JsonValue& entry = *hlods->At(item);
        WorldCellHlod hlod;
        const std::string* mesh = entry.GetKind() == JsonValue::Kind::Object ? text::String(entry, "mesh") : nullptr;
        const JsonValue* materials = mesh != nullptr ? entry.Find("materials") : nullptr;
        const std::optional<std::int64_t> triangles = mesh != nullptr ? text::Int(entry, "triangles", 0, kMaxCount) : std::nullopt;
        const std::optional<std::int64_t> sourceTriangles = mesh != nullptr ? text::Int(entry, "sourceTriangles", 0, kMaxCount) : std::nullopt;
        if (mesh == nullptr || materials == nullptr || materials->GetKind() != JsonValue::Kind::Array || !triangles || !sourceTriangles ||
            !ReadCoord(entry, hlod.coord)) {
            return { .succeeded = false, .index = {}, .error = "cell index HLOD " + std::to_string(item) + " is invalid" };
        }
        hlod.mesh = *mesh;
        hlod.triangleCount = static_cast<std::uint32_t>(*triangles);
        hlod.sourceTriangleCount = static_cast<std::uint32_t>(*sourceTriangles);
        for (std::size_t slot = 0U; slot < materials->Size(); ++slot) {
            // Asset ids use all 64 bits, more than a JSON number holds exactly.
            const JsonValue& material = *materials->At(slot);
            const std::optional<std::uint64_t> id = material.GetKind() == JsonValue::Kind::String ? ParseUInt64(material.AsString()) : std::nullopt;
            if (!id.has_value()) {
                return { .succeeded = false, .index = {}, .error = "cell index HLOD material ids must be decimal strings" };
            }
            hlod.materials.push_back(*id);
        }
        index.hlods.push_back(std::move(hlod));
    }
    if (const JsonValue* navMeshes = root.Find("navMeshes"); navMeshes != nullptr) {
        if (navMeshes->GetKind() != JsonValue::Kind::Array) {
            return { .succeeded = false, .index = {}, .error = "cell index navMeshes must be an array" };
        }
        index.navMeshes.reserve(navMeshes->Size());
        for (std::size_t item = 0U; item < navMeshes->Size(); ++item) {
            const JsonValue& entry = *navMeshes->At(item);
            WorldCellNavMesh navMesh;
            const std::string* mesh = entry.GetKind() == JsonValue::Kind::Object ? text::String(entry, "mesh") : nullptr;
            const std::optional<std::int64_t> tiles = mesh != nullptr ? text::Int(entry, "tiles", 0, kMaxCount) : std::nullopt;
            if (mesh == nullptr || !tiles || !ReadCoord(entry, navMesh.coord)) {
                return { .succeeded = false, .index = {}, .error = "cell index navigation mesh " + std::to_string(item) + " is invalid" };
            }
            navMesh.mesh = *mesh;
            navMesh.tileCount = static_cast<std::uint32_t>(std::min<std::int64_t>(*tiles, UINT32_MAX));
            index.navMeshes.push_back(std::move(navMesh));
        }
    }
    if (std::string invalid = Validate(index); !invalid.empty()) {
        return { .succeeded = false, .index = {}, .error = std::move(invalid) };
    }
    return { .succeeded = true, .index = std::move(index), .error = {} };
}

WorldCellIndexReadResult WorldCellIndexIO::Read(const std::filesystem::path& path) {
    std::string source;
    std::string error;
    if (!text::ReadTextFile(path, source, error)) {
        return { .succeeded = false, .index = {}, .error = std::move(error) };
    }
    WorldCellIndexReadResult result = Parse(source);
    if (!result.succeeded) {
        result.error = path.generic_string() + ": " + result.error;
    }
    return result;
}

std::string WorldCellIndexIO::Serialize(const WorldCellIndex& index) {
    std::string out = "{\n  \"schema\": ";
    text::AppendQuoted(out, WorldCellIndex::Schema);
    out += ",\n  \"worldGuid\": ";
    text::AppendQuoted(out, index.worldGuid);
    out += ",\n  \"worldName\": ";
    text::AppendQuoted(out, index.worldName);
    out += ",\n  \"cellSize\": " + text::Number(index.cellSize);
    out += ",\n  \"regionCells\": " + std::to_string(index.regionCells);
    out += ",\n  \"hlodRange\": " + text::Number(index.hlodRange);
    out += ",\n  \"dataLayers\": [";
    for (std::size_t item = 0U; item < index.dataLayers.size(); ++item) {
        out += item == 0U ? "\n    {\"name\": " : ",\n    {\"name\": ";
        text::AppendQuoted(out, index.dataLayers[item].name);
        out += index.dataLayers[item].initiallyActive ? ", \"initiallyActive\": true}" : ", \"initiallyActive\": false}";
    }
    out += index.dataLayers.empty() ? "]" : "\n  ]";
    out += ",\n  \"units\": [";
    for (std::size_t item = 0U; item < index.units.size(); ++item) {
        const WorldCellUnit& unit = index.units[item];
        out += item == 0U ? "\n    {" : ",\n    {";
        out += unit.persistent ? "\"persistent\": true" : "\"persistent\": false, \"x\": " + text::Integer(unit.coord.x) + ", \"z\": " + text::Integer(unit.coord.z);
        out += ", \"layer\": ";
        text::AppendQuoted(out, unit.dataLayer);
        out += ", \"scene\": ";
        text::AppendQuoted(out, unit.scene);
        out += ", \"objects\": " + std::to_string(unit.objectCount);
        out += ", \"nodes\": " + std::to_string(unit.nodeCount);
        out += ", \"bytes\": " + std::to_string(unit.estimatedBytes) + "}";
    }
    out += index.units.empty() ? "]" : "\n  ]";
    out += ",\n  \"hlods\": [";
    for (std::size_t item = 0U; item < index.hlods.size(); ++item) {
        const WorldCellHlod& hlod = index.hlods[item];
        out += item == 0U ? "\n    {" : ",\n    {";
        out += "\"x\": " + text::Integer(hlod.coord.x) + ", \"z\": " + text::Integer(hlod.coord.z) + ", \"mesh\": ";
        text::AppendQuoted(out, hlod.mesh);
        out += ", \"materials\": [";
        for (std::size_t slot = 0U; slot < hlod.materials.size(); ++slot) {
            if (slot != 0U) out += ", ";
            out += '"' + std::to_string(hlod.materials[slot]) + '"';
        }
        out += "], \"triangles\": " + std::to_string(hlod.triangleCount);
        out += ", \"sourceTriangles\": " + std::to_string(hlod.sourceTriangleCount) + "}";
    }
    out += index.hlods.empty() ? "]" : "\n  ]";
    if (!index.navMeshes.empty()) {
        out += ",\n  \"navMeshes\": [";
        for (std::size_t item = 0U; item < index.navMeshes.size(); ++item) {
            const WorldCellNavMesh& navMesh = index.navMeshes[item];
            out += item == 0U ? "\n    {" : ",\n    {";
            out += "\"x\": " + text::Integer(navMesh.coord.x) + ", \"z\": " + text::Integer(navMesh.coord.z) + ", \"mesh\": ";
            text::AppendQuoted(out, navMesh.mesh);
            out += ", \"tiles\": " + std::to_string(navMesh.tileCount) + "}";
        }
        out += "\n  ]";
    }
    out += "\n}\n";
    return out;
}

bool WorldCellIndexIO::Write(const std::filesystem::path& path, const WorldCellIndex& index, std::string& error) {
    if (std::string invalid = Validate(index); !invalid.empty()) {
        error = std::move(invalid);
        return false;
    }
    return text::WriteTextFileAtomically(path, Serialize(index), error);
}

} // namespace kb::world
