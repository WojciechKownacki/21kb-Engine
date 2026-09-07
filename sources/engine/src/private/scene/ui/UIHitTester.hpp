#pragma once

#include "engine/scene/UIPresentation.hpp"

namespace kb::scene {

class UIHitTester final {
  public:
    UIHitTester() = delete;
    [[nodiscard]] static const UIPresentationHitTarget* Topmost(const UIPresentationSnapshot& snapshot, float x,
                                                                float y) noexcept;
};

} // namespace kb::scene
