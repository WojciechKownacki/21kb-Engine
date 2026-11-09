#pragma once

#include "engine/core/JsonValue.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::editor {

class EditorDockModel;
class EditorHeadlessAutomation;

// The profiler's steps of a headless scenario: choose what it shows, feed it frames, ask what it
// measured, click its window and capture it. They go through the same selection, sampler, registry
// and window the editor uses.
class EditorProfilerAutomation final {
public:
    EditorProfilerAutomation() = delete;

    // The outcome (succeeded, what happened) of a profiler step, or nothing for any other step.
    [[nodiscard]] static std::optional<std::pair<bool, std::string>> Run(std::string_view operation,
        const kb::core::JsonValue& step, EditorSceneContext& context, EditorHeadlessAutomation& automation, EditorDockModel& dock);
};

} // namespace kb::editor
