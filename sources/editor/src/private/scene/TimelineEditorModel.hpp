#pragma once

#include "engine/scene/TimelineAsset.hpp"
#include <functional>
#include <string>
#include <vector>

namespace kb::ecs { class World; }
namespace kb::editor {

// Rows contain indices, never references into a document's undo history.
struct TimelineEditorRow {
    std::string label, value;
    std::function<bool(kb::scene::TimelineAsset&, const std::string&)> edit;
    std::function<bool(kb::scene::TimelineAsset&)> remove;
    std::function<bool(kb::scene::TimelineAsset&, float)> addKey;
};
enum class TimelineEditorTrackKind { Binding, Transform, Property, Audio, Event, Sequence, Animation, Marker };
class TimelineEditorModel {
public:
    [[nodiscard]] static std::vector<TimelineEditorRow> Rows(const kb::scene::TimelineAsset& asset);
    [[nodiscard]] static bool Add(kb::scene::TimelineAsset& asset, TimelineEditorTrackKind kind,
        const std::string& binding, const std::string& reference, const kb::ecs::World& world,
        const kb::scene::TimelineAsset* child = nullptr);
};
} // namespace kb::editor
