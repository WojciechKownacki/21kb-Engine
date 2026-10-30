#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/JobGraph.hpp"
#include "engine/ecs/System.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include <atomic>
#include <memory>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

extern "C" __declspec(dllexport) void GraphModuleRunChunks(kb::ecs::WorkerPool* pool, std::atomic<int>* count) {
    pool->ParallelForChunks(20, 2, [count](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        count->fetch_add(static_cast<int>(chunk.count));
    });
}

extern "C" __declspec(dllexport) bool GraphModuleRejectNestedCalls(kb::ecs::WorkerPool* pool) {
    bool submitRejected = false;
    bool graphRejected = false;
    try { static_cast<void>(pool->Submit({})); }
    catch (const std::logic_error&) { submitRejected = true; }
    try { static_cast<void>(pool->RunGraph(kb::ecs::JobGraph{}, {})); }
    catch (const std::logic_error&) { graphRejected = true; }
    return submitRejected && graphRejected;
}

namespace {
class GraphFixtureSystem final : public kb::ecs::System {
public:
    explicit GraphFixtureSystem(std::shared_ptr<std::atomic<int>> value) : value_(std::move(value)) {}
    std::string_view Name() const noexcept override { return "GraphFixtureSystem"; }
    kb::ecs::SystemAccess DeclareAccess(kb::ecs::World&) const override { return {}; }
    void SetExecutionWorkerPool(kb::ecs::WorkerPool* pool) noexcept override { pool_ = pool; }
    void OnUpdate(kb::ecs::World&, float) override {
        if (!pool_) throw std::runtime_error("fixture requires the graph worker pool");
        std::atomic<int> count{0};
        GraphModuleRunChunks(pool_, &count);
        if (count != 20) throw std::runtime_error("fixture nested work produced an invalid result");
        char* marker = nullptr;
        std::size_t markerSize = 0;
        if (_dupenv_s(&marker, &markerSize, "KB_GRAPH_FIXTURE_MARKER") == 0 && marker && *marker) {
            std::ofstream output{marker, std::ios::app};
            output << count.load() << '\n';
        }
        std::free(marker);
        value_->store(count.load());
        ReportProfilerJobs(10);
    }
private:
    kb::ecs::WorkerPool* pool_ = nullptr;
    std::shared_ptr<std::atomic<int>> value_;
};
class GraphFixtureConsumer final : public kb::ecs::System {
public:
    explicit GraphFixtureConsumer(std::shared_ptr<std::atomic<int>> value) : value_(std::move(value)) {}
    std::string_view Name() const noexcept override { return "GraphFixtureConsumer"; }
    kb::ecs::SystemAccess DeclareAccess(kb::ecs::World&) const override {
        kb::ecs::SystemAccess access;
        access.After("GraphFixtureSystem");
        return access;
    }
    void OnUpdate(kb::ecs::World&, float) override {
        if (value_->exchange(0) != 20) throw std::runtime_error("fixture dependency ran before its producer");
    }
private:
    std::shared_ptr<std::atomic<int>> value_;
};
class GraphFixtureModule final : public kb::modules::IEngineModule {
public:
    kb::modules::EngineModuleMetadata Metadata() const override {
        return {"Tests.GraphFixture", 1U, {}, kb::modules::EngineModuleLoadingPhase::Default};
    }
    void OnSceneAttach(kb::scene::Scene& scene) override {
        auto runtime = scene.Runtime();
        runtime.SetEcsParallelismMode(kb::ecs::SystemSchedulerParallelismMode::SystemsAndChunks);
        auto value = std::make_shared<std::atomic<int>>(0);
        runtime.AddSystem(std::make_unique<GraphFixtureSystem>(value));
        runtime.AddSystem(std::make_unique<GraphFixtureConsumer>(value));
    }
};
}

#ifdef KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
#endif
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() { return kb::modules::kEngineModuleAbiVersion; }
extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() { return "Tests.GraphFixture"; }
extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() { return new GraphFixtureModule(); }
extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) { delete module; }
