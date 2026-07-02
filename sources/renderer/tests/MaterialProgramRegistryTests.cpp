#include "RendererTestSupport.hpp"

#include "kb/render/MaterialProgramRegistry.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace kb::render::tests {
namespace {

[[nodiscard]] MaterialProgramKey GraphKey(
    std::uint64_t graphSourceHash,
    std::string pass,
    std::uint32_t backend = 0U,
    std::uint64_t variantKey = 0x5555U,
    std::uint64_t pipelineStateKey = 0x7777U,
    std::uint64_t materialTypeId = 0xABCDU,
    std::uint32_t materialTypeVersion = 1U) {
    return MaterialProgramKey{
        .materialTypeId = materialTypeId,
        .materialTypeVersion = materialTypeVersion,
        .graphSourceHash = graphSourceHash,
        .variantKey = variantKey,
        .pass = std::move(pass),
        .backend = backend,
        .pipelineStateKey = pipelineStateKey,
        .graphProgram = true,
    };
}

struct RecordingDestroyer {
    std::vector<std::uint16_t> destroyed;

    [[nodiscard]] MaterialProgramDestroyer Callback() {
        return [this](bgfx::ProgramHandle handle) {
            destroyed.push_back(handle.idx);
        };
    }

    [[nodiscard]] bool WasDestroyed(std::uint16_t idx) const {
        return std::find(destroyed.begin(), destroyed.end(), idx) != destroyed.end();
    }
};

[[nodiscard]] MaterialProgramLoader IncrementingLoader(std::uint16_t* counter) {
    return [counter](const MaterialProgramKey&) {
        const std::uint16_t idx = (*counter)++;
        return bgfx::ProgramHandle{ idx };
    };
}

[[nodiscard]] MaterialProgramLoader FailingLoader() {
    return [](const MaterialProgramKey&) {
        return bgfx::ProgramHandle{ BGFX_INVALID_HANDLE };
    };
}

void RunMaterialProgramRegistryReuseAndDistinctKeysTest() {
    std::uint16_t counter = 10U;
    RecordingDestroyer destroyer;
    MaterialProgramRegistry registry;
    registry.Configure(IncrementingLoader(&counter), destroyer.Callback());

    const MaterialProgramKey keyA = GraphKey(0x100U, "BaseOpaque");
    const bgfx::ProgramHandle first = registry.Acquire(keyA);
    Require(bgfx::isValid(first), "KBMAT-MAT05: First acquire must produce a valid program");

    const bgfx::ProgramHandle second = registry.Acquire(keyA);
    Require(second.idx == first.idx, "KBMAT-MAT05: Two materials with the same graph hash/pass must reuse the program");
    Require(registry.RefCount(keyA) == 2U, "KBMAT-MAT05: Reused program must be reference counted");

    const MaterialProgramKey keyB = GraphKey(0x200U, "BaseOpaque");
    const bgfx::ProgramHandle other = registry.Acquire(keyB);
    Require(bgfx::isValid(other) && other.idx != first.idx, "KBMAT-MAT05: A different graph hash must resolve to a different program");

    const MaterialProgramRegistryStats stats = registry.Stats();
    Require(stats.loads == 2U, "KBMAT-MAT05: Distinct keys must trigger exactly one load each");
    Require(stats.hits == 1U, "KBMAT-MAT05: Reacquiring the same key must count as a cache hit");
    Require(stats.liveProgramCount == 2U, "KBMAT-MAT05: Registry must report two live programs");

    registry.Shutdown();
    Require(destroyer.WasDestroyed(first.idx) && destroyer.WasDestroyed(other.idx),
        "KBMAT-MAT05: Shutdown must destroy every owned program handle");
}

void RunMaterialProgramRegistryFullIdentityKeyTest() {
    std::uint16_t counter = 100U;
    RecordingDestroyer destroyer;
    MaterialProgramRegistry registry;
    registry.Configure(IncrementingLoader(&counter), destroyer.Callback());

    const MaterialProgramKey base = GraphKey(
        0xFFFF'0000'0000'0001ULL,
        "BaseOpaque",
        0x0000'0001U,
        0xEEEE'0000'0000'0002ULL,
        0xDDDD'0000'0000'0003ULL,
        0xCCCC'0000'0000'0004ULL,
        7U);
    const bgfx::ProgramHandle baseHandle = registry.Acquire(base);
    Require(bgfx::isValid(baseHandle), "KBMAT-MAT66: Base full-identity graph program must load");
    Require(registry.Acquire(base).idx == baseHandle.idx,
        "KBMAT-MAT66: Reacquiring the exact full program key must reuse the same program");

    std::vector<MaterialProgramKey> distinctKeys;
    MaterialProgramKey changedType = base;
    changedType.materialTypeId ^= 0x8000'0000'0000'0000ULL;
    distinctKeys.push_back(changedType);
    MaterialProgramKey changedVersion = base;
    ++changedVersion.materialTypeVersion;
    distinctKeys.push_back(changedVersion);
    MaterialProgramKey changedHashHighBits = base;
    changedHashHighBits.graphSourceHash ^= 0xFFFF'0000'0000'0000ULL;
    distinctKeys.push_back(changedHashHighBits);
    MaterialProgramKey changedVariant = base;
    changedVariant.variantKey ^= 0xAAAA'0000'0000'0000ULL;
    distinctKeys.push_back(changedVariant);
    MaterialProgramKey changedPass = base;
    changedPass.pass = "ShadowDepth";
    distinctKeys.push_back(changedPass);
    MaterialProgramKey changedBackend = base;
    changedBackend.backend = 0x8000'0002U;
    distinctKeys.push_back(changedBackend);
    MaterialProgramKey changedPipeline = base;
    changedPipeline.pipelineStateKey ^= 0xBBBB'0000'0000'0000ULL;
    distinctKeys.push_back(changedPipeline);

    for (const MaterialProgramKey& key : distinctKeys) {
        Require(!(key == base), "KBMAT-MAT66: Mutating any full identity field must change key equality");
        Require(MaterialProgramKeyIdentityHash(key) != MaterialProgramKeyIdentityHash(base),
            "KBMAT-MAT66: Full program identity hash must include high bits and every key field");
        const bgfx::ProgramHandle handle = registry.Acquire(key);
        Require(bgfx::isValid(handle) && handle.idx != baseHandle.idx,
            "KBMAT-MAT66: A distinct full identity key must not reuse the base program");
    }

    Require(registry.Stats().loads == static_cast<std::uint32_t>(distinctKeys.size() + 1U),
        "KBMAT-MAT66: Registry loads must match distinct full program identities");
    registry.Shutdown();
}

void RunMaterialProgramRegistryMissingBinaryTest() {
    RecordingDestroyer destroyer;
    MaterialProgramRegistry registry;
    registry.Configure(FailingLoader(), destroyer.Callback());

    const MaterialProgramKey key = GraphKey(0x300U, "BaseOpaque");
    const bgfx::ProgramHandle handle = registry.Acquire(key);
    Require(!bgfx::isValid(handle), "KBMAT-MAT05: A missing binary must yield an invalid program, not a crash");

    const MaterialProgramRegistryStats stats = registry.Stats();
    Require(stats.failures == 1U, "KBMAT-MAT05: A failed load must be recorded as a failure");
    Require(stats.liveProgramCount == 0U, "KBMAT-MAT05: A failed load must not register a live program");
    registry.Shutdown();
}

void RunMaterialProgramRegistryDeferredDestroyTest() {
    std::uint16_t counter = 20U;
    RecordingDestroyer destroyer;
    MaterialProgramRegistry registry;
    registry.Configure(IncrementingLoader(&counter), destroyer.Callback(), 2U);
    registry.BeginFrame(100U);

    const MaterialProgramKey key = GraphKey(0x400U, "BaseOpaque");
    const bgfx::ProgramHandle handle = registry.Acquire(key);
    Require(registry.Acquire(key).idx == handle.idx && registry.RefCount(key) == 2U, "KBMAT-MAT05: Program must be shared with refcount 2");

    registry.Release(key);
    Require(registry.RefCount(key) == 1U, "KBMAT-MAT05: One release must leave the program live");
    registry.Release(key);
    Require(registry.RefCount(key) == 0U, "KBMAT-MAT05: Final release must drop the reference count to zero");
    Require(!destroyer.WasDestroyed(handle.idx), "KBMAT-MAT05: A program must not be destroyed in the frame it was released");

    registry.BeginFrame(101U);
    Require(!destroyer.WasDestroyed(handle.idx), "KBMAT-MAT05: A program must survive the grace period");
    registry.BeginFrame(102U);
    Require(destroyer.WasDestroyed(handle.idx), "KBMAT-MAT05: A program must be destroyed once its grace frames elapse");
    Require(registry.Stats().liveProgramCount == 0U, "KBMAT-MAT05: Destroyed programs must leave no live entries");
    registry.Shutdown();
}

void RunMaterialProgramRegistryFailedReloadKeepsLastGoodTest() {
    std::uint16_t counter = 30U;
    RecordingDestroyer destroyer;
    MaterialProgramRegistry registry;
    registry.Configure(IncrementingLoader(&counter), destroyer.Callback(), 2U);
    registry.BeginFrame(0U);

    const MaterialProgramKey key = GraphKey(0x500U, "BaseOpaque");
    const bgfx::ProgramHandle good = registry.Acquire(key);
    Require(bgfx::isValid(good), "KBMAT-MAT05: Initial load must succeed");

    // A compile that now fails must keep the last-good program bound.
    registry.Configure(FailingLoader(), destroyer.Callback(), 2U);
    const bgfx::ProgramHandle afterFailure = registry.Reload(key);
    Require(afterFailure.idx == good.idx, "KBMAT-MAT05: A failed reload must keep serving the last-good program");
    Require(!destroyer.WasDestroyed(good.idx), "KBMAT-MAT05: A failed reload must not destroy the last-good program");
    Require(registry.Stats().lastGoodUses == 1U, "KBMAT-MAT05: A failed reload must be reported as a last-good use");
    Require(registry.Stats().failures >= 1U, "KBMAT-MAT05: A failed reload must be reported as a failure");

    // A successful reload swaps to a new program and retires the old one after the grace period.
    registry.Configure(IncrementingLoader(&counter), destroyer.Callback(), 2U);
    const bgfx::ProgramHandle reloaded = registry.Reload(key);
    Require(bgfx::isValid(reloaded) && reloaded.idx != good.idx, "KBMAT-MAT05: A successful reload must swap to a new program");
    Require(registry.Stats().reloads == 1U, "KBMAT-MAT05: A successful reload must be counted");
    Require(!destroyer.WasDestroyed(good.idx), "KBMAT-MAT05: The replaced program must not be destroyed during the active frame");
    registry.BeginFrame(2U);
    Require(destroyer.WasDestroyed(good.idx), "KBMAT-MAT05: The replaced program must be retired after its grace frames");
    registry.Shutdown();
}

} // namespace

void RunMaterialProgramRegistryTests() {
    RunMaterialProgramRegistryReuseAndDistinctKeysTest();
    RunMaterialProgramRegistryFullIdentityKeyTest();
    RunMaterialProgramRegistryMissingBinaryTest();
    RunMaterialProgramRegistryDeferredDestroyTest();
    RunMaterialProgramRegistryFailedReloadKeepsLastGoodTest();
}

} // namespace kb::render::tests
