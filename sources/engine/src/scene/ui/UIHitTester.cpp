#include "scene/ui/UIHitTester.hpp"

#include <cmath>

namespace kb::scene {
namespace {

[[nodiscard]] bool ContainsTransformed(const UIPresentationHitTarget& target, float x, float y) noexcept {
    if (!target.clipRect.Contains(x, y) || target.scale.x == 0.0F || target.scale.y == 0.0F) {
        return false;
    }
    const float pivotX = target.rect.left + target.rect.Width() * target.pivot.x;
    const float pivotY = target.rect.top + target.rect.Height() * target.pivot.y;
    const float radians = -target.rotationDegrees * 0.01745329251994329577F;
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    const float translatedX = x - pivotX;
    const float translatedY = y - pivotY;
    const float localX = (translatedX * cosine - translatedY * sine) / target.scale.x + pivotX;
    const float localY = (translatedX * sine + translatedY * cosine) / target.scale.y + pivotY;
    return target.rect.Contains(localX, localY);
}

} // namespace

const UIPresentationHitTarget* UIHitTester::Topmost(const UIPresentationSnapshot& snapshot, float x, float y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y))
        return nullptr;
    for (auto it = snapshot.hitTargets.rbegin(); it != snapshot.hitTargets.rend(); ++it) {
        if (ContainsTransformed(*it, x, y))
            return &*it;
    }
    return nullptr;
}

} // namespace kb::scene
