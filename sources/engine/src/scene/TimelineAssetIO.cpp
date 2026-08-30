#include "engine/scene/TimelineAssetIO.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace kb::scene {
namespace {

constexpr std::size_t kMaxBindings = 256U;
constexpr std::size_t kMaxTracks = 256U;
constexpr std::size_t kMaxKeyframesPerTrack = 65536U;
constexpr std::size_t kMaxMarkers = 65536U;
constexpr float kMaxDurationSeconds = 24.0F * 60.0F * 60.0F;

[[nodiscard]] bool Finite(const LocalTransform& value) noexcept {
    const auto finite = [](float number) { return std::isfinite(number); };
    const float rotationLength =
        value.rotation.x * value.rotation.x +
        value.rotation.y * value.rotation.y +
        value.rotation.z * value.rotation.z +
        value.rotation.w * value.rotation.w;
    return finite(value.position.x) && finite(value.position.y) &&
        finite(value.position.z) && finite(value.rotation.x) &&
        finite(value.rotation.y) && finite(value.rotation.z) &&
        finite(value.rotation.w) && finite(value.scale.x) &&
        finite(value.scale.y) && finite(value.scale.z) &&
        std::abs(rotationLength - 1.0F) <= 1.0e-3F;
}

[[nodiscard]] bool Validate(const TimelineAsset& asset) {
    if (!std::isfinite(asset.durationSeconds) ||
        asset.durationSeconds <= 0.0F ||
        asset.durationSeconds > kMaxDurationSeconds ||
        asset.bindings.empty() || asset.bindings.size() > kMaxBindings ||
        asset.transformTracks.size() > kMaxTracks ||
        asset.markers.size() > kMaxMarkers) {
        return false;
    }
    std::set<std::string, std::less<>> bindingNames;
    for (const TimelineBindingDefinition& binding : asset.bindings) {
        if (binding.name.empty() || binding.defaultPath.empty() ||
            binding.defaultPath.front() == '/' ||
            binding.defaultPath.back() == '/' ||
            binding.defaultPath.find("//") != std::string::npos ||
            !bindingNames.insert(binding.name).second) {
            return false;
        }
    }
    std::set<std::string, std::less<>> trackBindings;
    for (const TimelineTransformTrack& track : asset.transformTracks) {
        if (!bindingNames.contains(track.binding) ||
            !trackBindings.insert(track.binding).second ||
            track.keyframes.empty() ||
            track.keyframes.size() > kMaxKeyframesPerTrack) {
            return false;
        }
        float previous = -1.0F;
        for (const TimelineTransformKeyframe& keyframe : track.keyframes) {
            if (!std::isfinite(keyframe.timeSeconds) ||
                keyframe.timeSeconds < 0.0F ||
                keyframe.timeSeconds > asset.durationSeconds ||
                keyframe.timeSeconds <= previous ||
                !Finite(keyframe.transform)) {
                return false;
            }
            previous = keyframe.timeSeconds;
        }
    }
    float previousMarker = -1.0F;
    for (const TimelineMarker& marker : asset.markers) {
        if (marker.id == 0U || !std::isfinite(marker.timeSeconds) ||
            marker.timeSeconds <= 0.0F ||
            marker.timeSeconds > asset.durationSeconds ||
            marker.timeSeconds < previousMarker) {
            return false;
        }
        previousMarker = marker.timeSeconds;
    }
    return true;
}

} // namespace

std::optional<TimelineAsset> TimelineAssetIO::Load(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return std::nullopt;
    return Load(input);
}

std::optional<TimelineAsset> TimelineAssetIO::Load(std::istream& input) {
    TimelineAsset asset{};
    asset.bindings.clear();
    std::string line;
    bool header = false;
    bool duration = false;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string keyword;
        row >> keyword;
        if (keyword == "kbtimeline") {
            int version = 0;
            if (header || !(row >> version) || version != 1) {
                return std::nullopt;
            }
            header = true;
        } else if (keyword == "duration") {
            if (!header || duration || !(row >> asset.durationSeconds)) {
                return std::nullopt;
            }
            duration = true;
        } else if (keyword == "binding") {
            if (!header) return std::nullopt;
            TimelineBindingDefinition binding{};
            if (!(row >> std::quoted(binding.name) >>
                  std::quoted(binding.defaultPath))) {
                return std::nullopt;
            }
            asset.bindings.push_back(std::move(binding));
        } else if (keyword == "transform") {
            if (!header) return std::nullopt;
            std::string binding;
            TimelineTransformKeyframe keyframe{};
            if (!(row >> std::quoted(binding) >> keyframe.timeSeconds >>
                  keyframe.transform.position.x >>
                  keyframe.transform.position.y >>
                  keyframe.transform.position.z >>
                  keyframe.transform.rotation.x >>
                  keyframe.transform.rotation.y >>
                  keyframe.transform.rotation.z >>
                  keyframe.transform.rotation.w >>
                  keyframe.transform.scale.x >>
                  keyframe.transform.scale.y >>
                  keyframe.transform.scale.z)) {
                return std::nullopt;
            }
            auto track = std::find_if(
                asset.transformTracks.begin(), asset.transformTracks.end(),
                [&](const TimelineTransformTrack& value) {
                    return value.binding == binding;
                });
            if (track == asset.transformTracks.end()) {
                asset.transformTracks.push_back(
                    TimelineTransformTrack{ .binding = std::move(binding) });
                track = asset.transformTracks.end() - 1;
            }
            track->keyframes.push_back(std::move(keyframe));
        } else if (keyword == "marker") {
            if (!header) return std::nullopt;
            TimelineMarker marker{};
            if (!(row >> marker.timeSeconds >> marker.id)) {
                return std::nullopt;
            }
            asset.markers.push_back(marker);
        } else {
            return std::nullopt;
        }
        std::string trailing;
        if (row >> trailing) return std::nullopt;
    }
    return header && duration && Validate(asset)
        ? std::optional<TimelineAsset>{ std::move(asset) }
        : std::nullopt;
}

bool TimelineAssetIO::Save(
    const std::filesystem::path& path, const TimelineAsset& asset) {
    if (!Validate(asset)) return false;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "kbtimeline 1\n";
    output << "duration " << asset.durationSeconds << '\n';
    for (const TimelineBindingDefinition& binding : asset.bindings) {
        output << "binding " << std::quoted(binding.name) << ' '
               << std::quoted(binding.defaultPath) << '\n';
    }
    for (const TimelineTransformTrack& track : asset.transformTracks) {
        for (const TimelineTransformKeyframe& keyframe : track.keyframes) {
            const LocalTransform& value = keyframe.transform;
            output << "transform " << std::quoted(track.binding) << ' '
                   << keyframe.timeSeconds << ' '
                   << value.position.x << ' ' << value.position.y << ' '
                   << value.position.z << ' ' << value.rotation.x << ' '
                   << value.rotation.y << ' ' << value.rotation.z << ' '
                   << value.rotation.w << ' ' << value.scale.x << ' '
                   << value.scale.y << ' ' << value.scale.z << '\n';
        }
    }
    for (const TimelineMarker& marker : asset.markers) {
        output << "marker " << marker.timeSeconds << ' ' << marker.id << '\n';
    }
    return output.good();
}

} // namespace kb::scene
