#pragma once

#include <cstdint>

namespace kb::editor {

struct SceneViewportSceneSyncDecision {
    bool fullSync = false;
    bool incrementalEntitySync = false;
    bool runtimeTransformSync = false;
};

class SceneViewportSceneSyncPolicy {
public:
    [[nodiscard]] static constexpr SceneViewportSceneSyncDecision Resolve(
        std::uint64_t submittedRevision,
        std::uint64_t sceneRevision,
        std::uint64_t dirtyBaseRevision,
        bool fullSyncRequested,
        bool hasDirtyEntities,
        bool runtimeTransformSyncRequested) noexcept {
        const bool sceneChanged = submittedRevision != sceneRevision;
        const bool incrementalEntitySync = sceneChanged &&
            !fullSyncRequested &&
            hasDirtyEntities &&
            submittedRevision >= dirtyBaseRevision;
        const bool fullSync = submittedRevision == 0U ||
            (sceneChanged && !incrementalEntitySync);
        return SceneViewportSceneSyncDecision{
            .fullSync = fullSync,
            .incrementalEntitySync = incrementalEntitySync,
            .runtimeTransformSync = runtimeTransformSyncRequested && !fullSync,
        };
    }
};

} // namespace kb::editor
