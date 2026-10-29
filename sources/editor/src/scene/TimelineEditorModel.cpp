#include "scene/TimelineEditorModel.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/ui/layout/UICanvasGroup.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace kb::editor {
namespace {
using Asset = kb::scene::TimelineAsset;
template<class T> std::string Format(const T& value) {
    std::ostringstream out; out << std::setprecision(std::numeric_limits<double>::max_digits10);
    if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
    else out << value;
    return out.str();
}
template<class T, std::size_t N> std::string Format(const std::array<T, N>& value) {
    std::string result;
    for (const auto item : value) { if (!result.empty()) result += ' '; result += Format(item); }
    return result;
}
template<class T> bool Parse(const std::string& text, T& value) {
    if constexpr (std::is_same_v<T, std::string>) { value = text; return true; }
    else if constexpr (std::is_same_v<T, bool>) {
        if (text != "true" && text != "false") return false;
        value = text == "true"; return true;
    } else {
        if constexpr (std::is_unsigned_v<T>) if (text.find('-') != std::string::npos) return false;
        std::istringstream input(text); T candidate{};
        if (!(input >> candidate) || !(input >> std::ws).eof()) return false;
        if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(candidate)) return false;
        value = candidate; return true;
    }
}
template<class T, std::size_t N> bool Parse(const std::string& text, std::array<T, N>& value) {
    std::istringstream input(text); auto candidate = value;
    for (auto& item : candidate) if (!(input >> item) || !std::isfinite(item)) return false;
    if (!(input >> std::ws).eof()) return false;
    value = candidate; return true;
}
std::string ValueText(const kb::ecs::ComponentFieldValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::vector<std::byte>>) return {};
        else return Format(item);
    }, value);
}
bool ParseValue(const std::string& text, kb::ecs::ComponentFieldValue& value) {
    return std::visit([&](auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::vector<std::byte>>) return false;
        else return Parse(text, item);
    }, value);
}
template<class Access> void Field(std::vector<TimelineEditorRow>& rows, const Asset& asset, std::string label, Access access) {
    rows.push_back({std::move(label), Format(access(asset)), [access](Asset& target, const std::string& text) {
        return Parse(text, access(target));
    }, {}, {}});
}
template<class Access> void Header(std::vector<TimelineEditorRow>& rows, std::string label, std::size_t index, Access access) {
    rows.push_back({std::move(label), "Remove", {}, [access, index](Asset& asset) {
        auto& list = access(asset); if (index >= list.size()) return false;
        list.erase(list.begin() + index); return true;
    }, {}});
}
template<class Access> void Keys(TimelineEditorRow& row, Access access) {
    row.addKey = [access](Asset& asset, float time) {
        auto& keys = access(asset);
        if (keys.empty()) return false;
        auto key = keys.back(); key.timeSeconds = time;
        keys.push_back(std::move(key));
        std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
        return true;
    };
}
} // namespace

std::vector<TimelineEditorRow> TimelineEditorModel::Rows(const Asset& asset) {
    std::vector<TimelineEditorRow> rows;
    Field(rows, asset, "Duration (seconds)", [](auto& a) -> auto& { return a.durationSeconds; });
    for (std::size_t i = 0; i < asset.bindings.size(); ++i) {
        Header(rows, "Binding " + asset.bindings[i].name, i, [](auto& a) -> auto& { return a.bindings; });
        rows.push_back({"  Name", asset.bindings[i].name, [i](Asset& a, const std::string& name) {
            const auto previous = a.bindings[i].name;
            a.bindings[i].name = name;
            const auto rename = [&](auto& tracks) { for (auto& track : tracks) if (track.binding == previous) track.binding = name; };
            rename(a.transformTracks); rename(a.propertyTracks); rename(a.audioTracks); rename(a.animationTracks); rename(a.eventTracks);
            for (auto& track : a.sequenceTracks) for (auto& binding : track.bindings) if (binding.parent == previous) binding.parent = name;
            return true;
        }, {}, {}});
        Field(rows, asset, "  Owner-relative path", [i](auto& a) -> auto& { return a.bindings[i].defaultPath; });
    }
    for (std::size_t i = 0; i < asset.transformTracks.size(); ++i) {
        Header(rows, "Transform track", i, [](auto& a) -> auto& { return a.transformTracks; });
        Keys(rows.back(), [i](auto& a) -> auto& { return a.transformTracks[i].keyframes; });
        Field(rows, asset, "  Binding", [i](auto& a) -> auto& { return a.transformTracks[i].binding; });
        for (std::size_t k = 0; k < asset.transformTracks[i].keyframes.size(); ++k) {
            Header(rows, "  Transform key " + std::to_string(k), k, [i](auto& a) -> auto& { return a.transformTracks[i].keyframes; });
            Field(rows, asset, "    Time", [i,k](auto& a) -> auto& { return a.transformTracks[i].keyframes[k].timeSeconds; });
            const auto vectorField = [&](const char* label, auto access) {
                const auto& v = access(asset);
                rows.push_back({label, Format(std::array{v.x,v.y,v.z}), [access](Asset& a, const std::string& text) {
                    std::array<float,3> values{}; if (!Parse(text, values)) return false;
                    auto& v = access(a); v.x = values[0]; v.y = values[1]; v.z = values[2]; return true;
                }, {}, {}});
            };
            vectorField("    Position (x y z)", [i,k](auto& a) -> auto& { return a.transformTracks[i].keyframes[k].transform.position; });
            vectorField("    Scale (x y z)", [i,k](auto& a) -> auto& { return a.transformTracks[i].keyframes[k].transform.scale; });
            const auto& q = asset.transformTracks[i].keyframes[k].transform.rotation;
            rows.push_back({"    Rotation (x y z w)", Format(std::array{q.x,q.y,q.z,q.w}), [i,k](Asset& a, const std::string& text) {
                std::array<float,4> v{}; if (!Parse(text,v)) return false;
                a.transformTracks[i].keyframes[k].transform.rotation = {v[0],v[1],v[2],v[3]}; return true;
            }, {}, {}});
        }
    }
    for (std::size_t i = 0; i < asset.propertyTracks.size(); ++i) {
        const auto& track = asset.propertyTracks[i];
        Header(rows, "Property " + track.property.component + "." + track.property.field, i, [](auto& a) -> auto& { return a.propertyTracks; });
        Keys(rows.back(), [i](auto& a) -> auto& { return a.propertyTracks[i].keyframes; });
        Field(rows, asset, "  Binding", [i](auto& a) -> auto& { return a.propertyTracks[i].binding; });
        rows.push_back({"  Interpolation", track.interpolation == kb::scene::TimelinePropertyInterpolation::Step ? "Step" : "Linear",
            [i](Asset& a, const std::string& value) {
                if (value != "Step" && value != "Linear") return false;
                a.propertyTracks[i].interpolation = value == "Step" ? kb::scene::TimelinePropertyInterpolation::Step : kb::scene::TimelinePropertyInterpolation::Linear;
                return true;
            }, {}, {}});
        for (std::size_t k = 0; k < track.keyframes.size(); ++k) {
            Header(rows, "  Property key " + std::to_string(k), k, [i](auto& a) -> auto& { return a.propertyTracks[i].keyframes; });
            Field(rows, asset, "    Time", [i,k](auto& a) -> auto& { return a.propertyTracks[i].keyframes[k].timeSeconds; });
            rows.push_back({"    Value", ValueText(track.keyframes[k].value), [i,k](Asset& a, const std::string& text) {
                return ParseValue(text, a.propertyTracks[i].keyframes[k].value);
            }, {}, {}});
        }
    }
    for (std::size_t i = 0; i < asset.eventTracks.size(); ++i) {
        Header(rows, "Event track", i, [](auto& a) -> auto& { return a.eventTracks; });
        Keys(rows.back(), [i](auto& a) -> auto& { return a.eventTracks[i].keyframes; });
        Field(rows, asset, "  Binding", [i](auto& a) -> auto& { return a.eventTracks[i].binding; });
        for (std::size_t k = 0; k < asset.eventTracks[i].keyframes.size(); ++k) {
            Header(rows, "  Event key " + std::to_string(k), k, [i](auto& a) -> auto& { return a.eventTracks[i].keyframes; });
            Field(rows, asset, "    Time", [i,k](auto& a) -> auto& { return a.eventTracks[i].keyframes[k].timeSeconds; });
            Field(rows, asset, "    ID", [i,k](auto& a) -> auto& { return a.eventTracks[i].keyframes[k].id; });
            Field(rows, asset, "    Name", [i,k](auto& a) -> auto& { return a.eventTracks[i].keyframes[k].name; });
            Field(rows, asset, "    Payload", [i,k](auto& a) -> auto& { return a.eventTracks[i].keyframes[k].payload; });
        }
    }
    for (std::size_t i = 0; i < asset.markers.size(); ++i) {
        Header(rows, "Marker", i, [](auto& a) -> auto& { return a.markers; });
        Field(rows, asset, "  Time", [i](auto& a) -> auto& { return a.markers[i].timeSeconds; });
        Field(rows, asset, "  ID", [i](auto& a) -> auto& { return a.markers[i].id; });
    }
    for (std::size_t i = 0; i < asset.audioTracks.size(); ++i) {
        Header(rows, "Audio track", i, [](auto& a) -> auto& { return a.audioTracks; });
        Field(rows, asset, "  Binding", [i](auto& a) -> auto& { return a.audioTracks[i].binding; });
        Field(rows, asset, "  Clip", [i](auto& a) -> auto& { return a.audioTracks[i].clipReference; });
        Field(rows, asset, "  Start", [i](auto& a) -> auto& { return a.audioTracks[i].startSeconds; });
        Field(rows, asset, "  End", [i](auto& a) -> auto& { return a.audioTracks[i].endSeconds; });
        Field(rows, asset, "  Clip offset", [i](auto& a) -> auto& { return a.audioTracks[i].clipOffsetSeconds; });
        Field(rows, asset, "  Volume", [i](auto& a) -> auto& { return a.audioTracks[i].volume; });
        Field(rows, asset, "  Spatial", [i](auto& a) -> auto& { return a.audioTracks[i].spatial; });
        Field(rows, asset, "  Output bus", [i](auto& a) -> auto& { return a.audioTracks[i].outputBus; });
    }
    for (std::size_t i = 0; i < asset.animationTracks.size(); ++i) {
        Header(rows, "Animation track", i, [](auto& a) -> auto& { return a.animationTracks; });
        Field(rows, asset, "  Binding", [i](auto& a) -> auto& { return a.animationTracks[i].binding; });
        Field(rows, asset, "  Clip", [i](auto& a) -> auto& { return a.animationTracks[i].clipReference; });
        Field(rows, asset, "  Start", [i](auto& a) -> auto& { return a.animationTracks[i].startSeconds; });
        Field(rows, asset, "  End", [i](auto& a) -> auto& { return a.animationTracks[i].endSeconds; });
        Field(rows, asset, "  Source offset", [i](auto& a) -> auto& { return a.animationTracks[i].sourceOffsetSeconds; });
        Field(rows, asset, "  Rate", [i](auto& a) -> auto& { return a.animationTracks[i].rate; });
    }
    for (std::size_t i = 0; i < asset.sequenceTracks.size(); ++i) {
        Header(rows, "Nested sequence", i, [](auto& a) -> auto& { return a.sequenceTracks; });
        Field(rows, asset, "  Sequence", [i](auto& a) -> auto& { return a.sequenceTracks[i].sequenceReference; });
        Field(rows, asset, "  Start", [i](auto& a) -> auto& { return a.sequenceTracks[i].startSeconds; });
        Field(rows, asset, "  End", [i](auto& a) -> auto& { return a.sequenceTracks[i].endSeconds; });
        Field(rows, asset, "  Source offset", [i](auto& a) -> auto& { return a.sequenceTracks[i].sourceOffsetSeconds; });
        Field(rows, asset, "  Rate", [i](auto& a) -> auto& { return a.sequenceTracks[i].rate; });
        for (std::size_t b = 0; b < asset.sequenceTracks[i].bindings.size(); ++b) {
            Field(rows, asset, "  Child binding", [i,b](auto& a) -> auto& { return a.sequenceTracks[i].bindings[b].child; });
            Field(rows, asset, "  Parent binding", [i,b](auto& a) -> auto& { return a.sequenceTracks[i].bindings[b].parent; });
        }
    }
    return rows;
}

bool TimelineEditorModel::Add(Asset& asset, TimelineEditorTrackKind kind, const std::string& binding,
    const std::string& reference, const kb::ecs::World& world, const Asset* child) {
    using namespace kb::scene;
    if (kind == TimelineEditorTrackKind::Binding) { asset.bindings.push_back({binding, reference}); return true; }
    if (kind == TimelineEditorTrackKind::Marker) { asset.markers.push_back({asset.durationSeconds, asset.markers.size() + 1}); return true; }
    if (kind == TimelineEditorTrackKind::Transform) { asset.transformTracks.push_back({binding, {{0, {}}}}); return true; }
    if (kind == TimelineEditorTrackKind::Event) { asset.eventTracks.push_back({binding, {{asset.durationSeconds * 0.5F,1,"Event",{}}}}); return true; }
    if (kind == TimelineEditorTrackKind::Audio) { asset.audioTracks.push_back({binding, reference, 0, std::min(1.0F, asset.durationSeconds)}); return true; }
    if (kind == TimelineEditorTrackKind::Animation) { asset.animationTracks.push_back({binding, reference, 0, std::min(1.0F, asset.durationSeconds)}); return true; }
    if (kind == TimelineEditorTrackKind::Sequence) {
        if (!child) return false;
        TimelineSequenceTrack track{reference, 0, std::min(asset.durationSeconds, child->durationSeconds)};
        for (const auto& item : child->bindings) track.bindings.push_back({item.name, binding});
        asset.sequenceTracks.push_back(std::move(track)); return true;
    }
    const auto dot = reference.rfind('.');
    if (dot == std::string::npos) return false;
    const auto component = reference.substr(0, dot);
    const auto* reflection = world.Reflection(component);
    if (!reflection && component == "Light") reflection = world.Reflection<kb::scene::LightComponent>();
    if (!reflection && component == "UICanvasGroup") reflection = world.Reflection<kb::scene::UICanvasGroup>();
    const auto* field = reflection ? reflection->FindField(reference.substr(dot + 1)) : nullptr;
    if (!field) return false;
    using Type = kb::ecs::ComponentFieldType;
    kb::ecs::ComponentFieldValue value;
    switch (field->type) {
    case Type::Bool: value = false; break;
    case Type::Enum32: case Type::Int32: value = std::int32_t{}; break;
    case Type::UInt32: value = std::uint32_t{}; break;
    case Type::Float32: value = float{}; break;
    case Type::Float64: value = double{}; break;
    case Type::Vec2Float32: value = std::array<float,2>{}; break;
    case Type::Vec3Float32: value = std::array<float,3>{}; break;
    case Type::Vec4Float32: value = std::array<float,4>{}; break;
    default: return false;
    }
    const bool discrete = field->type == Type::Bool || field->type == Type::Int32 || field->type == Type::UInt32 || field->type == Type::Enum32;
    asset.propertyTracks.push_back({binding, {1, std::string{reflection->Name()}, field->name, field->type, TimelineComponentSchema(*reflection)},
        discrete ? TimelinePropertyInterpolation::Step : TimelinePropertyInterpolation::Linear, {{0, std::move(value)}}});
    return true;
}
} // namespace kb::editor
