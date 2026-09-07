#pragma once

#include "engine/scene/UIPresentation.hpp"

#include <cstdint>

namespace kb::scene {

class SceneState;

class UIPresentationBuilder final {
  public:
    UIPresentationBuilder() = delete;
    static void Build(SceneState& state, std::uint32_t viewportWidth, std::uint32_t viewportHeight);
};

} // namespace kb::scene
