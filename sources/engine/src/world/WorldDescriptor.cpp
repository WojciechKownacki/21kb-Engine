#include "engine/world/WorldDescriptor.hpp"

#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldPartitionGrid.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "world/WorldTextFormat.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <span>

namespace kb::world {
namespace text {

bool ReadTextFile(const std::filesystem::path& path, std::string& output, std::string& error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        error = "could not open " + path.generic_string();
        return false;
    }
    output.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    if (input.bad()) {
        error = "could not read " + path.generic_string();
        return false;
    }
    return true;
}

bool WriteTextFileAtomically(const std::filesystem::path& path, std::string_view value, std::string& error) {
    std::error_code directoryError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), directoryError);
    }
    const std::span<const std::uint8_t> bytes{ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() };
    if (directoryError || !kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(path, bytes)) {
        error = "could not write " + path.generic_string();
        return false;
    }
    return true;
}

} // namespace text

namespace {

[[nodiscard]] bool IsSafeRelativeDirectory(std::string_view value) {
    if (value.empty() || value.size() > 255U) {
        return false;
    }
    const std::filesystem::path path{ std::string{ value } };
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part == ".." || part == ".") {
            return false;
        }
    }
    return true;
}

} // namespace

const WorldDataLayerDesc* WorldDescriptor::FindDataLayer(std::string_view layer) const noexcept {
    const auto found = std::ranges::find(dataLayers, layer, &WorldDataLayerDesc::name);
    return found == dataLayers.end() ? nullptr : &*found;
}

bool IsValidDataLayerName(std::string_view name) noexcept {
    if (name.empty() || name.size() > WorldDescriptor::MaxDataLayerNameBytes) {
        return false;
    }
    return std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
    });
}

std::string WorldDescriptorIO::Validate(const WorldDescriptor& descriptor) {
    if (descriptor.guid.empty() || descriptor.guid.size() > 128U) {
        return "world guid must be 1..128 characters";
    }
    if (descriptor.name.empty() || descriptor.name.size() > 255U) {
        return "world name must be 1..255 characters";
    }
    if (!IsValidCellSize(descriptor.cellSize)) {
        return "world cell size must be between 1 and 1000000 metres";
    }
    if (!IsSafeRelativeDirectory(descriptor.objectsDirectory)) {
        return "world object directory must be a relative path inside the world's folder";
    }
    if (descriptor.dataLayers.size() > WorldDescriptor::MaxDataLayers) {
        return "world declares more than 64 data layers";
    }
    std::set<std::string, std::less<>> names;
    for (const WorldDataLayerDesc& layer : descriptor.dataLayers) {
        if (!IsValidDataLayerName(layer.name)) {
            return "data layer name \"" + layer.name + "\" must be 1..64 letters, digits, '_', '-' or '.'";
        }
        if (!names.insert(layer.name).second) {
            return "data layer \"" + layer.name + "\" is declared twice";
        }
    }
    if (!std::isfinite(descriptor.hlod.range) || descriptor.hlod.range < 0.0) {
        return "HLOD range must be a finite, non-negative distance";
    }
    if (!std::isfinite(descriptor.hlod.triangleRatio) || descriptor.hlod.triangleRatio <= 0.0 || descriptor.hlod.triangleRatio > 1.0) {
        return "HLOD triangle ratio must be in (0, 1]";
    }
    if (descriptor.tagDefinitions.size() > 256U) {
        return "world declares more than 256 tags";
    }
    for (const std::string& tag : descriptor.tagDefinitions) {
        if (tag.empty() || tag.size() > 255U) {
            return "world tag names must be 1..255 characters";
        }
    }
    return {};
}

WorldDescriptorReadResult WorldDescriptorIO::Parse(std::string_view source) {
    kb::core::JsonValue root;
    std::string error;
    if (!kb::core::JsonValue::Parse(source, root, error)) {
        return { .succeeded = false, .descriptor = {}, .error = "world file is not valid JSON: " + error };
    }
    if (root.GetKind() != kb::core::JsonValue::Kind::Object) {
        return { .succeeded = false, .descriptor = {}, .error = "world file must be a JSON object" };
    }
    const std::string* schema = text::String(root, "schema");
    if (schema == nullptr || *schema != WorldDescriptor::Schema) {
        return { .succeeded = false, .descriptor = {}, .error = "world file schema must be \"" + std::string{ WorldDescriptor::Schema } + "\"" };
    }
    WorldDescriptor descriptor;
    const std::string* guid = text::String(root, "guid");
    const std::string* name = text::String(root, "name");
    const std::string* objects = text::String(root, "objects");
    const std::optional<double> cellSize = text::Double(root, "cellSize");
    if (guid == nullptr || name == nullptr || objects == nullptr || !cellSize.has_value()) {
        return { .succeeded = false, .descriptor = {}, .error = "world file needs string guid, name and objects and a numeric cellSize" };
    }
    descriptor.guid = *guid;
    descriptor.name = *name;
    descriptor.objectsDirectory = *objects;
    descriptor.cellSize = *cellSize;
    if (const kb::core::JsonValue* layers = root.Find("dataLayers"); layers != nullptr) {
        if (layers->GetKind() != kb::core::JsonValue::Kind::Array) {
            return { .succeeded = false, .descriptor = {}, .error = "world dataLayers must be an array" };
        }
        for (std::size_t index = 0U; index < layers->Size(); ++index) {
            const kb::core::JsonValue& layer = *layers->At(index);
            const std::string* layerName = layer.GetKind() == kb::core::JsonValue::Kind::Object ? text::String(layer, "name") : nullptr;
            if (layerName == nullptr) {
                return { .succeeded = false, .descriptor = {}, .error = "every world data layer needs a name" };
            }
            const std::optional<bool> active = text::Bool(layer, "initiallyActive");
            if (layer.Find("initiallyActive") != nullptr && !active.has_value()) {
                return { .succeeded = false, .descriptor = {}, .error = "data layer initiallyActive must be a boolean" };
            }
            descriptor.dataLayers.push_back({ .name = *layerName, .initiallyActive = active.value_or(true) });
        }
    }
    if (const kb::core::JsonValue* hlod = root.Find("hlod"); hlod != nullptr) {
        if (hlod->GetKind() != kb::core::JsonValue::Kind::Object) {
            return { .succeeded = false, .descriptor = {}, .error = "world hlod must be an object" };
        }
        if (hlod->Find("enabled") != nullptr) {
            const std::optional<bool> enabled = text::Bool(*hlod, "enabled");
            if (!enabled.has_value()) return { .succeeded = false, .descriptor = {}, .error = "hlod enabled must be a boolean" };
            descriptor.hlod.enabled = *enabled;
        }
        if (hlod->Find("range") != nullptr) {
            const std::optional<double> range = text::Double(*hlod, "range");
            if (!range.has_value()) return { .succeeded = false, .descriptor = {}, .error = "hlod range must be a number" };
            descriptor.hlod.range = *range;
        }
        if (hlod->Find("triangleRatio") != nullptr) {
            const std::optional<double> ratio = text::Double(*hlod, "triangleRatio");
            if (!ratio.has_value()) return { .succeeded = false, .descriptor = {}, .error = "hlod triangleRatio must be a number" };
            descriptor.hlod.triangleRatio = *ratio;
        }
    }
    if (const kb::core::JsonValue* tags = root.Find("tags"); tags != nullptr) {
        if (tags->GetKind() != kb::core::JsonValue::Kind::Array) {
            return { .succeeded = false, .descriptor = {}, .error = "world tags must be an array of strings" };
        }
        descriptor.tagDefinitions.clear();
        for (std::size_t index = 0U; index < tags->Size(); ++index) {
            if (tags->At(index)->GetKind() != kb::core::JsonValue::Kind::String) {
                return { .succeeded = false, .descriptor = {}, .error = "world tags must be an array of strings" };
            }
            descriptor.tagDefinitions.push_back(tags->At(index)->AsString());
        }
    }
    if (std::string invalid = Validate(descriptor); !invalid.empty()) {
        return { .succeeded = false, .descriptor = {}, .error = std::move(invalid) };
    }
    return { .succeeded = true, .descriptor = std::move(descriptor), .error = {} };
}

WorldDescriptorReadResult WorldDescriptorIO::Read(const std::filesystem::path& path) {
    std::string source;
    std::string error;
    if (!text::ReadTextFile(path, source, error)) {
        return { .succeeded = false, .descriptor = {}, .error = std::move(error) };
    }
    WorldDescriptorReadResult result = Parse(source);
    if (!result.succeeded) {
        result.error = path.generic_string() + ": " + result.error;
    }
    return result;
}

std::string WorldDescriptorIO::Serialize(const WorldDescriptor& descriptor) {
    if (!Validate(descriptor).empty()) {
        return {};
    }
    std::string out = "{\n  \"schema\": ";
    text::AppendQuoted(out, WorldDescriptor::Schema);
    out += ",\n  \"guid\": ";
    text::AppendQuoted(out, descriptor.guid);
    out += ",\n  \"name\": ";
    text::AppendQuoted(out, descriptor.name);
    out += ",\n  \"cellSize\": " + text::Number(descriptor.cellSize);
    out += ",\n  \"objects\": ";
    text::AppendQuoted(out, descriptor.objectsDirectory);
    out += ",\n  \"dataLayers\": [";
    for (std::size_t index = 0U; index < descriptor.dataLayers.size(); ++index) {
        out += index == 0U ? "\n    {\"name\": " : ",\n    {\"name\": ";
        text::AppendQuoted(out, descriptor.dataLayers[index].name);
        out += ", \"initiallyActive\": ";
        out += descriptor.dataLayers[index].initiallyActive ? "true}" : "false}";
    }
    out += descriptor.dataLayers.empty() ? "]" : "\n  ]";
    out += ",\n  \"hlod\": {\"enabled\": ";
    out += descriptor.hlod.enabled ? "true" : "false";
    out += ", \"range\": " + text::Number(descriptor.hlod.range);
    out += ", \"triangleRatio\": " + text::Number(descriptor.hlod.triangleRatio) + "}";
    out += ",\n  \"tags\": [";
    for (std::size_t index = 0U; index < descriptor.tagDefinitions.size(); ++index) {
        if (index != 0U) out += ", ";
        text::AppendQuoted(out, descriptor.tagDefinitions[index]);
    }
    out += "]\n}\n";
    return out;
}

bool WorldDescriptorIO::Write(const std::filesystem::path& path, const WorldDescriptor& descriptor, std::string& error) {
    if (path.extension() != WorldDescriptor::Extension) {
        error = "world files use the " + std::string{ WorldDescriptor::Extension } + " extension";
        return false;
    }
    if (std::string invalid = Validate(descriptor); !invalid.empty()) {
        error = std::move(invalid);
        return false;
    }
    return text::WriteTextFileAtomically(path, Serialize(descriptor), error);
}

std::filesystem::path WorldPaths::ObjectsDirectory(const std::filesystem::path& descriptorPath, const WorldDescriptor& descriptor) {
    return (descriptorPath.parent_path() / std::filesystem::path{ descriptor.objectsDirectory }).lexically_normal();
}

std::filesystem::path WorldPaths::CellsDirectory(const std::filesystem::path& descriptorPath) {
    return descriptorPath.parent_path() / (descriptorPath.stem().string() + ".cells");
}

std::filesystem::path WorldPaths::CellIndexPath(const std::filesystem::path& descriptorPath) {
    return CellsDirectory(descriptorPath) / (descriptorPath.stem().string() + std::string{ WorldCellIndex::Extension });
}

std::string WorldPaths::CellIndexVirtualPath(std::string_view descriptorVirtualPath) {
    const std::filesystem::path path{ std::string{ descriptorVirtualPath } };
    return CellIndexPath(path).generic_string();
}

} // namespace kb::world
