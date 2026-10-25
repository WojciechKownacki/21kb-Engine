#include "engine/audio/AudioMixerEffectEditing.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace kb::audio {
namespace {
using DoubleField = std::pair<std::string_view, double AudioDspParameters::*>;
using FloatField = std::pair<std::string_view, float AudioDspParameters::*>;
constexpr std::array<DoubleField, 10U> kDoubleFields{{
    {"cutoffHz", &AudioDspParameters::cutoffHz}, {"gainDb", &AudioDspParameters::gainDb},
    {"quality", &AudioDspParameters::quality}, {"delayMs", &AudioDspParameters::delayMs},
    {"thresholdDb", &AudioDspParameters::thresholdDb}, {"ratio", &AudioDspParameters::ratio},
    {"attackMs", &AudioDspParameters::attackMs}, {"releaseMs", &AudioDspParameters::releaseMs},
    {"makeupDb", &AudioDspParameters::makeupDb}, {"ceilingDb", &AudioDspParameters::ceilingDb}}};
constexpr std::array<FloatField, 6U> kFloatFields{{
    {"feedback", &AudioDspParameters::feedback}, {"wet", &AudioDspParameters::wet}, {"dry", &AudioDspParameters::dry},
    {"roomSize", &AudioDspParameters::roomSize}, {"damping", &AudioDspParameters::damping}, {"width", &AudioDspParameters::width}}};

AudioMixerBus* FindBus(AudioMixerAsset& asset, std::string_view name) {
    const auto found = std::find_if(asset.buses.begin(), asset.buses.end(), [&](const auto& bus) { return bus.name == name; });
    return found == asset.buses.end() ? nullptr : &*found;
}
} // namespace

std::span<const std::string_view> AudioDspFieldNames(AudioDspKind kind) noexcept {
    static constexpr std::array<std::string_view, 3U> filter{"bypass", "cutoffHz", "order"};
    static constexpr std::array<std::string_view, 4U> equalizer{"bypass", "cutoffHz", "gainDb", "quality"};
    static constexpr std::array<std::string_view, 5U> delay{"bypass", "delayMs", "feedback", "wet", "dry"};
    static constexpr std::array<std::string_view, 6U> reverb{"bypass", "roomSize", "damping", "width", "wet", "dry"};
    static constexpr std::array<std::string_view, 6U> compressor{"bypass", "thresholdDb", "ratio", "attackMs", "releaseMs", "makeupDb"};
    static constexpr std::array<std::string_view, 3U> limiter{"bypass", "ceilingDb", "releaseMs"};
    switch (kind) {
    case AudioDspKind::LowPass:
    case AudioDspKind::HighPass: return filter;
    case AudioDspKind::PeakingEqualizer: return equalizer;
    case AudioDspKind::Delay: return delay;
    case AudioDspKind::Reverb: return reverb;
    case AudioDspKind::Compressor: return compressor;
    case AudioDspKind::Limiter: return limiter;
    }
    return {};
}

std::string AudioDspFieldValue(const AudioDspParameters& p, std::string_view field) {
    const auto fields = AudioDspFieldNames(p.kind);
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) return {};
    if (field == "bypass") return p.bypass ? "true" : "false";
    if (field == "order") return std::to_string(p.order);
    std::ostringstream text; text.imbue(std::locale::classic());
    for (const auto& [name, member] : kDoubleFields) if (name == field) text << std::setprecision(std::numeric_limits<double>::max_digits10) << p.*member;
    for (const auto& [name, member] : kFloatFields) if (name == field) text << std::setprecision(std::numeric_limits<float>::max_digits10) << p.*member;
    return text.str();
}

std::string SetAudioDspField(AudioDspParameters& p, std::string_view field, std::string_view value) {
    const auto fields = AudioDspFieldNames(p.kind);
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) return "DSP parameter does not apply to this effect: " + std::string{field};
    auto candidate = p;
    if (field == "bypass") {
        if (value != "true" && value != "false" && value != "0" && value != "1") return "DSP bypass requires true, false, 0 or 1";
        candidate.bypass = value == "true" || value == "1";
    } else if (field == "order") {
        std::uint64_t order = 0U;
        if (!kb::library::TryParseUInt64(value, order) || order > 8U) return "DSP filter order must be between 1 and 8";
        candidate.order = static_cast<std::uint32_t>(order);
    } else {
        double number = 0.0;
        if (!kb::library::TryParseDouble(value, number) || !std::isfinite(number)) return "DSP parameter must be a finite number";
        for (const auto& [name, member] : kDoubleFields) if (name == field) candidate.*member = number;
        for (const auto& [name, member] : kFloatFields) {
            if (name != field) continue;
            if (std::abs(number) > std::numeric_limits<float>::max()) return "DSP parameter exceeds the finite float range";
            candidate.*member = static_cast<float>(number);
        }
    }
    if (const auto error = AudioDspParameterError(candidate); !error.empty()) return std::string{error};
    p = candidate;
    return {};
}

std::string AddAudioMixerEffect(AudioMixerAsset& asset, std::string_view busName, std::string_view id, AudioDspKind kind) {
    if (const auto error = ValidateAudioMixerAsset(asset); !error.empty()) return error;
    auto* bus = FindBus(asset, busName);
    if (bus == nullptr) return "audio bus does not exist: " + std::string{busName};
    if (!IsAudioMixerNameTokenValid(id)) return "DSP effect ID must be a non-empty name token";
    if (bus->effects.size() >= 16U) return "audio bus already has 16 DSP effects";
    if (std::any_of(bus->effects.begin(), bus->effects.end(), [&](const auto& effect) { return effect.id == id; })) return "DSP effect ID already exists: " + std::string{id};
    AudioDspEffect effect{.id = std::string{id}, .parameters = {.kind = kind}};
    if (const auto error = AudioDspParameterError(effect.parameters); !error.empty()) return std::string{error};
    bus->effects.push_back(std::move(effect));
    return {};
}

std::string RemoveAudioMixerEffect(AudioMixerAsset& asset, std::string_view busName, std::string_view id) {
    if (const auto error = ValidateAudioMixerAsset(asset); !error.empty()) return error;
    auto* bus = FindBus(asset, busName);
    if (bus == nullptr) return "audio bus does not exist: " + std::string{busName};
    if (std::erase_if(bus->effects, [&](const auto& effect) { return effect.id == id; }) == 0U) return "DSP effect does not exist: " + std::string{id};
    return {};
}

std::string MoveAudioMixerEffect(AudioMixerAsset& asset, std::string_view busName, std::string_view id, std::size_t index) {
    if (const auto error = ValidateAudioMixerAsset(asset); !error.empty()) return error;
    auto* bus = FindBus(asset, busName);
    if (bus == nullptr) return "audio bus does not exist: " + std::string{busName};
    if (index >= bus->effects.size()) return "DSP effect destination index is outside this chain";
    auto candidate = bus->effects;
    const auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& effect) { return effect.id == id; });
    if (found == candidate.end()) return "DSP effect does not exist: " + std::string{id};
    auto effect = std::move(*found); candidate.erase(found);
    candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(index), std::move(effect));
    bus->effects.swap(candidate);
    return {};
}

std::string SetAudioMixerEffectField(AudioMixerAsset& asset, std::string_view busName, std::string_view id,
    std::string_view field, std::string_view value) {
    if (const auto error = ValidateAudioMixerAsset(asset); !error.empty()) return error;
    auto* bus = FindBus(asset, busName);
    if (bus == nullptr) return "audio bus does not exist: " + std::string{busName};
    const auto found = std::find_if(bus->effects.begin(), bus->effects.end(), [&](const auto& effect) { return effect.id == id; });
    if (found == bus->effects.end()) return "DSP effect does not exist: " + std::string{id};
    return SetAudioDspField(found->parameters, field, value);
}

} // namespace kb::audio
