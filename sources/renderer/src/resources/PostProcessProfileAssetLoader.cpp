#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"

#include "RenderMaterialAtomicFileWriter.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace kb::render {
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

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseBoolToken(std::string_view text, bool& output) noexcept {
    text = Trim(text);
    if (text == "true" || text == "1") {
        output = true;
        return true;
    }
    if (text == "false" || text == "0") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseAutoExposureMeteringMode(std::string_view text, ScenePostProcessSettings::AutoExposureMeteringMode& output) noexcept {
    if (text == "HdrColor") output = ScenePostProcessSettings::AutoExposureMeteringMode::HdrColor;
    else if (text == "SceneLighting") output = ScenePostProcessSettings::AutoExposureMeteringMode::SceneLighting;
    else if (text == "Manual") output = ScenePostProcessSettings::AutoExposureMeteringMode::Manual;
    else return false;
    return true;
}

[[nodiscard]] std::string_view AutoExposureMeteringModeName(ScenePostProcessSettings::AutoExposureMeteringMode mode) noexcept {
    switch (mode) {
    case ScenePostProcessSettings::AutoExposureMeteringMode::HdrColor:
        return "HdrColor";
    case ScenePostProcessSettings::AutoExposureMeteringMode::SceneLighting:
        return "SceneLighting";
    case ScenePostProcessSettings::AutoExposureMeteringMode::Manual:
        return "Manual";
    }
    return "HdrColor";
}

[[nodiscard]] bool ParseTonemapOperator(std::string_view text, FullscreenTextureTonemapOperator& output) noexcept {
    if (text == "None") output = FullscreenTextureTonemapOperator::None;
    else if (text == "Aces") output = FullscreenTextureTonemapOperator::Aces;
    else if (text == "AgxApprox") output = FullscreenTextureTonemapOperator::AgxApprox;
    else return false;
    return true;
}

[[nodiscard]] std::string_view TonemapOperatorName(FullscreenTextureTonemapOperator tonemap) noexcept {
    switch (tonemap) {
    case FullscreenTextureTonemapOperator::None:
        return "None";
    case FullscreenTextureTonemapOperator::Aces:
        return "Aces";
    case FullscreenTextureTonemapOperator::AgxApprox:
        return "AgxApprox";
    }
    return "None";
}

// LIB-142: a flat `key value\n` text format, mirroring RenderMaterialAssetParser/Writer's own
// established convention for simple (non-graph) render assets - every field of
// ScenePostProcessSettings (including the nested outputTransform/autoExposure structs,
// flattened with dotted keys) gets exactly one line, unknown keys are ignored (forward
// compatible with future fields), and a missing key simply leaves the struct's own default
// member initializer in place (so a hand-trimmed profile file omitting most keys is valid).
[[nodiscard]] bool ApplyField(std::string_view keyword, std::string_view rest, ScenePostProcessSettings& settings) {
    if (keyword == "bloomEnabled") return ParseBoolToken(rest, settings.bloomEnabled);
    if (keyword == "bloomStrength") return ParseFiniteMaterialFloatToken(Trim(rest), settings.bloomStrength);
    if (keyword == "bloomThreshold") return ParseFiniteMaterialFloatToken(Trim(rest), settings.bloomThreshold);
    if (keyword == "bloomSoftKnee") return ParseFiniteMaterialFloatToken(Trim(rest), settings.bloomSoftKnee);
    if (keyword == "bloomRadiusPixels") return ParseFiniteMaterialFloatToken(Trim(rest), settings.bloomRadiusPixels);
    if (keyword == "temporalAntiAliasingEnabled") return ParseBoolToken(rest, settings.temporalAntiAliasingEnabled);
    if (keyword == "temporalJitterEnabled") return ParseBoolToken(rest, settings.temporalJitterEnabled);
    if (keyword == "temporalHistoryBlend") return ParseFiniteMaterialFloatToken(Trim(rest), settings.temporalHistoryBlend);
    if (keyword == "fxaaEnabled") return ParseBoolToken(rest, settings.fxaaEnabled);
    if (keyword == "tonemapEnabled") return ParseBoolToken(rest, settings.tonemapEnabled);
    if (keyword == "autoExposureMetering") return ParseAutoExposureMeteringMode(Trim(rest), settings.autoExposureMetering);
    if (keyword == "outputTransform.exposureStops") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.exposureStops);
    if (keyword == "outputTransform.gamma") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.gamma);
    if (keyword == "outputTransform.tonemap") return ParseTonemapOperator(Trim(rest), settings.outputTransform.tonemap);
    if (keyword == "outputTransform.colorGradingLutStrength") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.colorGradingLutStrength);
    if (keyword == "outputTransform.autoExposure.enabled") return ParseBoolToken(rest, settings.outputTransform.autoExposure.enabled);
    if (keyword == "outputTransform.autoExposure.meteredAverageLuminance") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.meteredAverageLuminance);
    if (keyword == "outputTransform.autoExposure.middleGray") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.middleGray);
    if (keyword == "outputTransform.autoExposure.minExposureStops") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.minExposureStops);
    if (keyword == "outputTransform.autoExposure.maxExposureStops") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.maxExposureStops);
    if (keyword == "outputTransform.autoExposure.biasStops") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.biasStops);
    if (keyword == "outputTransform.autoExposure.temporalAdaptationEnabled") return ParseBoolToken(rest, settings.outputTransform.autoExposure.temporalAdaptationEnabled);
    if (keyword == "outputTransform.autoExposure.brightAdaptationRate") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.brightAdaptationRate);
    if (keyword == "outputTransform.autoExposure.darkAdaptationRate") return ParseFiniteMaterialFloatToken(Trim(rest), settings.outputTransform.autoExposure.darkAdaptationRate);
    return true; // unknown keyword - forward compatible, ignored rather than a hard parse failure.
}

[[nodiscard]] std::optional<ScenePostProcessSettings> ParseProfile(std::istream& input) {
    ScenePostProcessSettings settings{};
    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string_view content = Trim(StripComment(line));
        if (content.empty()) {
            continue;
        }
        const std::size_t separator = content.find(' ');
        const std::string_view keyword = separator == std::string_view::npos ? content : content.substr(0U, separator);
        const std::string_view rest = separator == std::string_view::npos ? std::string_view{} : Trim(content.substr(separator + 1U));
        if (!ApplyField(keyword, rest, settings)) {
            return std::nullopt;
        }
    }
    return settings;
}

void WriteProfile(std::ostream& output, const ScenePostProcessSettings& settings) {
    output << "bloomEnabled " << (settings.bloomEnabled ? 1 : 0) << '\n';
    output << "bloomStrength " << settings.bloomStrength << '\n';
    output << "bloomThreshold " << settings.bloomThreshold << '\n';
    output << "bloomSoftKnee " << settings.bloomSoftKnee << '\n';
    output << "bloomRadiusPixels " << settings.bloomRadiusPixels << '\n';
    output << "temporalAntiAliasingEnabled " << (settings.temporalAntiAliasingEnabled ? 1 : 0) << '\n';
    output << "temporalJitterEnabled " << (settings.temporalJitterEnabled ? 1 : 0) << '\n';
    output << "temporalHistoryBlend " << settings.temporalHistoryBlend << '\n';
    output << "fxaaEnabled " << (settings.fxaaEnabled ? 1 : 0) << '\n';
    output << "tonemapEnabled " << (settings.tonemapEnabled ? 1 : 0) << '\n';
    output << "autoExposureMetering " << AutoExposureMeteringModeName(settings.autoExposureMetering) << '\n';
    output << "outputTransform.exposureStops " << settings.outputTransform.exposureStops << '\n';
    output << "outputTransform.gamma " << settings.outputTransform.gamma << '\n';
    output << "outputTransform.tonemap " << TonemapOperatorName(settings.outputTransform.tonemap) << '\n';
    output << "outputTransform.colorGradingLutStrength " << settings.outputTransform.colorGradingLutStrength << '\n';
    output << "outputTransform.autoExposure.enabled " << (settings.outputTransform.autoExposure.enabled ? 1 : 0) << '\n';
    output << "outputTransform.autoExposure.meteredAverageLuminance " << settings.outputTransform.autoExposure.meteredAverageLuminance << '\n';
    output << "outputTransform.autoExposure.middleGray " << settings.outputTransform.autoExposure.middleGray << '\n';
    output << "outputTransform.autoExposure.minExposureStops " << settings.outputTransform.autoExposure.minExposureStops << '\n';
    output << "outputTransform.autoExposure.maxExposureStops " << settings.outputTransform.autoExposure.maxExposureStops << '\n';
    output << "outputTransform.autoExposure.biasStops " << settings.outputTransform.autoExposure.biasStops << '\n';
    output << "outputTransform.autoExposure.temporalAdaptationEnabled " << (settings.outputTransform.autoExposure.temporalAdaptationEnabled ? 1 : 0) << '\n';
    output << "outputTransform.autoExposure.brightAdaptationRate " << settings.outputTransform.autoExposure.brightAdaptationRate << '\n';
    output << "outputTransform.autoExposure.darkAdaptationRate " << settings.outputTransform.autoExposure.darkAdaptationRate << '\n';
}

} // namespace

std::string_view PostProcessProfileAssetLoader::Type() const noexcept {
    return kPostProcessProfileAssetType;
}

std::type_index PostProcessProfileAssetLoader::PayloadType() const noexcept {
    return typeid(ScenePostProcessSettings);
}

std::vector<std::string> PostProcessProfileAssetLoader::Extensions() const {
    return { kPostProcessProfileAssetExtension };
}

kb::assets::AssetLoadResult PostProcessProfileAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    const std::optional<ScenePostProcessSettings> settings = LoadProfile(request.resolvedPath);
    if (!settings.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Post-process profile asset could not be loaded or parsed." };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<ScenePostProcessSettings>(*settings),
        .error = {},
    };
}

std::optional<ScenePostProcessSettings> PostProcessProfileAssetLoader::LoadProfile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return std::nullopt;
    }
    return ParseProfile(input);
}

bool PostProcessProfileAssetLoader::SaveProfile(const std::filesystem::path& path, const ScenePostProcessSettings& settings) {
    return detail::WriteMaterialFileAtomically(path, [&settings](std::ostream& output) {
        WriteProfile(output, settings);
    });
}

} // namespace kb::render
