#include "engine/scene/TimelineAssetIO.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <tuple>
#include <type_traits>

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

[[nodiscard]] bool IsValidTimeline(const TimelineAsset& asset) {
    if (!std::isfinite(asset.durationSeconds) ||
        asset.durationSeconds <= 0.0F ||
        asset.durationSeconds > kMaxDurationSeconds ||
        asset.bindings.empty() || asset.bindings.size() > kMaxBindings ||
        asset.transformTracks.size() + asset.propertyTracks.size() + asset.audioTracks.size() + asset.eventTracks.size() + asset.sequenceTracks.size() + asset.animationTracks.size() > kMaxTracks ||
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
    for (const auto& track : asset.sequenceTracks) {
        if (track.sequenceReference.empty() || !std::isfinite(track.startSeconds) ||
            !std::isfinite(track.endSeconds) || !std::isfinite(track.sourceOffsetSeconds) ||
            !std::isfinite(track.rate) || track.startSeconds < 0.0F || track.endSeconds <= track.startSeconds ||
            track.endSeconds > asset.durationSeconds || track.sourceOffsetSeconds < 0.0F || track.rate <= 0.0F ||
            track.bindings.empty() || track.bindings.size() > kMaxBindings) return false;
        std::set<std::string, std::less<>> children;
        for (const auto& binding : track.bindings) {
            if (binding.child.empty() || !bindingNames.contains(binding.parent) || !children.insert(binding.child).second)
                return false;
        }
    }
    std::set<std::string, std::less<>> trackBindings;
    for (const auto& track : asset.animationTracks) {
        if (!bindingNames.contains(track.binding) || track.clipReference.empty() ||
            !std::isfinite(track.startSeconds) || !std::isfinite(track.endSeconds) ||
            !std::isfinite(track.sourceOffsetSeconds) || !std::isfinite(track.rate) ||
            track.startSeconds < 0.0F || track.endSeconds <= track.startSeconds ||
            track.endSeconds > asset.durationSeconds || track.sourceOffsetSeconds < 0.0F || track.rate <= 0.0F)
            return false;
    }
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
    std::set<std::tuple<std::string, std::string, std::string>> propertyBindings;
    for (const auto& track : asset.propertyTracks) {
        std::string error;
        if (!bindingNames.contains(track.binding) ||
            !propertyBindings.emplace(track.binding, track.property.component, track.property.field).second ||
            !ValidateTimelinePropertyTrack(track, asset.durationSeconds, error)) return false;
    }
    for (const auto& track : asset.audioTracks) {
        std::string error;
        if (!bindingNames.contains(track.binding) ||
            !ValidateTimelineAudioTrack(track, asset.durationSeconds, error)) return false;
    }
    std::set<std::string, std::less<>> eventBindings;
    std::size_t eventCount = asset.markers.size();
    for (const auto& track : asset.eventTracks) {
        if (!bindingNames.contains(track.binding) || !eventBindings.insert(track.binding).second || track.keyframes.empty()) return false;
        float previous = -1.0F;
        for (const auto& key : track.keyframes) {
            if (++eventCount > kMaxMarkers || !std::isfinite(key.timeSeconds) || key.timeSeconds <= 0.0F ||
                key.timeSeconds > asset.durationSeconds || key.timeSeconds < previous || key.id == 0 ||
                key.name.empty() || key.name.size() > 255 || key.payload.size() > 65536) return false;
            previous = key.timeSeconds;
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

bool ReadPropertyValue(std::istream& input, kb::ecs::ComponentFieldType type, kb::ecs::ComponentFieldValue& output) {
    using Type = kb::ecs::ComponentFieldType;
    switch (type) {
    case Type::Bool: {
        int value = 0;
        if (!(input >> value) || (value != 0 && value != 1)) return false;
        output = value != 0;
        return true;
    }
    case Type::Int32:
    case Type::Enum32: { std::int32_t value{}; if (!(input >> value)) return false; output = value; return true; }
    case Type::UInt32: {
        std::int64_t value{};
        if (!(input >> value) || value < 0 || value > std::numeric_limits<std::uint32_t>::max()) return false;
        output = static_cast<std::uint32_t>(value); return true;
    }
    case Type::Float32: { float value{}; if (!(input >> value)) return false; output = value; return true; }
    case Type::Float64: { double value{}; if (!(input >> value)) return false; output = value; return true; }
    case Type::Vec2Float32: {
        std::array<float, 2> value{}; for (auto& item : value) if (!(input >> item)) return false;
        output = value; return true;
    }
    case Type::Vec3Float32: {
        std::array<float, 3> value{}; for (auto& item : value) if (!(input >> item)) return false;
        output = value; return true;
    }
    case Type::Vec4Float32: {
        std::array<float, 4> value{}; for (auto& item : value) if (!(input >> item)) return false;
        output = value; return true;
    }
    case Type::Bytes: return false;
    }
    return false;
}

} // namespace

bool TimelineAssetIO::Validate(const TimelineAsset& asset) {
    return IsValidTimeline(asset);
}

std::optional<TimelineAsset> TimelineAssetIO::Load(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return Load(input);
}

std::optional<TimelineAsset> TimelineAssetIO::Load(std::istream& input) {
    TimelineAsset asset{};
    asset.bindings.clear();
    std::string line;
    bool header = false;
    bool duration = false;
    int version = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        // Quoted event payloads may contain line breaks. Preserve them instead
        // of treating the continuation as another authored row.
        bool quoted = false;
        bool escaped = false;
        std::size_t scanned = 0;
        for (;;) {
            for (; scanned < line.size(); ++scanned) {
                const char value = line[scanned];
                if (escaped) { escaped = false; continue; }
                if (quoted && value == '\\') escaped = true;
                else if (value == '"') quoted = !quoted;
            }
            if (!quoted) break;
            std::string continuation;
            if (line.size() > 1024U * 1024U || !std::getline(input, continuation)) return std::nullopt;
            line += '\n';
            line += continuation;
        }
        std::istringstream row(line);
        std::string keyword;
        row >> keyword;
        if (keyword == "kbtimeline") {
            if (header || !(row >> version) || (version != 1 && version != 2)) {
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
        } else if (keyword == "property") {
            if (!header || version < 2 || asset.propertyTracks.size() >= kMaxTracks) return std::nullopt;
            TimelinePropertyTrack track;
            unsigned type = 0, interpolation = 0;
            if (!(row >> std::quoted(track.binding) >> track.property.version >>
                std::quoted(track.property.component) >> std::quoted(track.property.field) >> type >>
                track.property.componentSchema >> interpolation) ||
                type >= static_cast<unsigned>(kb::ecs::ComponentFieldType::Bytes) || interpolation > 1U)
                return std::nullopt;
            track.property.type = static_cast<kb::ecs::ComponentFieldType>(type);
            track.interpolation = static_cast<TimelinePropertyInterpolation>(interpolation);
            asset.propertyTracks.push_back(std::move(track));
        } else if (keyword == "property-key") {
            std::size_t index = 0;
            TimelinePropertyKeyframe key;
            if (!header || version < 2 || !(row >> index >> key.timeSeconds) || index >= asset.propertyTracks.size())
                return std::nullopt;
            auto& track = asset.propertyTracks[index];
            if (track.keyframes.size() >= kMaxKeyframesPerTrack || !ReadPropertyValue(row, track.property.type, key.value))
                return std::nullopt;
            track.keyframes.push_back(std::move(key));
        } else if (keyword == "animation") {
            if (!header || version < 2 || asset.animationTracks.size() >= kMaxTracks) return std::nullopt;
            TimelineAnimationTrack track;
            if (!(row >> std::quoted(track.binding) >> std::quoted(track.clipReference) >> track.startSeconds >>
                track.endSeconds >> track.sourceOffsetSeconds >> track.rate)) return std::nullopt;
            asset.animationTracks.push_back(std::move(track));
        } else if (keyword == "sequence") {
            if (!header || version < 2 || asset.sequenceTracks.size() >= kMaxTracks) return std::nullopt;
            TimelineSequenceTrack track;
            if (!(row >> std::quoted(track.sequenceReference) >> track.startSeconds >> track.endSeconds >>
                track.sourceOffsetSeconds >> track.rate)) return std::nullopt;
            asset.sequenceTracks.push_back(std::move(track));
        } else if (keyword == "sequence-binding") {
            std::size_t index = 0;
            TimelineSequenceBinding binding;
            if (!header || version < 2 || !(row >> index >> std::quoted(binding.child) >> std::quoted(binding.parent)) ||
                index >= asset.sequenceTracks.size() || asset.sequenceTracks[index].bindings.size() >= kMaxBindings)
                return std::nullopt;
            asset.sequenceTracks[index].bindings.push_back(std::move(binding));
        } else if (keyword == "event") {
            if (!header || version < 2) return std::nullopt;
            std::string binding;
            TimelineEventKeyframe key;
            if (!(row >> std::quoted(binding) >> key.timeSeconds >> key.id >> std::quoted(key.name) >> std::quoted(key.payload)))
                return std::nullopt;
            auto track = std::find_if(asset.eventTracks.begin(), asset.eventTracks.end(),
                [&](const auto& entry) { return entry.binding == binding; });
            if (track == asset.eventTracks.end()) {
                if (asset.eventTracks.size() >= kMaxTracks) return std::nullopt;
                asset.eventTracks.push_back({.binding = std::move(binding)});
                track = asset.eventTracks.end() - 1;
            }
            if (track->keyframes.size() >= kMaxMarkers) return std::nullopt;
            track->keyframes.push_back(std::move(key));
        } else if (keyword == "audio") {
            if (!header || version < 2 || asset.audioTracks.size() >= kMaxTracks) return std::nullopt;
            TimelineAudioTrack track;
            int spatial = 0;
            if (!(row >> std::quoted(track.binding) >> std::quoted(track.clipReference) >>
                track.startSeconds >> track.endSeconds >> track.clipOffsetSeconds >> track.volume >>
                spatial >> std::quoted(track.outputBus)) || (spatial != 0 && spatial != 1)) return std::nullopt;
            track.spatial = spatial != 0;
            asset.audioTracks.push_back(std::move(track));
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
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "kbtimeline 2\n";
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
    for (std::size_t index = 0; index < asset.propertyTracks.size(); ++index) {
        const auto& track = asset.propertyTracks[index];
        output << "property " << std::quoted(track.binding) << ' ' << track.property.version << ' '
            << std::quoted(track.property.component) << ' ' << std::quoted(track.property.field) << ' '
            << static_cast<unsigned>(track.property.type) << ' ' << track.property.componentSchema << ' '
            << static_cast<unsigned>(track.interpolation) << '\n';
        for (const auto& key : track.keyframes) {
            output << "property-key " << index << ' ' << key.timeSeconds;
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_arithmetic_v<T>) output << ' ' << value;
                else if constexpr (std::is_same_v<T, std::array<float, 2>> ||
                                   std::is_same_v<T, std::array<float, 3>> ||
                                   std::is_same_v<T, std::array<float, 4>>) {
                    for (const auto item : value) output << ' ' << item;
                }
            }, key.value);
            output << '\n';
        }
    }
    for (const auto& track : asset.animationTracks)
        output << "animation " << std::quoted(track.binding) << ' ' << std::quoted(track.clipReference) << ' '
            << track.startSeconds << ' ' << track.endSeconds << ' ' << track.sourceOffsetSeconds << ' ' << track.rate << '\n';
    for (std::size_t index = 0; index < asset.sequenceTracks.size(); ++index) {
        const auto& track = asset.sequenceTracks[index];
        output << "sequence " << std::quoted(track.sequenceReference) << ' ' << track.startSeconds << ' '
            << track.endSeconds << ' ' << track.sourceOffsetSeconds << ' ' << track.rate << '\n';
        for (const auto& binding : track.bindings)
            output << "sequence-binding " << index << ' ' << std::quoted(binding.child) << ' '
                << std::quoted(binding.parent) << '\n';
    }
    for (const auto& track : asset.audioTracks) {
        output << "audio " << std::quoted(track.binding) << ' ' << std::quoted(track.clipReference) << ' '
            << track.startSeconds << ' ' << track.endSeconds << ' ' << track.clipOffsetSeconds << ' '
            << track.volume << ' ' << static_cast<int>(track.spatial) << ' ' << std::quoted(track.outputBus) << '\n';
    }
    for (const auto& track : asset.eventTracks) {
        for (const auto& key : track.keyframes)
            output << "event " << std::quoted(track.binding) << ' ' << key.timeSeconds << ' ' << key.id << ' '
                << std::quoted(key.name) << ' ' << std::quoted(key.payload) << '\n';
    }
    for (const TimelineMarker& marker : asset.markers) {
        output << "marker " << marker.timeSeconds << ' ' << marker.id << '\n';
    }
    if (!output.good()) return false;
    const std::string text = output.str();
    return SceneAssetBinaryIO::WriteBytesAtomically(path,
        std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

} // namespace kb::scene
