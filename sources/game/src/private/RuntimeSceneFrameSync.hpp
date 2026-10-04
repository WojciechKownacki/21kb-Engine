#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/Renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kb::game {

// Preserve proxy changes queued before Update, which consumes the runtime queue.
class RuntimeSceneFrameSync {
public:
    void BeforeUpdate(const kb::scene::Scene& scene) {
        preUpdateChanges_.clear();
        if (initialized_ && scene.Runtime().RenderProxyUpdateRevision() != synchronizedProxyRevision_) {
            for (const auto entity : scene.Runtime().RenderProxyUpdateEntities()) {
                preUpdateChanges_.push_back(entity.Id());
            }
        }
    }

    [[nodiscard]] bool Submit(kb::scene::Scene& scene, kb::render::Renderer& renderer) {
        const std::uint64_t topology = scene.Runtime().RenderTopologyVersion();
        const std::uint64_t proxyRevision = scene.Runtime().RenderProxyUpdateRevision();
        const kb::scene::SceneHierarchyQueries hierarchy{ scene };
        const std::size_t rootCount = hierarchy.RootCount();
        const std::uint64_t rootAppendEpoch = hierarchy.RootAppendEpoch();
        // Each fresh root changes the topology once and leaves every existing row alone: the renderer reconciles
        // just the appended roots instead of every proxy of the scene.
        const std::size_t appendedRootCount = rootCount > synchronizedRootCount_ ? rootCount - synchronizedRootCount_ : 0U;
        const bool appendOnly = initialized_ && rootAppendEpoch == synchronizedRootAppendEpoch_ && appendedRootCount != 0U &&
            topology > synchronizedTopology_ && topology - synchronizedTopology_ == appendedRootCount;
        if (appendOnly) {
            for (std::size_t index = synchronizedRootCount_; index < rootCount; ++index) {
                preUpdateChanges_.push_back(hierarchy.RootAt(index).Id());
            }
        }
        if (!renderer.SubmitRuntimeScene(scene, {
                .fullSync = !initialized_,
                .dirtySceneEntityIds = preUpdateChanges_,
                .structuralSync = initialized_ && topology != synchronizedTopology_ && !appendOnly,
            })) {
            return false;
        }
        initialized_ = true;
        synchronizedTopology_ = topology;
        synchronizedProxyRevision_ = proxyRevision;
        synchronizedRootCount_ = rootCount;
        synchronizedRootAppendEpoch_ = rootAppendEpoch;
        return true;
    }

    void Reset() noexcept {
        initialized_ = false;
        synchronizedTopology_ = 0U;
        synchronizedProxyRevision_ = 0U;
        synchronizedRootCount_ = 0U;
        synchronizedRootAppendEpoch_ = 0U;
        preUpdateChanges_.clear();
    }

private:
    bool initialized_ = false;
    std::uint64_t synchronizedTopology_ = 0U;
    std::uint64_t synchronizedProxyRevision_ = 0U;
    std::size_t synchronizedRootCount_ = 0U;
    std::uint64_t synchronizedRootAppendEpoch_ = 0U;
    std::vector<std::uint64_t> preUpdateChanges_;
};

} // namespace kb::game
