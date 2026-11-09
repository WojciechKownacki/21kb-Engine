#include "TerrainOps.hpp"

#include "TerrainAnalysis.hpp"
#include "TerrainHeightmapImporter.hpp"
#include "TerrainLayerPainter.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace kb::terrain_editor {
namespace {

using kb::core::JsonValue;

struct Point {
    float x = 0.0F;
    float z = 0.0F;
};

// Reads the members of one operation. A wrong member sets the error and every later read gives nothing.
class Reader final {
public:
    Reader(const JsonValue& operation, std::string& error) : operation_(operation), error_(error) {}

    [[nodiscard]] bool Failed() const noexcept { return !error_.empty(); }

    [[nodiscard]] std::optional<double> Number(std::string_view key) {
        const JsonValue* value = operation_.Find(key);
        if (value == nullptr || Failed()) return std::nullopt;
        if (value->GetKind() == JsonValue::Kind::Number && std::isfinite(value->AsNumber())) return value->AsNumber();
        if (value->GetKind() == JsonValue::Kind::String) {
            if (const std::optional<double> parsed = ParseNumber(value->AsString())) return parsed;
        }
        Fail(key, "must be a number");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> Text(std::string_view key) {
        const JsonValue* value = operation_.Find(key);
        if (value == nullptr || Failed()) return std::nullopt;
        if (value->GetKind() == JsonValue::Kind::String) return value->AsString();
        Fail(key, "must be text");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<bool> Flag(std::string_view key) {
        const JsonValue* value = operation_.Find(key);
        if (value == nullptr || Failed()) return std::nullopt;
        if (value->GetKind() == JsonValue::Kind::Bool) return value->AsBool();
        if (value->GetKind() == JsonValue::Kind::String) {
            const std::string& text = value->AsString();
            if (text == "yes" || text == "true") return true;
            if (text == "no" || text == "false") return false;
        }
        Fail(key, "must be true/false (or yes/no)");
        return std::nullopt;
    }

    // "a..b" or [a, b].
    [[nodiscard]] std::optional<std::pair<float, float>> Range(std::string_view key) {
        const JsonValue* value = operation_.Find(key);
        if (value == nullptr || Failed()) return std::nullopt;
        std::optional<double> first;
        std::optional<double> second;
        if (value->GetKind() == JsonValue::Kind::String) {
            const std::string& text = value->AsString();
            const std::size_t dots = text.find("..");
            if (dots != std::string::npos) {
                first = ParseNumber(std::string_view{ text }.substr(0U, dots));
                second = ParseNumber(std::string_view{ text }.substr(dots + 2U));
            }
        } else if (value->GetKind() == JsonValue::Kind::Array && value->Size() == 2U) {
            if (value->At(0U)->GetKind() == JsonValue::Kind::Number) first = value->At(0U)->AsNumber();
            if (value->At(1U)->GetKind() == JsonValue::Kind::Number) second = value->At(1U)->AsNumber();
        }
        if (!first || !second || *first > *second) {
            Fail(key, "must be a range low..high");
            return std::nullopt;
        }
        return std::pair{ static_cast<float>(*first), static_cast<float>(*second) };
    }

    // [[x, z], ...] or "x,z;x,z", else the single point of x and z.
    [[nodiscard]] std::vector<Point> Points() {
        std::vector<Point> points;
        if (Failed()) return points;
        if (const JsonValue* path = operation_.Find("path")) {
            if (path->GetKind() == JsonValue::Kind::Array) {
                for (std::size_t index = 0U; index < path->Size(); ++index) {
                    const JsonValue* pair = path->At(index);
                    if (pair->GetKind() != JsonValue::Kind::Array || pair->Size() != 2U ||
                        pair->At(0U)->GetKind() != JsonValue::Kind::Number || pair->At(1U)->GetKind() != JsonValue::Kind::Number ||
                        !std::isfinite(pair->At(0U)->AsNumber()) || !std::isfinite(pair->At(1U)->AsNumber())) {
                        Fail("path", "must be [[x,z],...] or \"x,z;x,z\"");
                        return {};
                    }
                    points.push_back({ static_cast<float>(pair->At(0U)->AsNumber()), static_cast<float>(pair->At(1U)->AsNumber()) });
                }
            } else if (path->GetKind() == JsonValue::Kind::String) {
                std::string_view rest = path->AsString();
                while (!rest.empty()) {
                    const std::size_t semicolon = rest.find(';');
                    const std::string_view part = rest.substr(0U, semicolon);
                    const std::size_t comma = part.find(',');
                    const std::optional<double> x = comma == std::string_view::npos ? std::nullopt : ParseNumber(part.substr(0U, comma));
                    const std::optional<double> z = comma == std::string_view::npos ? std::nullopt : ParseNumber(part.substr(comma + 1U));
                    if (!x || !z) {
                        Fail("path", "must be [[x,z],...] or \"x,z;x,z\"");
                        return {};
                    }
                    points.push_back({ static_cast<float>(*x), static_cast<float>(*z) });
                    if (semicolon == std::string_view::npos) break;
                    rest.remove_prefix(semicolon + 1U);
                }
            } else {
                Fail("path", "must be [[x,z],...] or \"x,z;x,z\"");
                return {};
            }
            if (points.empty()) Fail("path", "has no points");
            return points;
        }
        const std::optional<double> x = Number("x");
        const std::optional<double> z = Number("z");
        if (Failed()) return {};
        if (!x || !z) {
            error_ = "needs x and z, or a path";
            return {};
        }
        points.push_back({ static_cast<float>(*x), static_cast<float>(*z) });
        return points;
    }

    // "x,z" or [x, z].
    [[nodiscard]] std::optional<std::pair<float, float>> Pair(std::string_view key) {
        const JsonValue* value = operation_.Find(key);
        if (value == nullptr || Failed()) return std::nullopt;
        std::optional<double> first;
        std::optional<double> second;
        if (value->GetKind() == JsonValue::Kind::String) {
            const std::string& text = value->AsString();
            const std::size_t comma = text.find(',');
            if (comma != std::string::npos) {
                first = ParseNumber(std::string_view{ text }.substr(0U, comma));
                second = ParseNumber(std::string_view{ text }.substr(comma + 1U));
            }
        } else if (value->GetKind() == JsonValue::Kind::Array && value->Size() == 2U &&
                   value->At(0U)->GetKind() == JsonValue::Kind::Number && value->At(1U)->GetKind() == JsonValue::Kind::Number) {
            first = value->At(0U)->AsNumber();
            second = value->At(1U)->AsNumber();
        }
        if (!first || !second || !std::isfinite(*first) || !std::isfinite(*second)) {
            Fail(key, "must be a point x,z or [x, z]");
            return std::nullopt;
        }
        return std::pair{ static_cast<float>(*first), static_cast<float>(*second) };
    }

    // A height, or "sample" for the height of the terrain under the stroke.
    struct HeightOrSample {
        std::optional<double> height;
        bool sample = false;
    };
    [[nodiscard]] HeightOrSample Height(std::string_view key) {
        const JsonValue* value = operation_.Find(key);
        if (value != nullptr && value->GetKind() == JsonValue::Kind::String && value->AsString() == "sample") return { std::nullopt, true };
        return { Number(key), false };
    }

    // Every member must be one of `allowed`.
    void OnlyKeys(std::span<const std::string_view> allowed) {
        if (Failed()) return;
        for (std::size_t index = 0U; index < operation_.Size(); ++index) {
            const std::string& name = operation_.MemberName(index);
            if (std::ranges::find(allowed, name) != allowed.end()) continue;
            error_ = "unknown member '" + name + "'; allowed:";
            for (const std::string_view key : allowed) error_ += ' ' + std::string{ key };
            return;
        }
    }

    void Fail(std::string_view key, std::string_view what) {
        if (!Failed()) error_ = '\'' + std::string{ key } + "' " + std::string{ what };
    }

private:
    [[nodiscard]] static std::optional<double> ParseNumber(std::string_view text) {
        double value = 0.0;
        const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (code != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    }

    const JsonValue& operation_;
    std::string& error_;
};

constexpr std::string_view kBrushKeys[]{ "op", "mode", "shape", "x", "z", "path", "radius", "strength", "falloff", "height", "step",
    "softness", "seed", "spacing", "invert", "symmetry", "noiseKind", "noiseScale", "noiseOctaves", "to", "toHeight", "image",
    "rotation", "blend" };
constexpr std::string_view kPaintKeys[]{ "op", "layer", "x", "z", "path", "radius", "opacity", "falloff", "shape", "erase", "seed", "auto" };
constexpr std::string_view kAutoPaintKeys[]{ "op", "layer", "slope", "height", "slopeBlend", "heightBlend", "opacity" };

// A stamp image by path: as written, or under the context's base directory when that is where it is.
[[nodiscard]] std::shared_ptr<const TerrainStampImage> LoadImage(TerrainOpContext& context, const std::string& text, std::string& error) {
    std::filesystem::path path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
    std::error_code code;
    if (path.is_relative() && !context.baseDirectory.empty() && std::filesystem::exists(context.baseDirectory / path, code)) {
        path = context.baseDirectory / path;
    }
    const std::string key = path.generic_string();
    if (const auto found = context.images.find(key); found != context.images.end()) return found->second;
    std::string reason;
    std::shared_ptr<const TerrainStampImage> image = TerrainHeightmapImporter::LoadStampImage(path, &reason);
    if (image == nullptr) {
        error = "'image' " + key + ": " + reason;
        return nullptr;
    }
    context.images.emplace(key, image);
    return image;
}

void ReadBrushSettings(Reader& reader, TerrainOpContext& context, TerrainBrushSettings& settings) {
    if (const std::optional<std::string> mode = reader.Text("mode")) {
        if (const std::optional<TerrainBrushMode> parsed = ParseTerrainBrushMode(*mode)) settings.mode = *parsed;
        else reader.Fail("mode", "is not a brush mode (raise lower smooth flatten noise terrace cut-hole fill-hole ramp stamp)");
    }
    if (const std::optional<std::string> shape = reader.Text("shape")) {
        if (const std::optional<TerrainBrushShape> parsed = ParseTerrainBrushShape(*shape)) settings.shape = *parsed;
        else reader.Fail("shape", "is not a brush shape (soft-round hard-round linear-round bell ring speckle)");
    }
    if (const auto value = reader.Number("radius")) settings.radius = static_cast<float>(*value);
    if (const auto value = reader.Number("strength")) settings.strength = static_cast<float>(*value);
    if (const auto value = reader.Number("falloff")) settings.falloff = static_cast<float>(*value);
    if (const auto value = reader.Number("step")) settings.terraceStep = static_cast<float>(*value);
    if (const auto value = reader.Number("softness")) settings.terraceSoftness = static_cast<float>(*value);
    if (const auto value = reader.Number("seed")) {
        if (*value < 0.0 || *value > 4294967295.0) reader.Fail("seed", "must be from 0 to 4294967295");
        else settings.noiseSeed = static_cast<std::uint32_t>(*value);
    }
    if (const std::optional<bool> invert = reader.Flag("invert")) settings.invert = *invert;
    if (const std::optional<std::string> symmetry = reader.Text("symmetry")) {
        if (*symmetry != "none" && *symmetry != "x" && *symmetry != "z" && *symmetry != "xz") {
            reader.Fail("symmetry", "is none, x, z or xz (the planes x=0 and z=0 the stroke is mirrored across)");
        }
        settings.symmetryX = *symmetry == "x" || *symmetry == "xz";
        settings.symmetryZ = *symmetry == "z" || *symmetry == "xz";
    }
    if (const std::optional<std::string> kind = reader.Text("noiseKind")) {
        if (const std::optional<TerrainNoiseKind> parsed = ParseTerrainNoiseKind(*kind)) settings.noiseKind = *parsed;
        else reader.Fail("noiseKind", "is gradient or value");
    }
    if (const auto value = reader.Number("noiseScale")) settings.noiseScale = static_cast<float>(*value);
    if (const auto value = reader.Number("noiseOctaves")) {
        if (*value < 1.0 || *value > kTerrainNoiseMaximumOctaves || *value != std::floor(*value)) reader.Fail("noiseOctaves", "must be a whole number from 1 to 8");
        else settings.noiseOctaves = static_cast<std::uint8_t>(*value);
    }
    if (const auto value = reader.Number("rotation")) settings.stampRotationDegrees = static_cast<float>(*value);
    if (const std::optional<std::string> blend = reader.Text("blend")) {
        if (const std::optional<TerrainStampBlend> parsed = ParseTerrainStampBlend(*blend)) settings.stampBlend = *parsed;
        else reader.Fail("blend", "is add, max or min");
    }
    // Heights: Flatten's target, a Stamp's base for max and min, a Ramp's heights at its two ends.
    const Reader::HeightOrSample height = reader.Height("height");
    const Reader::HeightOrSample toHeight = reader.Height("toHeight");
    if (height.height) settings.targetHeight = static_cast<float>(*height.height);
    if (toHeight.height) settings.rampEndHeight = static_cast<float>(*toHeight.height);
    if (settings.mode == TerrainBrushMode::Ramp) {
        if (const auto end = reader.Pair("to")) {
            settings.rampEndX = end->first;
            settings.rampEndZ = end->second;
        } else if (!reader.Failed()) {
            reader.Fail("to", "is required by a ramp: the point it ends at, x,z");
        }
        if (height.height.has_value() != toHeight.height.has_value() && !height.sample && !toHeight.sample) {
            reader.Fail("height", "and 'toHeight' go together: a ramp takes both heights, or neither (then it joins the terrain's own)");
        }
        settings.sampleTargetHeight = !height.height.has_value() && !toHeight.height.has_value();
        if (height.sample != toHeight.sample && (height.sample || toHeight.sample)) {
            reader.Fail("height", "'sample' goes with both heights of a ramp or neither");
        }
    } else {
        settings.sampleTargetHeight = height.sample;
    }
    if (settings.mode == TerrainBrushMode::Stamp) {
        const std::optional<std::string> image = reader.Text("image");
        if (!reader.Failed() && !image) reader.Fail("image", "is required by a stamp: a greyscale PNG or RAW height image");
        if (!reader.Failed()) {
            std::string imageError;
            settings.stampImage = LoadImage(context, *image, imageError);
            if (settings.stampImage == nullptr) reader.Fail("image", imageError);
        }
    }
}

[[nodiscard]] bool ApplyBrushOp(
    kb::assets::TerrainAsset& terrain, Reader& reader, TerrainOpContext& context, TerrainOpSummary& summary, std::string& error) {
    reader.OnlyKeys(kBrushKeys);
    TerrainBrushSettings settings{};
    ReadBrushSettings(reader, context, settings);
    const std::optional<double> spacing = reader.Number("spacing");
    const std::vector<Point> points = reader.Points();
    if (reader.Failed()) return false;
    if (!IsTerrainBrushSettingsValid(settings)) {
        error = "brush settings are out of range (radius 0..100000, strength >= 0, falloff 0..1, step > 0, softness 0..1, noiseScale 0.01..1000000)";
        return false;
    }
    if (spacing && (*spacing <= 0.0 || *spacing > 100000.0)) {
        error = "'spacing' must be greater than 0";
        return false;
    }
    if (!kb::assets::IsTerrainAssetValid(terrain, &error)) return false;
    BeginTerrainStroke(terrain, settings, TerrainBrushStamp{ .localX = points.front().x, .localZ = points.front().z });
    const float cell = std::min(terrain.worldSizeX / static_cast<float>(terrain.width - 1U),
        terrain.worldSizeZ / static_cast<float>(terrain.height - 1U));
    const float step = spacing ? static_cast<float>(*spacing) : std::max(settings.radius * 0.25F, cell);
    std::vector<float> scratch;
    const auto stamp = [&](Point at) {
        const TerrainBrushResult result = ApplyTerrainBrushToValidatedTerrain(
            terrain, settings, TerrainBrushStamp{ .localX = at.x, .localZ = at.z }, scratch);
        summary.changedSamples += result.changedSamples;
    };
    stamp(points.front());
    // Stamps every `step` along the path, so a stroke does not depend on how finely it was written;
    // the end of the path is stamped too unless a stamp already lies right at it.
    float untilNext = step;
    float sinceLast = 0.0F;
    for (std::size_t index = 1U; index < points.size(); ++index) {
        const Point from = points[index - 1U];
        const Point to = points[index];
        const float length = std::hypot(to.x - from.x, to.z - from.z);
        float travelled = untilNext;
        sinceLast += length;
        for (; travelled <= length; travelled += step) {
            const float t = travelled / length;
            stamp({ std::lerp(from.x, to.x, t), std::lerp(from.z, to.z, t) });
            sinceLast = length - travelled;
        }
        untilNext = travelled - length;
    }
    if (points.size() > 1U && sinceLast > step * 0.25F) stamp(points.back());
    return true;
}

void ReadLayerPaintSettings(Reader& reader, TerrainLayerPaintSettings& settings) {
    if (const std::optional<bool> automatic = reader.Flag("auto")) settings.autoLayer = *automatic;
    if (const auto layer = reader.Number("layer")) {
        if (*layer < 0.0 || *layer >= static_cast<double>(kb::assets::TerrainAsset::MaximumMaterialLayers) || *layer != std::floor(*layer)) {
            reader.Fail("layer", "must be a layer index from 0 to 3");
        } else {
            settings.layerIndex = static_cast<std::uint32_t>(*layer);
        }
    } else if (!reader.Failed() && !settings.autoLayer) {
        reader.Fail("layer", "is required: the index of the layer to paint (or \"auto\": true)");
    }
    if (const auto value = reader.Number("radius")) settings.radius = static_cast<float>(*value);
    if (const auto value = reader.Number("opacity")) settings.opacity = static_cast<float>(*value);
    if (const auto value = reader.Number("falloff")) settings.falloff = static_cast<float>(*value);
    if (const auto value = reader.Number("seed")) settings.noiseSeed = static_cast<std::uint32_t>(std::clamp(*value, 0.0, 4294967295.0));
    if (const std::optional<std::string> shape = reader.Text("shape")) {
        if (const std::optional<TerrainBrushShape> parsed = ParseTerrainBrushShape(*shape)) settings.shape = *parsed;
        else reader.Fail("shape", "is not a brush shape (soft-round hard-round linear-round bell ring speckle)");
    }
    if (const std::optional<bool> erase = reader.Flag("erase")) settings.erase = *erase;
    if (!reader.Failed() && (settings.opacity < 0.0F || settings.opacity > 1.0F)) reader.Fail("opacity", "must be from 0 to 1");
}

[[nodiscard]] bool ApplyPaintOp(kb::assets::TerrainAsset& terrain, Reader& reader, TerrainOpSummary& summary, std::string& error) {
    reader.OnlyKeys(kPaintKeys);
    TerrainLayerPaintSettings settings{};
    ReadLayerPaintSettings(reader, settings);
    const std::vector<Point> points = reader.Points();
    if (reader.Failed()) return false;
    if (settings.layerIndex >= terrain.materialLayers.size()) {
        error = "'layer' " + std::to_string(settings.layerIndex) + " is not a layer of the terrain (" + std::to_string(terrain.materialLayers.size()) + " layers)";
        return false;
    }
    if (settings.erase && terrain.materialLayers.size() < 2U) {
        error = "a terrain with one layer has nothing to erase to";
        return false;
    }
    for (std::size_t index = 0U; index < points.size(); ++index) {
        const Point from = points[index == 0U ? 0U : index - 1U];
        const Point to = points[index];
        const TerrainLayerPaintResult result = ApplyTerrainLayerPaintSegment(terrain, settings,
            TerrainBrushStamp{ .localX = from.x, .localZ = from.z }, TerrainBrushStamp{ .localX = to.x, .localZ = to.z });
        summary.changedTexels += result.changedTexels;
    }
    return true;
}

[[nodiscard]] bool ApplyAutoPaintOp(kb::assets::TerrainAsset& terrain, Reader& reader, TerrainOpSummary& summary, std::string& error) {
    reader.OnlyKeys(kAutoPaintKeys);
    TerrainAutoPaintSettings settings{};
    if (const auto layer = reader.Number("layer")) {
        if (*layer < 0.0 || *layer >= static_cast<double>(terrain.materialLayers.size()) || *layer != std::floor(*layer)) {
            error = "'layer' must be the index of a layer of the terrain (" + std::to_string(terrain.materialLayers.size()) + " layers)";
            return false;
        }
        settings.layerIndex = static_cast<std::uint32_t>(*layer);
    } else if (!reader.Failed()) {
        error = "auto-paint needs 'layer'";
        return false;
    }
    if (const auto range = reader.Range("slope")) {
        settings.minSlopeDegrees = range->first;
        settings.maxSlopeDegrees = range->second;
    }
    if (const auto range = reader.Range("height")) {
        settings.minHeight = range->first;
        settings.maxHeight = range->second;
    }
    if (const auto value = reader.Number("slopeBlend")) settings.slopeBlendDegrees = static_cast<float>(*value);
    if (const auto value = reader.Number("heightBlend")) settings.heightBlend = static_cast<float>(*value);
    if (const auto value = reader.Number("opacity")) settings.opacity = static_cast<float>(*value);
    if (reader.Failed()) return false;
    if (settings.slopeBlendDegrees < 0.0F || settings.heightBlend < 0.0F || settings.opacity > 1.0F || settings.opacity < 0.0F) {
        error = "blend widths must be >= 0 and opacity from 0 to 1";
        return false;
    }
    summary.changedTexels += ApplyTerrainAutoPaint(terrain, settings).changedTexels;
    return true;
}

} // namespace

bool ParseTerrainBrushSettings(const JsonValue& operation, TerrainOpContext& context, TerrainBrushSettings& settings, std::string& error) {
    Reader reader{ operation, error };
    ReadBrushSettings(reader, context, settings);
    return !reader.Failed();
}

bool ParseTerrainLayerPaintSettings(const JsonValue& operation, TerrainLayerPaintSettings& settings, std::string& error) {
    Reader reader{ operation, error };
    ReadLayerPaintSettings(reader, settings);
    return !reader.Failed();
}

bool ApplyTerrainOp(
    kb::assets::TerrainAsset& terrain, const JsonValue& operation, TerrainOpContext& context, TerrainOpSummary& summary, std::string& error) {
    error.clear();
    if (operation.GetKind() != JsonValue::Kind::Object) {
        error = "an operation must be an object with an 'op' member";
        return false;
    }
    Reader reader{ operation, error };
    const std::optional<std::string> kind = reader.Text("op");
    if (reader.Failed()) return false;
    if (!kind) {
        error = "an operation needs an 'op' member (brush, paint or auto-paint)";
        return false;
    }
    bool applied = false;
    if (*kind == "brush") applied = ApplyBrushOp(terrain, reader, context, summary, error);
    else if (*kind == "paint") applied = ApplyPaintOp(terrain, reader, summary, error);
    else if (*kind == "auto-paint") applied = ApplyAutoPaintOp(terrain, reader, summary, error);
    else error = "unknown op '" + *kind + "' (brush, paint or auto-paint)";
    if (applied) ++summary.operations;
    return applied;
}

bool ApplyTerrainOps(
    kb::assets::TerrainAsset& terrain, const JsonValue& operations, TerrainOpContext& context, TerrainOpSummary& summary, std::string& error) {
    const JsonValue* list = &operations;
    if (operations.GetKind() == JsonValue::Kind::Object) {
        list = operations.Find("ops");
        if (list == nullptr) {
            error = "an operations object needs an 'ops' array";
            return false;
        }
    }
    if (list->GetKind() != JsonValue::Kind::Array) {
        error = "operations must be a JSON array (or an object with an 'ops' array)";
        return false;
    }
    for (std::size_t index = 0U; index < list->Size(); ++index) {
        if (!ApplyTerrainOp(terrain, *list->At(index), context, summary, error)) {
            error = "op " + std::to_string(index) + ": " + error;
            return false;
        }
    }
    return true;
}

} // namespace kb::terrain_editor
