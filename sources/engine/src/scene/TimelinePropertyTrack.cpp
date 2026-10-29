#include "engine/scene/TimelinePropertyTrack.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace kb::scene {
namespace {

using Type = kb::ecs::ComponentFieldType;
using Value = kb::ecs::ComponentFieldValue;

bool Continuous(Type type) noexcept {
    return type == Type::Float32 || type == Type::Float64 ||
        type == Type::Vec2Float32 || type == Type::Vec3Float32 || type == Type::Vec4Float32;
}

bool Matches(Type type, const Value& value) noexcept {
    switch (type) {
    case Type::Bool: return std::holds_alternative<bool>(value);
    case Type::Int32:
    case Type::Enum32: return std::holds_alternative<std::int32_t>(value);
    case Type::UInt32: return std::holds_alternative<std::uint32_t>(value);
    case Type::Float32: return std::holds_alternative<float>(value);
    case Type::Float64: return std::holds_alternative<double>(value);
    case Type::Vec2Float32: return std::holds_alternative<std::array<float, 2>>(value);
    case Type::Vec3Float32: return std::holds_alternative<std::array<float, 3>>(value);
    case Type::Vec4Float32: return std::holds_alternative<std::array<float, 4>>(value);
    case Type::Bytes: return false;
    }
    return false;
}

bool Finite(const Value& value) noexcept {
    return std::visit([](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_arithmetic_v<T>) return std::isfinite(static_cast<double>(item));
        else if constexpr (std::is_same_v<T, std::array<float, 2>> ||
                           std::is_same_v<T, std::array<float, 3>> ||
                           std::is_same_v<T, std::array<float, 4>>) {
            return std::ranges::all_of(item, [](float number) { return std::isfinite(number); });
        } else return false;
    }, value);
}

bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}

} // namespace

std::uint64_t TimelineComponentSchema(const kb::ecs::ComponentReflection& reflection) noexcept {
    if (!reflection.IsValid()) return 0U;
    std::uint64_t hash = 14695981039346656037ULL;
    const auto byte = [&hash](unsigned char value) { hash = (hash ^ value) * 1099511628211ULL; };
    const auto text = [&byte](std::string_view value) {
        for (const char character : value) byte(static_cast<unsigned char>(character));
        byte(0U);
    };
    text(reflection.Name());
    for (const auto& field : reflection.Fields()) {
        text(field.name);
        byte(static_cast<unsigned char>(field.type));
    }
    return hash == 0U ? 1U : hash;
}

bool ValidateTimelinePropertyBinding(const TimelinePropertyBinding& binding,
    const kb::ecs::ComponentReflection& reflection, std::string& error) {
    error.clear();
    if (binding.version != 1U) return Fail(error, "Unsupported timeline property binding version");
    if (binding.component != reflection.Name()) return Fail(error, "Timeline component was renamed or is unavailable");
    const auto* field = reflection.FindField(binding.field);
    if (field == nullptr) return Fail(error, "Timeline property was renamed or removed");
    if (field->type != binding.type) return Fail(error, "Timeline property type changed");
    if (binding.componentSchema == 0U || binding.componentSchema != TimelineComponentSchema(reflection))
        return Fail(error, "Timeline component reflection schema changed");
    if (binding.type == Type::Bytes) return Fail(error, "Opaque component fields cannot be animated");
    return true;
}

bool ValidateTimelinePropertyTrack(const TimelinePropertyTrack& track, float durationSeconds, std::string& error) {
    error.clear();
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0F || track.binding.empty() ||
        track.property.version != 1U || track.property.component.empty() || track.property.field.empty() ||
        track.property.componentSchema == 0U || track.keyframes.empty() || track.keyframes.size() > 65536U)
        return Fail(error, "Invalid timeline property track or binding");
    if (track.interpolation != TimelinePropertyInterpolation::Step &&
        track.interpolation != TimelinePropertyInterpolation::Linear)
        return Fail(error, "Invalid timeline property interpolation");
    if (track.interpolation == TimelinePropertyInterpolation::Linear && !Continuous(track.property.type))
        return Fail(error, "Discrete timeline properties require step interpolation");
    float previous = -1.0F;
    for (const auto& key : track.keyframes) {
        if (!std::isfinite(key.timeSeconds) || key.timeSeconds < 0.0F || key.timeSeconds > durationSeconds ||
            key.timeSeconds <= previous || !Matches(track.property.type, key.value) || !Finite(key.value))
            return Fail(error, "Invalid timeline property keyframe time or value");
        previous = key.timeSeconds;
    }
    return true;
}

bool SampleTimelineProperty(const TimelinePropertyTrack& track, float timeSeconds, Value& value, std::string& error) {
    error.clear();
    if (!std::isfinite(timeSeconds) || track.keyframes.empty()) return Fail(error, "Invalid timeline property sample");
    // Authored tracks are validated at load time. Check the chosen pair again
    // so callers constructing tracks directly cannot trigger variant failures.
    const auto upper = std::upper_bound(track.keyframes.begin(), track.keyframes.end(), timeSeconds,
        [](float time, const TimelinePropertyKeyframe& key) { return time < key.timeSeconds; });
    const auto& from = upper == track.keyframes.begin() ? *upper : *(upper - 1);
    if (!Matches(track.property.type, from.value) || !Finite(from.value)) return Fail(error, "Invalid timeline property value");
    if (upper == track.keyframes.begin() || upper == track.keyframes.end() ||
        track.interpolation == TimelinePropertyInterpolation::Step) {
        value = from.value;
        return true;
    }
    const auto& to = *upper;
    if (track.interpolation != TimelinePropertyInterpolation::Linear || !Continuous(track.property.type) ||
        !Matches(track.property.type, to.value) || !Finite(to.value) || to.timeSeconds <= from.timeSeconds)
        return Fail(error, "Invalid timeline property interpolation pair");
    const double alpha = (static_cast<double>(timeSeconds) - from.timeSeconds) /
        (static_cast<double>(to.timeSeconds) - from.timeSeconds);
    value = std::visit([&](const auto& first) -> Value {
        using T = std::decay_t<decltype(first)>;
        const auto* last = std::get_if<T>(&to.value);
        if constexpr (std::is_floating_point_v<T>) return std::lerp(first, *last, static_cast<T>(alpha));
        else if constexpr (std::is_same_v<T, std::array<float, 2>> ||
                           std::is_same_v<T, std::array<float, 3>> ||
                           std::is_same_v<T, std::array<float, 4>>) {
            T result{};
            for (std::size_t i = 0; i < result.size(); ++i) result[i] = std::lerp(first[i], (*last)[i], static_cast<float>(alpha));
            return result;
        } else return {};
    }, from.value);
    return Finite(value) || Fail(error, "Timeline interpolation produced an invalid value");
}

} // namespace kb::scene
