#pragma once

#include "inspection/InspectorSceneAudioModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

struct InspectorSceneAudioHit {
    InspectorHitKind kind = InspectorHitKind::None;
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPropertyId property = InspectorPropertyId::None;
    int index = -1;
#if defined(_WIN32)
    RECT rect{};
#endif
};

class InspectorSceneAudioView {
public:
    InspectorSceneAudioView() = delete;

    [[nodiscard]] static int ContentHeight(
        const InspectorPanelState& state,
        const InspectorSceneAudioModel& model);
    [[nodiscard]] static bool IsRowEditing(
        const InspectorPanelState& state,
        std::uint64_t documentGeneration,
        const InspectorSceneAudioRow& row) noexcept;

#if defined(_WIN32)
    static void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const InspectorPanelState& state,
        std::uint64_t documentGeneration,
        const InspectorSceneAudioModel& model);
    [[nodiscard]] static InspectorSceneAudioHit HitTest(
        const RECT& content,
        const InspectorPanelState& state,
        const InspectorSceneAudioModel& model,
        int x,
        int y);
    [[nodiscard]] static std::optional<RECT> RowBounds(
        const RECT& content,
        const InspectorPanelState& state,
        const InspectorSceneAudioModel& model,
        int flatIndex);
#endif
};

} // namespace kb::editor
