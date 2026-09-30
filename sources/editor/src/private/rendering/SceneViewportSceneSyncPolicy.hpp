#pragma once

#include <cstdint>

namespace kb::editor {

struct SceneViewportSceneSyncDecision {
    bool fullSync = false;
    bool incrementalEntitySync = false;
    bool runtimeTransformSync = false;
    bool structuralSync = false;
};

class SceneViewportSceneSyncPolicy {
public:
    [[nodiscard]] static constexpr SceneViewportSceneSyncDecision Resolve(
        std::uint64_t submittedRevision,
        std::uint64_t sceneRevision,
        std::uint64_t dirtyBaseRevision,
        bool fullSyncRequested,
        bool structuralSyncRequested,
        bool hasDirtyEntities,
        bool runtimeTransformSyncRequested) noexcept {
        const bool sceneChanged = submittedRevision != sceneRevision;
        const bool revisionCovered = submittedRevision >= dirtyBaseRevision;
        const bool incrementalEntitySync = sceneChanged &&
            !fullSyncRequested &&
            hasDirtyEntities &&
            revisionCovered;
        const bool structuralSync = sceneChanged && !fullSyncRequested &&
            structuralSyncRequested && revisionCovered;
        const bool fullSync = submittedRevision == 0U ||
            (sceneChanged && !incrementalEntitySync && !structuralSync);
        return SceneViewportSceneSyncDecision{
            .fullSync = fullSync,
            .incrementalEntitySync = incrementalEntitySync,
            .runtimeTransformSync = runtimeTransformSyncRequested && !fullSync,
            .structuralSync = structuralSync && !fullSync,
        };
    }
};

} // namespace kb::editor
