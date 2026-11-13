#pragma once

#include <cstdint>

namespace kb::scene {

class Scene;

// How much pose work a character costs while the renderer's last frame shows it small or not at all.
// It applies to skeletal Animators whose Pose Rate is 0 and only once a frame has been rendered; playheads,
// transitions, events and root motion keep running every tick, only the visual pose is sampled less often.
// Intervals count frames between two pose evaluations (1 = every frame); they are staggered per character
// so that they do not all land on the same frame, and a shown pose that is evaluated every 2nd or 3rd frame
// moves toward each new pose on the frames between.
struct AnimatorPoseRatePolicy {
    bool enabled = true;
    // Share of the view height the character's bounds fill: at or above this it is posed every frame,
    float fullRateCoverage = 0.25F;
    // at or above this every 2nd frame, at or above this every 3rd, below it every 4th.
    float halfRateCoverage = 0.10F;
    float thirdRateCoverage = 0.04F;
    // Frames between poses while no view shows the character (0 = not posed until it is seen again).
    std::uint32_t offScreenFrameInterval = 4U;
};

class SceneAnimatorPoseRate {
public:
    SceneAnimatorPoseRate() = delete;
    // Throws std::invalid_argument for coverages that are not finite or do not decrease from full to third rate.
    static void SetPolicy(Scene& scene, const AnimatorPoseRatePolicy& policy);
    [[nodiscard]] static AnimatorPoseRatePolicy Policy(const Scene& scene) noexcept;
};

} // namespace kb::scene
