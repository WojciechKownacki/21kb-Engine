#include "TestSupport.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/scene/ParallelSceneSystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
constexpr float kStep = 1.0F / 1024.0F;
constexpr std::uint32_t kSeed = 0x12345678U;

std::uint64_t Append(std::uint64_t hash, std::uint32_t value) {
    return (hash ^ value) * 1099511628211ULL;
}

struct Observation {
    std::atomic<std::size_t> count{0U};
    std::atomic<bool> ownerOnly{true};
    std::thread::id owner = std::this_thread::get_id();
    std::array<std::uint32_t, 64U> calls{};
};

class FixedProbe final : public kb::scene::SceneSystem, public kb::scene::IParallelSceneSystem {
public:
    FixedProbe(Observation& observation, std::uint32_t id, bool slow)
        : observation_(observation), id_(id), slow_(slow) {}
    bool RequiresFixedStep() const override { return true; }
    kb::scene::SceneSystemAccess DeclareAccess() const override {
        return {.writes = {"probe:" + std::to_string(id_)}};
    }
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        kb::tests::Require(context.DeltaSeconds() == kStep, "Replay changed fixed delta");
        if (std::this_thread::get_id() != observation_.owner) observation_.ownerOnly = false;
        const auto next = kb::math::NextUInt32(random_);
        random_ = next.stream;
        hash_ = Append(hash_, next.value);
        const auto index = observation_.count.fetch_add(1U);
        if (index < observation_.calls.size()) observation_.calls[index] = id_;
        if (slow_) std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    std::uint64_t Hash() const { return hash_; }
private:
    Observation& observation_;
    std::uint32_t id_;
    bool slow_;
    kb::math::RandomStream random_ = kb::math::MakeRandomStream(kSeed);
    std::uint64_t hash_ = 14695981039346656037ULL;
};

std::uint64_t RunFixedReplay(std::size_t workers, bool slow) {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    kb::scene::Scene scene{std::move(descriptor), {}, {.workerThreadLimit = workers}};
    scene.Runtime().SetFixedStepSettings({kStep, 8.0F * kStep, 4U});
    kb::tests::Require(!scene.Runtime().DeterminismScope().enabled, "Replay must be opt in");
    scene.Runtime().SetDeterministicExecution(true);
    // Execution policy is scene-owned and survives the existing checkpoint contract.
    std::string error;
    const auto checkpoint = scene.Runtime().CaptureRuntimeState(error);
    kb::tests::Require(checkpoint != nullptr, error.c_str());
    scene.Runtime().SetDeterministicExecution(false);
    kb::tests::Require(scene.Runtime().RestoreRuntimeState(*checkpoint, error), error.c_str());
    kb::tests::Require(scene.Runtime().DeterminismScope().enabled, "Checkpoint lost replay policy");
    for (const float invalid : {0.0F, -kStep, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        bool refused = false;
        try { static_cast<void>(scene.Runtime().Update(invalid)); }
        catch (const std::invalid_argument&) { refused = true; }
        kb::tests::Require(refused && scene.Runtime().FrameIndex() == 0U && scene.Runtime().FixedStepIndex() == 0U,
            "Replay must reject an invalid explicit delta before changing scene state");
    }
    Observation observation;
    auto first = std::make_unique<FixedProbe>(observation, 1U, slow);
    auto second = std::make_unique<FixedProbe>(observation, 2U, false);
    const auto* probe = first.get();
    scene.Runtime().AddSceneSystem(std::move(first));
    scene.Runtime().AddSceneSystem(std::move(second));
    constexpr std::array frameDeltas{3.5F*kStep, 0.5F*kStep, 12.0F*kStep, 0.25F*kStep, 8.0F*kStep};
    constexpr std::array<std::size_t, 5U> expectedTicks{3U, 1U, 4U, 0U, 4U};
    for (std::size_t frame = 0; frame < frameDeltas.size(); ++frame) {
        static_cast<void>(scene.Runtime().Update(frameDeltas[frame]));
        kb::tests::Require(scene.Runtime().LastFixedStepCount() == expectedTicks[frame],
            "Replay tick count depends on CPU cost or lost frame clamp/debt semantics");
    }
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 12U, "Replay fixed tick golden changed");
    kb::tests::Require(observation.count == 24U && observation.ownerOnly,
        "Replay scene dispatch must run serially on the owner");
    for (std::size_t i = 0; i < 24U; ++i) {
        kb::tests::Require(observation.calls[i] == i%2U+1U, "Replay changed scene registration order");
    }
    return probe->Hash();
}
}

namespace kb::tests {
void RunSceneDeterministicReplayTests() {
    auto stream = kb::math::MakeRandomStream(kSeed);
    std::uint64_t hash = 14695981039346656037ULL;
    std::uint64_t fixedGolden = 0U;
    for (std::size_t i=0; i<1024U; ++i) {
        const auto next = kb::math::NextUInt32(stream);
        stream = next.stream;
        hash = Append(hash, next.value);
        if (i==11U) fixedGolden=hash;
    }
    // Fixed external constants: unsigned arithmetic and byte order independent combination.
    Require(hash == 13224159966847899506ULL, "RandomStream uint32 golden changed");
    const auto saved = stream;
    const auto expected = kb::math::NextUInt32(stream);
    stream = kb::math::NextUInt32(stream).stream;
    stream = saved;
    Require(kb::math::NextUInt32(stream).value == expected.value, "RNG restored state diverged");
    const auto wrap = kb::math::NextUInt32({kSeed, 0xffffffffU});
    Require(wrap.value == 3200798080U && wrap.stream.counter == 0U, "RNG counter wrap golden changed");
    for (const auto workers : {1U, 2U, 4U}) {
        Require(RunFixedReplay(workers, false) == fixedGolden, "Fixed replay RNG golden changed");
    }
    Require(RunFixedReplay(4U, true) == fixedGolden, "Slow fixed replay RNG golden changed");
    std::cout << "replay_v1 rng_uint32_hash=" << hash << " fixed_ticks=12 fixed_rng_hash=" << fixedGolden
              << " workers=1,2,4 slow_step=PASS scene_order=PASS tolerance_uint32=0\n";
}
}
