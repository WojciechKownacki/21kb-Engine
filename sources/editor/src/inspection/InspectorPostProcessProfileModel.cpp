#include "inspection/InspectorPostProcessProfileModel.hpp"

#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "localization/EditorLocalization.hpp"

#include <array>
#include <sstream>
#include <utility>

namespace kb::editor {
namespace {

using Kind = InspectorPostProcessProfileRowKind;
constexpr std::string_view kAntiAliasingField = "antiAliasing";

constexpr std::array<InspectorPostProcessProfileSection, 6> kSections{ {
    { InspectorSectionId::PostProcessTonemap },
    { InspectorSectionId::PostProcessExposure },
    { InspectorSectionId::PostProcessBloom },
    { InspectorSectionId::PostProcessAntiAliasing },
    { InspectorSectionId::PostProcessVignette },
    { InspectorSectionId::PostProcessAmbientOcclusion },
} };

constexpr std::array<InspectorPostProcessProfileRow, 20> kRows{ {
    { InspectorSectionId::PostProcessTonemap, Kind::Bool, "tonemapEnabled" },
    { InspectorSectionId::PostProcessTonemap, Kind::Choice, "outputTransform.tonemap" },
    { InspectorSectionId::PostProcessExposure, Kind::Number, "outputTransform.exposureStops" },
    { InspectorSectionId::PostProcessExposure, Kind::Bool, "outputTransform.autoExposure.enabled" },
    { InspectorSectionId::PostProcessExposure, Kind::Choice, "autoExposureMetering" },
    { InspectorSectionId::PostProcessExposure, Kind::Number, "outputTransform.autoExposure.minExposureStops" },
    { InspectorSectionId::PostProcessExposure, Kind::Number, "outputTransform.autoExposure.maxExposureStops" },
    { InspectorSectionId::PostProcessExposure, Kind::Number, "outputTransform.autoExposure.biasStops" },
    { InspectorSectionId::PostProcessExposure, Kind::Bool, "outputTransform.autoExposure.temporalAdaptationEnabled" },
    { InspectorSectionId::PostProcessBloom, Kind::Bool, "bloomEnabled" },
    { InspectorSectionId::PostProcessBloom, Kind::Number, "bloomStrength" },
    { InspectorSectionId::PostProcessBloom, Kind::Number, "bloomThreshold" },
    { InspectorSectionId::PostProcessAntiAliasing, Kind::Choice, kAntiAliasingField },
    { InspectorSectionId::PostProcessVignette, Kind::Bool, "vignetteEnabled" },
    { InspectorSectionId::PostProcessVignette, Kind::Number, "vignetteIntensity" },
    { InspectorSectionId::PostProcessAmbientOcclusion, Kind::Bool, "ambientOcclusionEnabled" },
    { InspectorSectionId::PostProcessAmbientOcclusion, Kind::Number, "ambientOcclusionIntensity" },
    { InspectorSectionId::PostProcessAmbientOcclusion, Kind::Number, "ambientOcclusionRadius" },
    { InspectorSectionId::PostProcessExposure, Kind::Number, "outputTransform.gamma" },
    { InspectorSectionId::PostProcessBloom, Kind::Number, "bloomSoftKnee" },
} };

// Every choice row's values, in the order a click steps through them: the file's value name and
// the name the Inspector shows.
struct Choice {
    std::string_view value;
};

constexpr std::array<Choice, 3> kToneCurves{ { { "Aces" }, { "AgxApprox" }, { "None" } } };
constexpr std::array<Choice, 3> kMetering{ { { "HdrColor" }, { "SceneLighting" }, { "Manual" } } };
constexpr std::array<Choice, 3> kAntiAliasing{ { { "Taa" }, { "Fxaa" }, { "Off" } } };

[[nodiscard]] std::string_view ChoiceLabel(std::string_view value) {
    if (value == "Aces") return "ACES";
    if (value == "AgxApprox") return "AgX";
    if (value == "None") return EditorText("inspector.postProcess.none", "None");
    if (value == "HdrColor") return EditorText("inspector.postProcess.hdrColor", "HDR Color");
    if (value == "SceneLighting") return EditorText("inspector.postProcess.sceneLighting", "Scene Lighting");
    if (value == "Manual") return EditorText("inspector.postProcess.manual", "Manual");
    if (value == "Taa") return "TAA";
    if (value == "Fxaa") return "FXAA";
    if (value == "Off") return EditorText("inspector.postProcess.off", "Off");
    return value;
}

[[nodiscard]] std::span<const Choice> ChoicesFor(std::string_view field) noexcept {
    if (field == "outputTransform.tonemap") return kToneCurves;
    if (field == "autoExposureMetering") return kMetering;
    return kAntiAliasing;
}

// The value of every field as the profile file writes it.
[[nodiscard]] std::string FieldValue(const kb::render::ScenePostProcessSettings& settings, std::string_view field) {
    if (field == kAntiAliasingField) {
        return settings.temporalAntiAliasingEnabled ? "Taa" : settings.fxaaEnabled ? "Fxaa" : "Off";
    }
    std::istringstream lines{ kb::render::PostProcessProfileAssetLoader::Describe(settings) };
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t space = line.find(' ');
        if (space != std::string::npos && std::string_view{ line }.substr(0U, space) == field) {
            return line.substr(space + 1U);
        }
    }
    return {};
}

// Writes one field through the profile's own parser; the anti-aliasing choice sets TAA (with its
// jitter) and FXAA together, as the renderer takes them.
[[nodiscard]] std::optional<kb::render::ScenePostProcessSettings> WithField(
    const kb::render::ScenePostProcessSettings& settings, std::string_view field, std::string_view value) {
    kb::render::ScenePostProcessSettings changed = settings;
    if (field == kAntiAliasingField) {
        const bool taa = value == "Taa";
        const bool fxaa = value == "Fxaa";
        changed.temporalAntiAliasingEnabled = taa;
        changed.temporalJitterEnabled = taa;
        changed.fxaaEnabled = fxaa;
        return changed;
    }
    if (!kb::render::PostProcessProfileAssetLoader::SetField(changed, field, value)) {
        return std::nullopt;
    }
    return changed;
}

[[nodiscard]] bool InRange(int row) noexcept {
    return row >= 0 && static_cast<std::size_t>(row) < kRows.size();
}

} // namespace

std::span<const InspectorPostProcessProfileSection> InspectorPostProcessProfileModel::Sections() noexcept {
    return kSections;
}

std::span<const InspectorPostProcessProfileRow> InspectorPostProcessProfileModel::Rows() noexcept {
    return kRows;
}

bool InspectorPostProcessProfileModel::IsSection(InspectorSectionId section) noexcept {
    for (const InspectorPostProcessProfileSection& entry : kSections) {
        if (entry.section == section) return true;
    }
    return false;
}

int InspectorPostProcessProfileModel::RowCount(InspectorSectionId section) noexcept {
    int count = 0;
    for (const InspectorPostProcessProfileRow& row : kRows) {
        if (row.section == section) ++count;
    }
    return count;
}

std::string InspectorPostProcessProfileModel::ValueText(const kb::render::ScenePostProcessSettings& settings, int row) {
    if (!InRange(row)) return {};
    const InspectorPostProcessProfileRow& entry = kRows[static_cast<std::size_t>(row)];
    const std::string value = FieldValue(settings, entry.field);
    if (entry.kind == Kind::Choice) {
        for (const Choice& choice : ChoicesFor(entry.field)) {
            if (choice.value == value) {
                return std::string{ ChoiceLabel(choice.value) };
            }
        }
    }
    return value;
}

bool InspectorPostProcessProfileModel::BoolValue(const kb::render::ScenePostProcessSettings& settings, int row) {
    return InRange(row) && FieldValue(settings, kRows[static_cast<std::size_t>(row)].field) == "1";
}

std::optional<kb::render::ScenePostProcessSettings> InspectorPostProcessProfileModel::Clicked(
    const kb::render::ScenePostProcessSettings& settings, int row) {
    if (!InRange(row)) return std::nullopt;
    const InspectorPostProcessProfileRow& entry = kRows[static_cast<std::size_t>(row)];
    if (entry.kind == Kind::Bool) {
        return WithField(settings, entry.field, BoolValue(settings, row) ? "0" : "1");
    }
    if (entry.kind == Kind::Choice) {
        const std::span<const Choice> choices = ChoicesFor(entry.field);
        const std::string current = FieldValue(settings, entry.field);
        std::size_t next = 0U;
        for (std::size_t index = 0U; index < choices.size(); ++index) {
            if (choices[index].value == current) {
                next = (index + 1U) % choices.size();
                break;
            }
        }
        return WithField(settings, entry.field, choices[next].value);
    }
    return std::nullopt;
}

std::optional<kb::render::ScenePostProcessSettings> InspectorPostProcessProfileModel::Edited(
    const kb::render::ScenePostProcessSettings& settings, int row, std::string_view text) {
    if (!InRange(row) || kRows[static_cast<std::size_t>(row)].kind != Kind::Number) return std::nullopt;
    return WithField(settings, kRows[static_cast<std::size_t>(row)].field, text);
}

std::string_view InspectorPostProcessProfileModel::SectionTitle(InspectorSectionId section) {
    switch (section) {
    case InspectorSectionId::PostProcessTonemap: return EditorText("inspector.postProcess.tonemapping", "Tonemapping");
    case InspectorSectionId::PostProcessExposure: return EditorText("inspector.postProcess.exposure", "Exposure");
    case InspectorSectionId::PostProcessBloom: return EditorText("inspector.postProcess.bloom", "Bloom");
    case InspectorSectionId::PostProcessAntiAliasing: return EditorText("inspector.postProcess.antiAliasing", "Anti-aliasing");
    case InspectorSectionId::PostProcessVignette: return EditorText("inspector.postProcess.vignette", "Vignette");
    case InspectorSectionId::PostProcessAmbientOcclusion: return EditorText("inspector.postProcess.ambientOcclusion", "Ambient Occlusion");
    default: return {};
    }
}

std::string_view InspectorPostProcessProfileModel::RowLabel(int row) {
    switch (row) {
    case 0: return EditorText("inspector.postProcess.tonemapEnabled", "Tonemapping");
    case 1: return EditorText("inspector.postProcess.toneCurve", "Tone Curve");
    case 2: return EditorText("inspector.postProcess.exposureStops", "Exposure (EV)");
    case 3: return EditorText("inspector.postProcess.autoExposure", "Auto Exposure");
    case 4: return EditorText("inspector.postProcess.metering", "Metering");
    case 5: return EditorText("inspector.postProcess.minExposure", "Minimum EV");
    case 6: return EditorText("inspector.postProcess.maxExposure", "Maximum EV");
    case 7: return EditorText("inspector.postProcess.compensation", "Compensation (EV)");
    case 8: return EditorText("inspector.postProcess.adaptation", "Adaptation");
    case 9: return EditorText("inspector.postProcess.bloomEnabled", "Bloom");
    case 10: return EditorText("inspector.postProcess.bloomIntensity", "Intensity");
    case 11: return EditorText("inspector.postProcess.bloomThreshold", "Threshold");
    case 12: return EditorText("inspector.postProcess.antiAliasingMode", "Mode");
    case 13: return EditorText("inspector.postProcess.vignetteEnabled", "Vignette");
    case 14: return EditorText("inspector.postProcess.vignetteIntensity", "Intensity");
    case 15: return EditorText("inspector.postProcess.ambientOcclusionEnabled", "Ambient Occlusion");
    case 16: return EditorText("inspector.postProcess.ambientOcclusionIntensity", "Intensity");
    case 17: return EditorText("inspector.postProcess.ambientOcclusionRadius", "Radius (m)");
    case 18: return EditorText("inspector.postProcess.gamma", "Display Gamma");
    case 19: return EditorText("inspector.postProcess.bloomSoftKnee", "Soft Knee");
    default: return {};
    }
}

std::string InspectorPostProcessProfileModel::Text(const kb::render::ScenePostProcessSettings& settings) {
    std::ostringstream text;
    text << "Post-Process Profile\n";
    for (const InspectorPostProcessProfileSection& section : kSections) {
        text << SectionTitle(section.section) << '\n';
        for (std::size_t index = 0U; index < kRows.size(); ++index) {
            const InspectorPostProcessProfileRow& row = kRows[index];
            if (row.section != section.section) continue;
            text << RowLabel(static_cast<int>(index)) << ": ";
            if (row.kind == Kind::Bool) {
                text << (BoolValue(settings, static_cast<int>(index)) ? "true" : "false");
            } else {
                text << ValueText(settings, static_cast<int>(index));
            }
            text << '\n';
        }
    }
    return text.str();
}

} // namespace kb::editor
