#include "scene/EditorGameView.hpp"

#include "localization/EditorLocalization.hpp"

#include <cstdio>

namespace kb::editor {
namespace {

struct Entry {
    EditorGameViewResolution resolution;
    std::string_view name;
    EditorGameViewSize size;
};

constexpr Entry kEntries[] = {
    { EditorGameViewResolution::FreeAspect, "FreeAspect", { 0U, 0U } },
    { EditorGameViewResolution::ProjectResolution, "Project", { 0U, 0U } },
    { EditorGameViewResolution::Landscape16x9, "Landscape16x9", { 1920U, 1080U } },
    { EditorGameViewResolution::Portrait9x16, "Portrait9x16", { 1080U, 1920U } },
    { EditorGameViewResolution::PhoneLandscape, "PhoneLandscape", { 2340U, 1080U } },
    { EditorGameViewResolution::Tablet4x3, "Tablet4x3", { 2048U, 1536U } },
    { EditorGameViewResolution::Tablet16x10, "Tablet16x10", { 1920U, 1200U } },
};

[[nodiscard]] const Entry& EntryFor(EditorGameViewResolution resolution) noexcept {
    for (const Entry& entry : kEntries) {
        if (entry.resolution == resolution) {
            return entry;
        }
    }
    return kEntries[0];
}

[[nodiscard]] std::string_view Title(EditorGameViewResolution resolution) {
    switch (resolution) {
    case EditorGameViewResolution::FreeAspect:
        return EditorText("gameView.resolution.free", "Free Aspect");
    case EditorGameViewResolution::ProjectResolution:
        return EditorText("gameView.resolution.project", "Project Resolution");
    case EditorGameViewResolution::Landscape16x9:
        return EditorText("gameView.resolution.landscape16x9", "16:9 Landscape");
    case EditorGameViewResolution::Portrait9x16:
        return EditorText("gameView.resolution.portrait9x16", "9:16 Phone Portrait");
    case EditorGameViewResolution::PhoneLandscape:
        return EditorText("gameView.resolution.phoneLandscape", "19.5:9 Phone Landscape");
    case EditorGameViewResolution::Tablet4x3:
        return EditorText("gameView.resolution.tablet4x3", "4:3 Tablet");
    case EditorGameViewResolution::Tablet16x10:
        return EditorText("gameView.resolution.tablet16x10", "16:10 Tablet");
    }
    return {};
}

} // namespace

EditorGameViewSize EditorGameView::Size(EditorGameViewResolution resolution, EditorGameViewSize project) noexcept {
    if (resolution == EditorGameViewResolution::ProjectResolution) {
        return project;
    }
    return EntryFor(resolution).size;
}

std::string_view EditorGameView::Name(EditorGameViewResolution resolution) noexcept {
    return EntryFor(resolution).name;
}

std::optional<EditorGameViewResolution> EditorGameView::FromName(std::string_view name) noexcept {
    for (const Entry& entry : kEntries) {
        if (entry.name == name) {
            return entry.resolution;
        }
    }
    return std::nullopt;
}

std::string EditorGameView::Label(EditorGameViewResolution resolution, EditorGameViewSize project) {
    std::string label{ Title(resolution) };
    const EditorGameViewSize size = Size(resolution, project);
    if (!size.FollowsPanel()) {
        label += " (" + std::to_string(size.width) + " x " + std::to_string(size.height) + ")";
    }
    return label;
}

std::string EditorGameView::FormatScale(std::uint32_t shownWidth, std::uint32_t renderWidth) {
    const double scale = renderWidth == 0U ? 1.0 : static_cast<double>(shownWidth) / static_cast<double>(renderWidth);
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.2fx", scale);
    return text;
}

} // namespace kb::editor
