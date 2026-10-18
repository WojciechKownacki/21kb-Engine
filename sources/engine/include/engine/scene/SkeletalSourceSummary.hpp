#pragma once

#include <cstddef>

namespace kb::scene {

// What a model source holds, read without importing it: enough to choose between a static and
// a skeletal import, and to say why a source cannot be either.
struct SkeletalSourceSummary {
    bool readable = false;
    // A mesh bound to a skeleton: a character or a creature.
    bool skinned = false;
    // Bones of the source's armature (glTF skin joints, FBX bone nodes, or the nodes a
    // mesh-less source animates).
    std::size_t boneCount = 0U;
    std::size_t animationCount = 0U;

    // A skeleton with animations and no mesh: a file of animations for a character imported
    // from another file.
    [[nodiscard]] bool AnimationsOnly() const noexcept {
        return readable && !skinned && boneCount != 0U && animationCount != 0U;
    }
    [[nodiscard]] bool Skeletal() const noexcept { return readable && (skinned || AnimationsOnly()); }
};

} // namespace kb::scene
