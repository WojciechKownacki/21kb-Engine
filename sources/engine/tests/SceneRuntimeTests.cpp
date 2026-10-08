#include "ScenePrefabTestSuites.hpp"
#include "EcsTestSuites.hpp"
#include "SceneSystemTestSuites.hpp"
#include "TestSuites.hpp"
#include "TestSupport.hpp"

#include <atomic>
#include <cstdlib>
#include <new>
#include <string_view>

namespace kb::tests {
void RunScriptNativeHeaderReloadTest();
void RunPhysicsReplayOnlyTest();
void RunPhysicsStepSpikeBenchmark();
void RunAgentsFrameBenchmark();
}

namespace {

bool RunSuite(std::string_view suite) {
    if (suite == "assets") {
        kb::tests::RunAssetRuntimeTests();
    } else if (suite == "asset-bake") {
        kb::tests::RunAssetBakeTests();
    } else if (suite == "asset-pack") {
        kb::tests::RunAssetPackTests();
    } else if (suite == "save") {
        kb::tests::RunSaveGameTests();
    } else if (suite == "security") {
        kb::tests::RunSecurityTests();
    } else if (suite == "ecs") {
        kb::tests::RunEcsRuntimeTests();
    } else if (suite == "ecs-native") {
        kb::tests::RunEcsNativeArchetypeStorageTests();
    } else if (suite == "scene-hierarchy") {
        kb::tests::RunSceneHierarchyTests();
    } else if (suite == "scene-transform-bench") {
        kb::tests::RunTransformWriteBenchmark();
    } else if (suite == "scene-ui") {
        kb::tests::RunSceneUITests();
    } else if (suite == "scene-ui-bench") {
        kb::tests::RunSceneUIBuildFrameBenchmark();
    } else if (suite == "scene-system") {
        kb::tests::RunSceneSystemTests();
    } else if (suite == "physics-replay") {
        kb::tests::RunPhysicsReplayOnlyTest();
    } else if (suite == "physics-step-spikes") {
        kb::tests::RunPhysicsStepSpikeBenchmark();
    } else if (suite == "agents-frame") {
        kb::tests::RunAgentsFrameBenchmark();
    } else if (suite == "audio") {
        kb::tests::RunAudioSceneSystemTests();
    } else if (suite == "scene-runtime") {
        kb::tests::RunSceneSystemTransformSyncTests();
    } else if (suite == "scene-prefab") {
        kb::tests::RunScenePrefabTests();
    } else if (suite == "scene-prefab-instantiation") {
        kb::tests::RunScenePrefabInstantiationTests();
    } else if (suite == "scene-prefab-capture") {
        kb::tests::RunScenePrefabCaptureTests();
    } else if (suite == "project-scene") {
        kb::tests::RunProjectSceneTests();
    } else if (suite == "project-scene-transition-bench") {
        kb::tests::RunLargeNonAdditiveSceneTransitionBenchmark();
    } else if (suite == "script") {
        kb::tests::RunScriptRuntimeTests();
    } else if (suite == "script-native-header") {
        kb::tests::RunScriptNativeHeaderReloadTest();
    } else if (suite == "script-api") {
        kb::tests::RunScriptApiCatalogTests();
    } else if (suite == "visual-graph") {
        kb::tests::RunVisualGraphTests();
    } else if (suite == "input") {
        kb::tests::RunInputTests();
    } else if (suite == "engine-module") {
        kb::tests::RunEngineModuleTests();
    } else if (suite == "engine-library") {
        kb::tests::RunEngineLibraryTests();
    } else if (suite == "engine-math") {
        kb::tests::RunEngineMathTests();
    } else if (suite == "animation-runtime") {
        kb::tests::RunAnimationRuntimeTests();
    } else if (suite == "skeleton-assets") {
        kb::tests::RunSkeletonAssetTests();
    } else if (suite == "skeletal-mesh-assets") {
        kb::tests::RunSkeletalMeshAssetTests();
    } else if (suite == "timeline-runtime") {
        kb::tests::RunTimelineRuntimeTests();
    } else if (suite == "localization") {
        kb::tests::RunLocalizationTests();
    } else if (suite == "ui-text-layout") {
        kb::tests::RunUITextLineBreakingTests();
    } else if (suite == "crash-consent") {
        kb::tests::RunCrashReportConsentTests();
    } else {
        return false;
    }
    return true;
}

void RunAllSuites() {
    kb::tests::RunAssetRuntimeTests();
    kb::tests::RunAssetBakeTests();
    kb::tests::RunAssetPackTests();
    kb::tests::RunSaveGameTests();
    kb::tests::RunSecurityTests();
    kb::tests::RunEcsRuntimeTests();
    kb::tests::RunSceneHierarchyTests();
    kb::tests::RunSceneUITests();
    kb::tests::RunSceneSystemTests();
    kb::tests::RunScenePrefabTests();
    kb::tests::RunProjectSceneTests();
    kb::tests::RunScriptRuntimeTests();
    kb::tests::RunScriptApiCatalogTests();
    kb::tests::RunVisualGraphTests();
    kb::tests::RunInputTests();
    kb::tests::RunEngineModuleTests();
    kb::tests::RunEngineLibraryTests();
    kb::tests::RunEngineMathTests();
    kb::tests::RunAnimationRuntimeTests();
    kb::tests::RunSkeletonAssetTests();
    kb::tests::RunSkeletalMeshAssetTests();
    kb::tests::RunTimelineRuntimeTests();
    kb::tests::RunLocalizationTests();
    kb::tests::RunCrashReportConsentTests();
}

} // namespace

namespace {

std::atomic<bool> g_tallyAllocations{ false };
std::atomic<std::size_t> g_allocationCount{ 0U };
std::atomic<std::size_t> g_allocationBytes{ 0U };
std::atomic<std::size_t> g_largeAllocationCount{ 0U };

} // namespace

void kb::tests::BeginAllocationTally() noexcept {
    g_allocationCount.store(0U);
    g_allocationBytes.store(0U);
    g_largeAllocationCount.store(0U);
    g_tallyAllocations.store(true);
}

kb::tests::AllocationTally kb::tests::EndAllocationTally() noexcept {
    g_tallyAllocations.store(false);
    return AllocationTally{ .count = g_allocationCount.load(), .bytes = g_allocationBytes.load(), .largeCount = g_largeAllocationCount.load() };
}

void* operator new(std::size_t size) {
    if (g_tallyAllocations.load(std::memory_order_relaxed)) {
        g_allocationCount.fetch_add(1U, std::memory_order_relaxed);
        g_allocationBytes.fetch_add(size, std::memory_order_relaxed);
        if (size >= 16U * 1024U) g_largeAllocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size == 0U ? 1U : size)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }

int main(int argc, char** argv) {
    if (argc <= 1) {
        RunAllSuites();
        return EXIT_SUCCESS;
    }

    for (int index = 1; index < argc; ++index) {
        if (!RunSuite(argv[index])) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
