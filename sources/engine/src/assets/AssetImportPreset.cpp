#include "engine/assets/AssetImportPreset.hpp"

#include "engine/assets/AssetMemoryInputStream.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>
#include <utility>

namespace kb::assets {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& output) noexcept {
    text = Trim(text);
    if (text == "1" || text == "true" || text == "yes") {
        output = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "no") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
        return false;
    }
    output = value;
    return true;
}

// Three numbers separated by spaces or commas: "0 90 0" or "0,90,0".
[[nodiscard]] bool ParseRotation(std::string_view text, std::array<float, 3>& output) noexcept {
    std::array<float, 3> values{};
    std::size_t index = 0U;
    std::size_t start = 0U;
    text = Trim(text);
    while (start <= text.size()) {
        std::size_t end = text.find_first_of(" ,\t", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view token = text.substr(start, end - start);
        if (!token.empty()) {
            if (index >= values.size() || !ParseFloat(token, values[index])) {
                return false;
            }
            ++index;
        }
        start = end + 1U;
    }
    if (index != values.size()) {
        return false;
    }
    output = values;
    return true;
}

enum class FieldResult { Applied, Invalid, Unknown };

[[nodiscard]] FieldResult Applied(bool parsed) noexcept {
    return parsed ? FieldResult::Applied : FieldResult::Invalid;
}

[[nodiscard]] FieldResult ApplyField(std::string_view key, std::string_view value, AssetImportOptions& options) {
    AssetMeshImportOptions& mesh = options.mesh;
    if (key == "scale") {
        float scale = 0.0F;
        if (!ParseFloat(value, scale) || scale <= 0.0F) {
            return FieldResult::Invalid;
        }
        mesh.transform.scale = scale;
        return FieldResult::Applied;
    }
    if (key == "rotation") return Applied(ParseRotation(value, mesh.transform.rotationDegrees));
    if (key == "importMaterialSlots") return Applied(ParseBool(value, mesh.importMaterialSlots));
    if (key == "importTextures") return Applied(ParseBool(value, mesh.importTextures));
    if (key == "importMaterials") return Applied(ParseBool(value, mesh.importMaterials));
    if (key == "combineMeshes") return Applied(ParseBool(value, mesh.combineMeshes));
    if (key == "importSkeletalMesh") return Applied(ParseBool(value, mesh.importSkeletalMesh));
    return FieldResult::Unknown;
}

[[nodiscard]] std::string FloatText(float value) {
    std::ostringstream output;
    output << value;
    return output.str();
}

} // namespace

std::optional<AssetImportOptions> AssetImportPresetIO::Parse(std::istream& input, std::string* error) {
    AssetImportOptions options{};
    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string_view content{ line };
        if (const std::size_t comment = content.find('#'); comment != std::string_view::npos) {
            content = content.substr(0U, comment);
        }
        content = Trim(content);
        if (content.empty()) {
            continue;
        }
        const std::size_t separator = content.find_first_of(" \t");
        const std::string_view key = content.substr(0U, separator);
        const std::string_view value = separator == std::string_view::npos ? std::string_view{} : Trim(content.substr(separator + 1U));
        if (ApplyField(key, value, options) == FieldResult::Invalid) {
            if (error != nullptr) {
                *error = "line " + std::to_string(lineNumber) + ": '" + std::string{ key } + "' does not take '" + std::string{ value } + "'";
            }
            return std::nullopt;
        }
    }
    return options;
}

std::optional<AssetImportOptions> AssetImportPresetIO::Load(const std::filesystem::path& path, std::string* error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        if (error != nullptr) {
            *error = "the import preset could not be opened";
        }
        return std::nullopt;
    }
    return Parse(input, error);
}

std::string AssetImportPresetIO::Write(const AssetImportOptions& options) {
    const AssetMeshImportOptions& mesh = options.mesh;
    std::string text = "# 21kb import preset\n";
    text += "scale " + FloatText(mesh.transform.scale) + "\n";
    text += "rotation " + FloatText(mesh.transform.rotationDegrees[0]) + " " + FloatText(mesh.transform.rotationDegrees[1]) + " " +
        FloatText(mesh.transform.rotationDegrees[2]) + "\n";
    text += std::string{ "importMaterialSlots " } + (mesh.importMaterialSlots ? "1" : "0") + "\n";
    text += std::string{ "importTextures " } + (mesh.importTextures ? "1" : "0") + "\n";
    text += std::string{ "importMaterials " } + (mesh.importMaterials ? "1" : "0") + "\n";
    text += std::string{ "combineMeshes " } + (mesh.combineMeshes ? "1" : "0") + "\n";
    text += std::string{ "importSkeletalMesh " } + (mesh.importSkeletalMesh ? "1" : "0") + "\n";
    return text;
}

bool AssetImportPresetIO::Save(const std::filesystem::path& path, const AssetImportOptions& options) {
    if (!options.mesh.transform.IsValid()) {
        return false;
    }
    const std::filesystem::path tempPath = path.string() + ".tmp";
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return false;
        }
        const std::string text = Write(options);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output.good()) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(tempPath, ignored);
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

bool AssetImportPresetIO::SetField(AssetImportOptions& options, std::string_view key, std::string_view value) {
    AssetImportOptions changed = options;
    if (ApplyField(Trim(key), Trim(value), changed) != FieldResult::Applied) {
        return false;
    }
    options = changed;
    return true;
}

std::vector<std::string> AssetImportPresetIO::FieldNames() {
    return { "scale", "rotation", "importMaterialSlots", "importTextures", "importMaterials", "combineMeshes", "importSkeletalMesh" };
}

std::string_view AssetImportPresetLoader::Type() const noexcept {
    return kAssetImportPresetAssetType;
}

std::type_index AssetImportPresetLoader::PayloadType() const noexcept {
    return typeid(AssetImportOptions);
}

std::vector<std::string> AssetImportPresetLoader::Extensions() const {
    return { kAssetImportPresetExtension };
}

AssetLoadResult AssetImportPresetLoader::Load(const AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    AssetMemoryInputStream input{ sourceBytes };
    std::optional<AssetImportOptions> options = AssetImportPresetIO::Parse(input, &error);
    if (!options.has_value()) {
        return AssetLoadResult{ .asset = {}, .error = "Import preset could not be parsed: " + error };
    }
    return AssetLoadResult{ .asset = std::make_shared<AssetImportOptions>(*options), .error = {} };
}

} // namespace kb::assets
