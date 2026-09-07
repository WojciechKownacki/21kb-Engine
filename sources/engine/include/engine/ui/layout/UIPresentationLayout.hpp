#pragma once

#include "engine/scene/UIPresentation.hpp"

#include <cstdint>
#include <span>

namespace kb::scene {

struct UIPresentationDocumentView {
    SceneEntity owner{};
    std::span<const UIDocumentElement* const> elements;
};

// Canonical retained-UI layout boundary shared by runtime presentation,
// rendering, hit testing and editor preview. It produces derived data only;
// UIDocument remains the sole authored tree.
class UIPresentationLayout final {
  public:
    UIPresentationLayout() = delete;

    static void Build(std::span<const UIPresentationDocumentView> documents, std::uint32_t viewportWidth,
                      std::uint32_t viewportHeight, UIPresentationSnapshot& output);

    static void Build(const UIDocument& document, std::uint32_t viewportWidth, std::uint32_t viewportHeight,
                      UIPresentationSnapshot& output);
};

} // namespace kb::scene
