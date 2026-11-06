#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include <bgfx/bgfx.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace kb::render {

// Optional, delayed GPU sample counts for depth-independent scene replays. The result
// excludes UI, sky, shadow and post-process draws and is not a shader
// invocation counter. A full query set is required before publishing a sample.
class SceneMeshCoverageProbe {
public:
    void SetEnabled(bool enabled) noexcept {
        if (enabled_ != enabled) {
            last_ = {};
            current_.incomplete = true;
            for (auto& frame : pending_) frame.incomplete = true;
        }
        enabled_ = enabled;
    }
    void BeginFrame(std::uint64_t frame) {
        Poll();
        for (auto& query : current_.queries) if (bgfx::isValid(query.handle)) bgfx::destroy(query.handle);
        current_ = Frame{};
        current_.frame = frame;
        const auto* caps = bgfx::getCaps();
        active_ = enabled_ && caps && caps->limits.maxOcclusionQueries > 0U && pending_.size() < 8U;
        limit_ = caps ? std::min<std::uint32_t>(caps->limits.maxOcclusionQueries, 1024U) : 0U;
    }
    void BeginPass(MeshPassType pass, std::uint32_t width, std::uint32_t height, bgfx::ViewId view) {
        eligible_ = active_ && (pass == MeshPassType::BaseOpaque || pass == MeshPassType::GBuffer ||
                                pass == MeshPassType::BaseTransparent);
        if (!eligible_) return;
        const auto slot = pass == MeshPassType::BaseOpaque ? 0U : pass == MeshPassType::GBuffer ? 1U : 2U;
        if (current_.views[slot] != bgfx::kInvalidHandle && current_.views[slot] != view) {
            current_.incomplete = true;
            eligible_ = false;
            return;
        }
        current_.views[slot] = view;
        if (width == 0 || height == 0 || (current_.width != 0 && (current_.width != width || current_.height != height))) {
            current_.incomplete = true;
            eligible_ = false;
            return;
        }
        current_.width = width;
        current_.height = height;
    }
    bgfx::OcclusionQueryHandle NextQuery(bool particle = false) noexcept {
        if (!eligible_) return BGFX_INVALID_HANDLE;
        if (current_.queries.size() >= limit_) { current_.incomplete = true; return BGFX_INVALID_HANDLE; }
        const auto query = bgfx::createOcclusionQuery();
        if (!bgfx::isValid(query)) { current_.incomplete = true; return BGFX_INVALID_HANDLE; }
        try { current_.queries.push_back({query, false, 0U, particle}); }
        catch (...) { bgfx::destroy(query); current_.incomplete = true; return BGFX_INVALID_HANDLE; }
        return query;
    }
    void EndFrame() {
        if (active_ && current_.width != 0U) pending_.push_back(std::move(current_));
        active_ = false;
        eligible_ = false;
        Poll();
    }
    [[nodiscard]] SceneMeshCoverageMetric Last() const noexcept { return enabled_ ? last_ : SceneMeshCoverageMetric{}; }
    void Shutdown() noexcept {
        for (auto& query : current_.queries) if (bgfx::isValid(query.handle)) bgfx::destroy(query.handle);
        for (auto& frame : pending_) for (auto& query : frame.queries) if (bgfx::isValid(query.handle)) bgfx::destroy(query.handle);
        current_ = Frame{};
        pending_.clear();
        last_ = {};
        active_ = false;
    }
private:
    struct Query { bgfx::OcclusionQueryHandle handle = BGFX_INVALID_HANDLE; bool ready = false; std::uint64_t samples = 0U; bool particle = false; };
    struct Frame {
        std::uint64_t frame = 0U;
        std::uint32_t width = 0U, height = 0U;
        bool incomplete = false;
        std::array<std::uint16_t, 3> views{bgfx::kInvalidHandle, bgfx::kInvalidHandle, bgfx::kInvalidHandle};
        std::vector<Query> queries;
    };
    void Poll() {
        for (auto it = pending_.begin(); it != pending_.end();) {
            bool complete = true;
            std::uint64_t samples = 0U;
            std::uint64_t particleSamples = 0U;
            std::uint32_t particleDraws = 0U;
            for (auto& query : it->queries) {
                if (!query.ready) {
                    std::int32_t count = 0;
                    const auto result = bgfx::getResult(query.handle, &count);
                    if (result != bgfx::OcclusionQueryResult::NoResult) {
                        if (count < 0) it->incomplete = true;
                        else query.samples = static_cast<std::uint64_t>(count);
                        query.ready = true;
                        bgfx::destroy(query.handle);
                        query.handle = BGFX_INVALID_HANDLE;
                    }
                }
                complete = complete && query.ready;
                if (query.particle) { particleSamples += query.samples; ++particleDraws; }
                else samples += query.samples;
            }
            if (!complete) { ++it; continue; }
            if (!it->incomplete && (!last_.available || it->frame > last_.frame))
                last_ = {true, it->frame, samples, particleSamples, it->width, it->height,
                    static_cast<std::uint32_t>(it->queries.size()) - particleDraws, particleDraws};
            it = pending_.erase(it);
        }
    }
    std::vector<Frame> pending_;
    Frame current_;
    SceneMeshCoverageMetric last_{};
    std::uint32_t limit_ = 0U;
    bool enabled_ = false, active_ = false, eligible_ = false;
};

inline void SubmitWithSceneCoverage(SceneMeshCoverageProbe* probe, bgfx::ViewId view, bgfx::ProgramHandle program,
    std::uint64_t state, bool particle) noexcept {
    const auto query = probe ? probe->NextQuery(particle) : bgfx::OcclusionQueryHandle{bgfx::kInvalidHandle};
    if (bgfx::isValid(query)) {
        const auto diagnosticState = (state & ~(BGFX_STATE_WRITE_MASK | BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_BLEND_MASK)) |
            BGFX_STATE_DEPTH_TEST_ALWAYS;
        bgfx::setState(diagnosticState);
        bgfx::submit(view, program, query, 0, BGFX_DISCARD_NONE);
    }
    bgfx::setState(state);
    bgfx::submit(view, program);
}

} // namespace kb::render
