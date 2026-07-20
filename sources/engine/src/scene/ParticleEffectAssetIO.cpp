#include "engine/scene/ParticleEffectAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

namespace kb::scene {
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

[[nodiscard]] bool ParseFloatToken(std::string_view text, float& output) noexcept {
    text = Trim(text);
    // Apple's libc++ has no floating-point std::from_chars; TryParseDouble is the codebase's
    // locale-independent replacement.
    double parsed = 0.0;
    if (!kb::library::TryParseDouble(text, parsed) || !std::isfinite(parsed)) {
        return false;
    }
    const double magnitude = parsed < 0.0 ? -parsed : parsed;
    if (magnitude > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    output = static_cast<float>(parsed);
    return true;
}

[[nodiscard]] bool ParseUInt32Token(std::string_view text, std::uint32_t& output) noexcept {
    text = Trim(text);
    std::uint32_t parsed = 0U;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    output = parsed;
    return true;
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

[[nodiscard]] bool ParseEasingToken(std::string_view text, kb::math::Easing& output) noexcept {
    text = Trim(text);
    for (std::uint32_t value = 0U; value <= static_cast<std::uint32_t>(kb::math::Easing::InOutBounce); ++value) {
        const auto candidate = static_cast<kb::math::Easing>(value);
        if (kb::math::ToString(candidate) == text) {
            output = candidate;
            return true;
        }
    }
    return false;
}

// Splits `content` on whitespace into up to `maxTokens` tokens (the repeatable
// sizeCurveKeyframe/colorGradientStop lines carry several numeric fields per line).
[[nodiscard]] std::vector<std::string_view> Tokenize(std::string_view content) {
    std::vector<std::string_view> tokens;
    std::size_t index = 0U;
    while (index < content.size()) {
        while (index < content.size() && (content[index] == ' ' || content[index] == '\t')) {
            ++index;
        }
        const std::size_t start = index;
        while (index < content.size() && content[index] != ' ' && content[index] != '\t') {
            ++index;
        }
        if (index > start) {
            tokens.push_back(content.substr(start, index - start));
        }
    }
    return tokens;
}

[[nodiscard]] bool ApplyField(std::string_view keyword, std::string_view rest, ParticleEffectAsset& asset) {
    if (keyword == "material") {
        asset.materialReference = std::string{ Trim(rest) };
        return true;
    }
    if (keyword == "looping") return ParseBoolToken(rest, asset.looping);
    if (keyword == "durationSeconds") return ParseFloatToken(rest, asset.durationSeconds);
    if (keyword == "maxParticles") return ParseUInt32Token(rest, asset.maxParticles);
    if (keyword == "emissionRatePerSecond") return ParseFloatToken(rest, asset.emissionRatePerSecond);
    if (keyword == "startSpeedMin") return ParseFloatToken(rest, asset.startSpeedMin);
    if (keyword == "startSpeedMax") return ParseFloatToken(rest, asset.startSpeedMax);
    if (keyword == "startLifetimeMin") return ParseFloatToken(rest, asset.startLifetimeMin);
    if (keyword == "startLifetimeMax") return ParseFloatToken(rest, asset.startLifetimeMax);
    if (keyword == "directionX") return ParseFloatToken(rest, asset.direction.x);
    if (keyword == "directionY") return ParseFloatToken(rest, asset.direction.y);
    if (keyword == "directionZ") return ParseFloatToken(rest, asset.direction.z);
    if (keyword == "spreadDegrees") return ParseFloatToken(rest, asset.spreadDegrees);
    if (keyword == "gravityScale") return ParseFloatToken(rest, asset.gravityScale);
    if (keyword == "sizeCurveKeyframe") {
        const std::vector<std::string_view> tokens = Tokenize(rest);
        if (tokens.size() != 3U) {
            return false;
        }
        kb::math::CurveKeyframe keyframe{};
        if (!ParseFloatToken(tokens[0], keyframe.time) || !ParseFloatToken(tokens[1], keyframe.value) || !ParseEasingToken(tokens[2], keyframe.easing)) {
            return false;
        }
        asset.sizeOverLifetime.keyframes.push_back(keyframe);
        return true;
    }
    if (keyword == "colorGradientStop") {
        const std::vector<std::string_view> tokens = Tokenize(rest);
        if (tokens.size() != 5U) {
            return false;
        }
        kb::math::GradientStop stop{};
        if (!ParseFloatToken(tokens[0], stop.time) || !ParseFloatToken(tokens[1], stop.color.r) ||
            !ParseFloatToken(tokens[2], stop.color.g) || !ParseFloatToken(tokens[3], stop.color.b) ||
            !ParseFloatToken(tokens[4], stop.color.a)) {
            return false;
        }
        asset.colorOverLifetime.stops.push_back(stop);
        return true;
    }
    return true; // unknown keyword - forward compatible, ignored rather than a hard parse failure.
}

[[nodiscard]] std::optional<ParticleEffectAsset> ParseAsset(std::string_view text) {
    ParticleEffectAsset asset{};
    std::istringstream input{ std::string{ text } };
    std::string line;
    while (std::getline(input, line)) {
        const std::string_view content = Trim(StripComment(line));
        if (content.empty()) {
            continue;
        }
        const std::size_t separator = content.find(' ');
        const std::string_view keyword = separator == std::string_view::npos ? content : content.substr(0U, separator);
        const std::string_view rest = separator == std::string_view::npos ? std::string_view{} : Trim(content.substr(separator + 1U));
        if (!ApplyField(keyword, rest, asset)) {
            return std::nullopt;
        }
    }
    // Curve::Evaluate returns 0.0F (invisible) for an empty keyframe list - a genuinely
    // author-omitted size curve should default to a constant, visible size instead.
    if (asset.sizeOverLifetime.keyframes.empty()) {
        asset.sizeOverLifetime.keyframes.push_back(kb::math::CurveKeyframe{ .time = 0.0F, .value = 1.0F, .easing = kb::math::Easing::Linear });
    }
    return asset;
}

void WriteAsset(std::ostream& output, const ParticleEffectAsset& asset) {
    output << "material " << asset.materialReference << '\n';
    output << "looping " << (asset.looping ? 1 : 0) << '\n';
    output << "durationSeconds " << asset.durationSeconds << '\n';
    output << "maxParticles " << asset.maxParticles << '\n';
    output << "emissionRatePerSecond " << asset.emissionRatePerSecond << '\n';
    output << "startSpeedMin " << asset.startSpeedMin << '\n';
    output << "startSpeedMax " << asset.startSpeedMax << '\n';
    output << "startLifetimeMin " << asset.startLifetimeMin << '\n';
    output << "startLifetimeMax " << asset.startLifetimeMax << '\n';
    output << "directionX " << asset.direction.x << '\n';
    output << "directionY " << asset.direction.y << '\n';
    output << "directionZ " << asset.direction.z << '\n';
    output << "spreadDegrees " << asset.spreadDegrees << '\n';
    output << "gravityScale " << asset.gravityScale << '\n';
    for (const kb::math::CurveKeyframe& keyframe : asset.sizeOverLifetime.keyframes) {
        output << "sizeCurveKeyframe " << keyframe.time << ' ' << keyframe.value << ' ' << kb::math::ToString(keyframe.easing) << '\n';
    }
    for (const kb::math::GradientStop& stop : asset.colorOverLifetime.stops) {
        output << "colorGradientStop " << stop.time << ' ' << stop.color.r << ' ' << stop.color.g << ' ' << stop.color.b << ' ' << stop.color.a << '\n';
    }
}

} // namespace

std::optional<ParticleEffectAsset> ParticleEffectAssetIO::Load(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) {
        return std::nullopt;
    }
    return ParseAsset(std::string_view{ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
}

bool ParticleEffectAssetIO::Save(const std::filesystem::path& path, const ParticleEffectAsset& asset) {
    std::ostringstream output;
    WriteAsset(output, asset);
    const std::string text = output.str();
    return SceneAssetBinaryIO::WriteBytesAtomically(
        path,
        std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

} // namespace kb::scene
