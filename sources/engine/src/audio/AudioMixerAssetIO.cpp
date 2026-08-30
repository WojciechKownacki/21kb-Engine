#include "engine/audio/AudioMixerAssetIO.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::audio {
namespace {

inline constexpr std::string_view kFormatKeyword = "kbmixer";
inline constexpr std::string_view kFormatHeader = "kbmixer 1\n";

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
    // Not std::from_chars: Apple's libc++ does not implement its floating-point overload (the CI macOS job
    // fails with "call to deleted function"). TryParseDouble is this codebase's locale-independent answer
    // to exactly that gap.
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

// "-" is the explicit "no parent" (implicit master) marker so every record keeps a fixed
// token count - an empty field would be indistinguishable from a missing one.
[[nodiscard]] std::string ParentFromToken(std::string_view token) {
    return token == "-" ? std::string{} : std::string{ token };
}

[[nodiscard]] std::string_view ParentToToken(const std::string& parent) noexcept {
    return parent.empty() ? std::string_view{ "-" } : std::string_view{ parent };
}

[[nodiscard]] bool ApplyRecord(std::string_view keyword, std::string_view rest, AudioMixerAsset& asset) {
    if (keyword == "bus") {
        const std::vector<std::string_view> tokens = Tokenize(rest);
        if (tokens.size() != 4U) {
            return false;
        }
        AudioMixerBus bus{};
        bus.name = std::string{ tokens[0] };
        bus.parentBus = ParentFromToken(tokens[1]);
        return ParseFloatToken(tokens[2], bus.volume) && ParseBoolToken(tokens[3], bus.mute)
            ? (asset.buses.push_back(std::move(bus)), true)
            : false;
    }
    if (keyword == "snapshot") {
        const std::vector<std::string_view> tokens = Tokenize(rest);
        if (tokens.size() != 1U) {
            return false;
        }
        asset.snapshots.push_back(AudioMixerSnapshot{ .name = std::string{ tokens[0] }, .busVolumes = {} });
        return true;
    }
    if (keyword == "snapshotVolume") {
        const std::vector<std::string_view> tokens = Tokenize(rest);
        if (tokens.size() != 3U) {
            return false;
        }
        for (AudioMixerSnapshot& snapshot : asset.snapshots) {
            if (snapshot.name == tokens[0]) {
                AudioMixerSnapshotBusVolume value{};
                value.bus = std::string{ tokens[1] };
                if (!ParseFloatToken(tokens[2], value.volume)) {
                    return false;
                }
                snapshot.busVolumes.push_back(std::move(value));
                return true;
            }
        }
        // A snapshotVolume line must follow its own snapshot's declaration - referencing an
        // undeclared snapshot is a structural error, not an ignorable unknown keyword.
        return false;
    }
    return true; // unknown keyword - forward compatible, ignored rather than a hard parse failure.
}

[[nodiscard]] bool IsAssetRecord(std::string_view keyword) noexcept {
    return keyword == "bus" || keyword == "snapshot" || keyword == "snapshotVolume";
}

[[nodiscard]] std::optional<AudioMixerAsset> ParseAsset(std::string_view text) {
    AudioMixerAsset asset{};
    std::istringstream input{ std::string{ text } };
    std::string line;
    bool sawContent = false;
    bool sawHeader = false;
    bool sawAssetRecord = false;
    while (std::getline(input, line)) {
        const std::string_view content = Trim(StripComment(line));
        if (content.empty()) {
            continue;
        }
        const std::size_t separator = content.find_first_of(" \t");
        const std::string_view keyword = separator == std::string_view::npos ? content : content.substr(0U, separator);
        const std::string_view rest = separator == std::string_view::npos ? std::string_view{} : Trim(content.substr(separator + 1U));
        if (keyword == kFormatKeyword) {
            if (sawContent || rest != "1") {
                return std::nullopt;
            }
            sawContent = true;
            sawHeader = true;
            continue;
        }
        sawContent = true;
        sawAssetRecord = sawAssetRecord || IsAssetRecord(keyword);
        if (!ApplyRecord(keyword, rest, asset)) {
            return std::nullopt;
        }
    }
    if (!sawHeader && !sawAssetRecord) {
        return std::nullopt;
    }
    if (!ValidateAudioMixerAsset(asset).empty()) {
        return std::nullopt;
    }
    return asset;
}

void WriteAsset(std::ostream& output, const AudioMixerAsset& asset) {
    output << kFormatHeader;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const AudioMixerBus& bus : asset.buses) {
        output << "bus " << bus.name << ' ' << ParentToToken(bus.parentBus) << ' ' << bus.volume << ' ' << (bus.mute ? 1 : 0) << '\n';
    }
    for (const AudioMixerSnapshot& snapshot : asset.snapshots) {
        output << "snapshot " << snapshot.name << '\n';
        for (const AudioMixerSnapshotBusVolume& value : snapshot.busVolumes) {
            output << "snapshotVolume " << snapshot.name << ' ' << value.bus << ' ' << value.volume << '\n';
        }
    }
}

} // namespace

std::string ValidateAudioMixerAsset(const AudioMixerAsset& asset) {
    std::unordered_map<std::string_view, const AudioMixerBus*> busesByName;
    busesByName.reserve(asset.buses.size());
    for (const AudioMixerBus& bus : asset.buses) {
        if (!IsAudioMixerNameTokenValid(bus.name)) {
            return "audio mixer bus name must be a non-empty, whitespace-free token (and not '-')";
        }
        if (!std::isfinite(bus.volume) || bus.volume < 0.0F) {
            return "audio mixer bus volume must be finite and non-negative";
        }
        if (!busesByName.emplace(bus.name, &bus).second) {
            return "audio mixer bus name '" + bus.name + "' is declared more than once";
        }
    }
    for (const AudioMixerBus& bus : asset.buses) {
        if (!bus.parentBus.empty() && busesByName.find(bus.parentBus) == busesByName.end()) {
            return "audio mixer bus '" + bus.name + "' routes to unknown parent bus '" + bus.parentBus + "'";
        }
    }
    // Cycle check: walk each bus's parent chain; a valid chain reaches the implicit master
    // (empty parent) in at most buses.size() steps.
    for (const AudioMixerBus& bus : asset.buses) {
        const AudioMixerBus* current = &bus;
        for (std::size_t step = 0U; step <= asset.buses.size(); ++step) {
            if (current->parentBus.empty()) {
                current = nullptr;
                break;
            }
            current = busesByName.find(current->parentBus)->second;
        }
        if (current != nullptr) {
            return "audio mixer bus '" + bus.name + "' is part of a parent-routing cycle";
        }
    }
    std::unordered_set<std::string_view> snapshotNames;
    snapshotNames.reserve(asset.snapshots.size());
    for (const AudioMixerSnapshot& snapshot : asset.snapshots) {
        if (!IsAudioMixerNameTokenValid(snapshot.name)) {
            return "audio mixer snapshot name must be a non-empty, whitespace-free token (and not '-')";
        }
        if (!snapshotNames.insert(snapshot.name).second) {
            return "audio mixer snapshot name '" + snapshot.name + "' is declared more than once";
        }
        std::unordered_set<std::string_view> overriddenBuses;
        overriddenBuses.reserve(snapshot.busVolumes.size());
        for (const AudioMixerSnapshotBusVolume& value : snapshot.busVolumes) {
            if (!std::isfinite(value.volume) || value.volume < 0.0F) {
                return "audio mixer snapshot volume must be finite and non-negative";
            }
            if (busesByName.find(value.bus) == busesByName.end()) {
                return "audio mixer snapshot '" + snapshot.name + "' overrides unknown bus '" + value.bus + "'";
            }
            if (!overriddenBuses.insert(value.bus).second) {
                return "audio mixer snapshot '" + snapshot.name + "' overrides bus '" + value.bus + "' more than once";
            }
        }
    }
    return {};
}

std::optional<AudioMixerAsset> AudioMixerAssetIO::Load(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = kb::scene::SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) {
        return std::nullopt;
    }
    return Load(bytes);
}

std::optional<AudioMixerAsset> AudioMixerAssetIO::Load(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    return ParseAsset(std::string_view{ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
}

bool AudioMixerAssetIO::Save(const std::filesystem::path& path, const AudioMixerAsset& asset) {
    if (!ValidateAudioMixerAsset(asset).empty()) {
        return false;
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    WriteAsset(output, asset);
    const std::string text = output.str();
    return kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(
        path,
        std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

} // namespace kb::audio
