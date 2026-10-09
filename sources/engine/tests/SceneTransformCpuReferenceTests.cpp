#include "engine/ecs/World.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "SceneTransformCpuReferenceOracle.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using kb::scene::Quat;
using kb::scene::SceneEntity;
using kb::scene::TransformComponent;
using kb::scene::Vec3;

constexpr float kHalf = 100.0F;
constexpr std::size_t kVisible = 10000U;

using CpuReferenceAgent = cpu_reference::Agent;
using Lcg = cpu_reference::Lcg;
using OracleRow = cpu_reference::Row;

struct SceneRows {
    kb::scene::Scene scene;
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    std::vector<SceneEntity> entities;
};

struct RunSettings {
    std::size_t initial = 10000U;
    std::size_t frames = 30U;
    std::size_t spawnPerFrame = 0U;
};

struct Diagnostics {
    std::string_view scenario;
    std::uint64_t frame = 0U;
    std::size_t index = 0U;
    std::size_t mismatches = 0U;
    std::uint64_t digest = 1469598103934665603ULL;

    void Field(std::string_view field, std::uint32_t expected, std::uint32_t actual, std::string_view source) {
        digest ^= actual;
        digest *= 1099511628211ULL;
        if (expected == actual) return;
        if (mismatches < 16U) {
            std::cerr << "MISMATCH scenario=" << scenario << " frame=" << frame << " index=" << index
                      << " source=" << source << " field=" << field << " expected=0x" << std::hex << expected
                      << " actual=0x" << actual << std::dec << '\n';
        }
        ++mismatches;
    }

    void Float(std::string_view field, float expected, float actual, std::string_view source) {
        Field(field, std::bit_cast<std::uint32_t>(expected), std::bit_cast<std::uint32_t>(actual), source);
    }

    void Vector(std::string_view field, const Vec3& expected, const Vec3& actual, std::string_view source) {
        Float(std::string(field) + ".x", expected.x, actual.x, source);
        Float(std::string(field) + ".y", expected.y, actual.y, source);
        Float(std::string(field) + ".z", expected.z, actual.z, source);
    }

    void Rotation(std::string_view field, const Quat& expected, const Quat& actual, std::string_view source) {
        Float(std::string(field) + ".x", expected.x, actual.x, source);
        Float(std::string(field) + ".y", expected.y, actual.y, source);
        Float(std::string(field) + ".z", expected.z, actual.z, source);
        Float(std::string(field) + ".w", expected.w, actual.w, source);
    }

    void Transform(const TransformComponent& expected, const TransformComponent& actual, std::string_view source) {
        Vector("localPosition", expected.localPosition, actual.localPosition, source);
        Rotation("localRotation", expected.localRotation, actual.localRotation, source);
        Vector("localScale", expected.localScale, actual.localScale, source);
        Vector("worldPosition", expected.worldPosition, actual.worldPosition, source);
        Rotation("worldRotation", expected.worldRotation, actual.worldRotation, source);
        Vector("worldScale", expected.worldScale, actual.worldScale, source);
        Field("localVersion", expected.localVersion, actual.localVersion, source);
        Field("parentVersion", expected.parentVersion, actual.parentVersion, source);
        Field("worldVersion", expected.worldVersion, actual.worldVersion, source);
        Field("worldDirty", expected.worldDirty ? 1U : 0U, actual.worldDirty ? 1U : 0U, source);
    }

    void Agent(const CpuReferenceAgent& expected, const CpuReferenceAgent& actual, std::string_view source) {
        Float("heading", expected.heading, actual.heading, source);
        Float("speed", expected.speed, actual.speed, source);
        Field("index", expected.index, actual.index, source);
    }

    void RequireMatch() const {
        if (mismatches != 0U) throw std::runtime_error("Exact CPU reference comparison failed with " + std::to_string(mismatches) + " named-field mismatches");
    }
};

void Populate(SceneRows& rows, const std::vector<kb::scene::SceneObjectDesc>& descriptions, const std::vector<CpuReferenceAgent>& agents) {
    const std::array<kb::ecs::World::BulkComponentView, 1U> components{
        kb::ecs::World::MakeBulkComponentView(std::span<const CpuReferenceAgent>{ agents })
    };
    const auto objects = rows.scene.Entities().CreateObjects(descriptions, components);
    if (objects.size() != agents.size()) throw std::runtime_error("CPU reference bulk creation missed an entity");
    const kb::scene::MeshRendererComponent mesh{ .castsShadow = false, .receivesShadow = false };
    for (const auto& object : objects) {
        const std::size_t index = rows.entities.size();
        rows.entities.push_back(object.Entity());
        // No renderer/assets are loaded in this correctness test. The component and migration match the
        // visible prefix's native archetype and proxy bookkeeping in both scenes.
        if (index < kVisible) rows.scene.Components().MeshRenderers().Set(object.Entity(), mesh);
    }
}

void SpawnRows(SceneRows& reference, SceneRows& candidate, std::vector<OracleRow>& oracle, Lcg& random,
    std::size_t count, Diagnostics& diagnostics) {
    if (count == 0U) return;
    const std::size_t first = oracle.size();
    std::vector<kb::scene::SceneObjectDesc> descriptions(count);
    std::vector<CpuReferenceAgent> agents(count);
    for (std::size_t row = 0U; row < count; ++row) {
        const OracleRow initial = cpu_reference::MakeInitialRow(random, first + row);
        descriptions[row].transform = initial.transform;
        agents[row] = initial.agent;
    }
    Populate(reference, descriptions, agents);
    Populate(candidate, descriptions, agents);
    oracle.reserve(first + count);
    for (std::size_t row = 0U; row < count; ++row) {
        diagnostics.index = first + row;
        const auto referenceTransform = reference.scene.Transforms().Get(reference.entities[first + row]);
        const auto candidateTransform = candidate.scene.Transforms().Get(candidate.entities[first + row]);
        diagnostics.Vector("initialPosition", descriptions[row].transform.localPosition, referenceTransform.localPosition, "creation");
        diagnostics.Rotation("initialRotation", descriptions[row].transform.localRotation, referenceTransform.localRotation, "creation");
        diagnostics.Vector("initialScale", descriptions[row].transform.localScale, referenceTransform.localScale, "creation");
        diagnostics.Transform(referenceTransform, candidateTransform, "candidate-creation");
        const auto* actualAgent = reference.world.TryGet<CpuReferenceAgent>(reference.entities[first + row]);
        const auto* candidateAgent = candidate.world.TryGet<CpuReferenceAgent>(candidate.entities[first + row]);
        if (actualAgent == nullptr || candidateAgent == nullptr) throw std::runtime_error("CPU reference creation lost an Extra component");
        diagnostics.Agent(agents[row], *actualAgent, "reference-creation");
        diagnostics.Agent(agents[row], *candidateAgent, "candidate-creation");
        oracle.push_back(OracleRow{ .agent = agents[row], .transform = referenceTransform });
    }
    diagnostics.RequireMatch();
}

std::size_t ReferenceFrame(SceneRows& reference, std::vector<OracleRow>& oracle, std::uint64_t frame, float dt) {
    std::size_t wraps = 0U;
    for (std::size_t index = 0U; index < oracle.size(); ++index) {
        OracleRow& expected = oracle[index];
        wraps += cpu_reference::AdvanceRow(expected, frame, dt);
        reference.world.Set(reference.entities[index], expected.agent);
        reference.scene.Transforms().Set(reference.entities[index], expected.transform);
    }
    return wraps;
}

void CandidateFrame(SceneRows& candidate, std::uint64_t frame, float dt, Diagnostics& diagnostics) {
    const float kDt = std::min(dt, 0.1F);
    const float t = static_cast<float>(frame) * kDt;
    const std::size_t count = candidate.entities.size();
    const auto coverage = std::make_unique<std::atomic_uint32_t[]>(count);
    std::atomic_size_t invalidIds{ 0U };
    const auto stats = candidate.scene.Transforms().ParallelForEachRoot<CpuReferenceAgent>(2048U,
        [&candidate, &coverage, &invalidIds, count, t, kDt](kb::scene::TransformRowRange& range) {
            CpuReferenceAgent* agents = range.Column<CpuReferenceAgent>();
            for (std::size_t row = 0U; row < range.Count(); ++row) {
                CpuReferenceAgent& agent = agents[row];
                if (agent.index >= count || candidate.entities[agent.index] != range.Entity(row)) {
                    invalidIds.fetch_add(1U, std::memory_order_relaxed);
                } else {
                    coverage[agent.index].fetch_add(1U, std::memory_order_relaxed);
                }
                const TransformComponent& current = range.Get(row);
                // Intentionally repeated independently from the frozen oracle, preserving the adapter's full
                // scalar expressions, operation order and direct engine-space positive yaw.
                const float h = agent.heading + 1.5F * std::sin(t * 0.5F + static_cast<float>(agent.index) * 0.001F) * kDt;
                float px = current.localPosition.x + std::cos(h) * agent.speed * kDt;
                float pz = current.localPosition.z + std::sin(h) * agent.speed * kDt;
                if (px < -kHalf) px += 2.0F * kHalf; else if (px >= kHalf) px -= 2.0F * kHalf;
                if (pz < -kHalf) pz += 2.0F * kHalf; else if (pz >= kHalf) pz -= 2.0F * kHalf;
                agent.heading = h;
                range.SetLocal(row, Vec3{ px, 0.5F, pz }, Quat{ 0.0F, std::sin(h * 0.5F), 0.0F, std::cos(h * 0.5F) }, current.localScale);
            }
        });
    if (invalidIds.load(std::memory_order_relaxed) != 0U || stats.rowsVisited != count || stats.rowsWritten != count || stats.rowsDeferred != 0U) {
        throw std::runtime_error("CPU pass coverage failed: visited=" + std::to_string(stats.rowsVisited) +
            " written=" + std::to_string(stats.rowsWritten) + " deferred=" + std::to_string(stats.rowsDeferred) +
            " expected=" + std::to_string(count) + " invalidIds=" + std::to_string(invalidIds.load(std::memory_order_relaxed)));
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const auto visits = coverage[index].load(std::memory_order_relaxed);
        diagnostics.index = index;
        diagnostics.Field("independentVisits", 1U, visits, "coverage");
    }
    diagnostics.RequireMatch();
}

void CompareAll(SceneRows& reference, SceneRows& candidate, const std::vector<OracleRow>& oracle, Diagnostics& diagnostics) {
    if (reference.scene.Entities().Count() != oracle.size() || candidate.scene.Entities().Count() != oracle.size()) {
        throw std::runtime_error("CPU reference scene contains an unexpected entity population");
    }
    for (std::size_t index = 0U; index < oracle.size(); ++index) {
        diagnostics.index = index;
        const auto referenceTransform = reference.scene.Transforms().Get(reference.entities[index]);
        const auto candidateTransform = candidate.scene.Transforms().Get(candidate.entities[index]);
        const auto* referenceAgent = reference.world.TryGet<CpuReferenceAgent>(reference.entities[index]);
        const auto* candidateAgent = candidate.world.TryGet<CpuReferenceAgent>(candidate.entities[index]);
        if (referenceAgent == nullptr || candidateAgent == nullptr) throw std::runtime_error("CPU reference frame lost an Extra component");
        diagnostics.Transform(oracle[index].transform, referenceTransform, "public-Set-reference");
        diagnostics.Agent(oracle[index].agent, *referenceAgent, "scalar-reference");
        diagnostics.Transform(referenceTransform, candidateTransform, "typed-pass");
        diagnostics.Agent(*referenceAgent, *candidateAgent, "typed-pass");
    }
    diagnostics.RequireMatch();
}

float FrameDt(std::size_t frame) {
    constexpr std::array<float, 10U> pattern{
        1.0F / 60.0F, 1.0F / 30.0F, 0.0F, 0.004F, 0.1F, 0.25F, 0.099999994F, 0.10000001F, 0.001F, 1.0F / 24.0F
    };
    return pattern[frame % pattern.size()];
}

void RunPopulation(const RunSettings& settings) {
    SceneRows reference;
    SceneRows candidate;
    std::vector<OracleRow> oracle;
    Lcg random;
    Diagnostics diagnostics{ .scenario = "seed12345-variable-dt" };
    SpawnRows(reference, candidate, oracle, random, settings.initial, diagnostics);
    std::size_t wraps = 0U;
    for (std::size_t frame = 0U; frame < settings.frames; ++frame) {
        diagnostics.frame = static_cast<std::uint64_t>(frame);
        SpawnRows(reference, candidate, oracle, random, settings.spawnPerFrame, diagnostics);
        const float dt = FrameDt(frame);
        wraps += ReferenceFrame(reference, oracle, diagnostics.frame, dt);
        CandidateFrame(candidate, diagnostics.frame, dt, diagnostics);
        // Kernel dt is clamped alone, as in the adapter; both full SceneRuntime updates get the same original dt.
        static_cast<void>(reference.scene.Runtime().Update(dt));
        static_cast<void>(candidate.scene.Runtime().Update(dt));
        CompareAll(reference, candidate, oracle, diagnostics);
    }
    std::cout << "PASS seed12345 initial=" << settings.initial << " frames=" << settings.frames << " spawn=" << settings.spawnPerFrame
              << " final=" << oracle.size() << " wraps=" << wraps << " mismatches=" << diagnostics.mismatches
              << " digest=0x" << std::hex << diagnostics.digest << std::dec << std::endl;
}

void RunBoundaryFrames() {
    SceneRows reference;
    SceneRows candidate;
    std::vector<OracleRow> oracle;
    Lcg random;
    Diagnostics diagnostics{ .scenario = "wrap-and-large-frame" };
    SpawnRows(reference, candidate, oracle, random, 8U, diagnostics);
    constexpr std::array<float, 8U> positions{ 100.0F, -100.0F, 99.99999F, -99.99999F, 0.0F, -0.0F, 99.9F, -99.9F };
    for (std::size_t index = 0U; index < oracle.size(); ++index) {
        auto transform = oracle[index].transform;
        transform.localPosition.x = positions[index];
        transform.localPosition.z = positions[positions.size() - 1U - index];
        reference.scene.Transforms().Set(reference.entities[index], transform);
        candidate.scene.Transforms().Set(candidate.entities[index], transform);
        oracle[index].transform = reference.scene.Transforms().Get(reference.entities[index]);
    }
    constexpr std::array<std::uint64_t, 8U> frames{ 0U, 1U, 17U, 1000U, 16777215U, 16777216U, 16777217U, 4294967295ULL };
    std::size_t wraps = 0U;
    for (std::size_t step = 0U; step < frames.size(); ++step) {
        diagnostics.frame = frames[step];
        const float dt = step == 0U ? 0.0F : FrameDt(step);
        wraps += ReferenceFrame(reference, oracle, frames[step], dt);
        CandidateFrame(candidate, frames[step], dt, diagnostics);
        static_cast<void>(reference.scene.Runtime().Update(dt));
        static_cast<void>(candidate.scene.Runtime().Update(dt));
        CompareAll(reference, candidate, oracle, diagnostics);
    }
    if (wraps == 0U) throw std::runtime_error("CPU boundary reference did not exercise an arena wrap");
    std::cout << "PASS boundary frames=" << frames.size() << " wraps=" << wraps << " mismatches=" << diagnostics.mismatches
              << " digest=0x" << std::hex << diagnostics.digest << std::dec << std::endl;
}

float FloatFromBits(std::uint32_t bits) {
    // Keep the corpus's exact inputs as runtime values, including NaN payloads and signed zero.
    volatile std::uint32_t runtimeBits = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(runtimeBits));
}

void RunNormalizeCorpus() {
    struct Entry {
        std::string_view name;
        Quat rotation;
    };
    const float positiveZero = FloatFromBits(0x00000000U);
    const float negativeZero = FloatFromBits(0x80000000U);
    const float subnormal = FloatFromBits(0x00000001U);
    const float maximum = FloatFromBits(0x7F7FFFFFU);
    const float positiveInfinity = FloatFromBits(0x7F800000U);
    const float negativeInfinity = FloatFromBits(0xFF800000U);
    const float positiveQuietNan = FloatFromBits(0x7FC01234U);
    const float negativeQuietNan = FloatFromBits(0xFFC05678U);
    const float signalingNan = FloatFromBits(0x7F801357U);
    const std::array corpus{
        Entry{ "identity-positive-zero", Quat{ positiveZero, positiveZero, positiveZero, 1.0F } },
        Entry{ "identity-negative-zero", Quat{ negativeZero, negativeZero, negativeZero, 1.0F } },
        Entry{ "identity-mixed-zero", Quat{ negativeZero, positiveZero, negativeZero, 1.0F } },
        Entry{ "negative-identity", Quat{ negativeZero, positiveZero, negativeZero, -1.0F } },
        Entry{ "zero-positive", Quat{ positiveZero, positiveZero, positiveZero, positiveZero } },
        Entry{ "zero-negative", Quat{ negativeZero, negativeZero, negativeZero, negativeZero } },
        Entry{ "subnormal", Quat{ subnormal, -subnormal, subnormal, -subnormal } },
        Entry{ "tiny", Quat{ 1.0e-20F, -1.0e-20F, 2.0e-20F, -1.0e-20F } },
        Entry{ "epsilon-below", Quat{ 0.00099999F, positiveZero, negativeZero, positiveZero } },
        Entry{ "epsilon-at-expression", Quat{ 0.001F, positiveZero, negativeZero, positiveZero } },
        Entry{ "epsilon-above", Quat{ 0.00100001F, positiveZero, negativeZero, positiveZero } },
        Entry{ "nonunit", Quat{ 2.0F, -3.0F, 4.0F, -5.0F } },
        Entry{ "finite-square-overflow", Quat{ maximum, maximum, -maximum, maximum } },
        Entry{ "positive-infinity-x", Quat{ positiveInfinity, 2.0F, -3.0F, 4.0F } },
        Entry{ "negative-infinity-y", Quat{ 2.0F, negativeInfinity, -3.0F, 4.0F } },
        Entry{ "mixed-infinities", Quat{ positiveInfinity, negativeInfinity, positiveInfinity, negativeInfinity } },
        Entry{ "quiet-nan-x", Quat{ positiveQuietNan, 2.0F, -3.0F, 4.0F } },
        Entry{ "quiet-nan-y", Quat{ 2.0F, negativeQuietNan, -3.0F, 4.0F } },
        Entry{ "quiet-nan-z", Quat{ 2.0F, -3.0F, positiveQuietNan, 4.0F } },
        Entry{ "quiet-nan-w", Quat{ 2.0F, -3.0F, 4.0F, negativeQuietNan } },
        Entry{ "signaling-nan-y", Quat{ 2.0F, signalingNan, -3.0F, 4.0F } }
    };
    SceneRows reference;
    SceneRows candidate;
    std::vector<OracleRow> oracle;
    Lcg random;
    Diagnostics diagnostics{ .scenario = "quaternion-specials-version-wrap" };
    SpawnRows(reference, candidate, oracle, random, corpus.size(), diagnostics);
    const auto maximumVersion = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = 0U; index < corpus.size(); ++index) {
        TransformComponent seed = oracle[index].transform;
        seed.localPosition = Vec3{ index % 2U == 0U ? negativeZero : positiveZero, 0.5F, -3.0F };
        seed.localRotation = corpus[index].rotation;
        seed.localScale = Vec3{ 1.0F, -2.0F, index % 2U == 0U ? negativeZero : positiveZero };
        seed.worldPosition = Vec3{ 37.0F, -41.0F, 43.0F };
        seed.worldRotation = Quat{ 2.0F, 3.0F, 5.0F, 7.0F };
        seed.worldScale = Vec3{ 11.0F, 13.0F, 17.0F };
        seed.localVersion = maximumVersion - static_cast<std::uint32_t>(index % 2U);
        seed.parentVersion = maximumVersion;
        seed.worldVersion = maximumVersion - static_cast<std::uint32_t>((index + 1U) % 2U);
        seed.worldDirty = index % 2U == 0U;
        // Test setup alone seeds metadata directly. Every tested write below uses public Set or SetLocal;
        // the frozen oracle predicts all world fields and metadata without reading either path's results.
        auto* referenceTransform = reference.scene.Transforms().TryGet(reference.entities[index]);
        auto* candidateTransform = candidate.scene.Transforms().TryGet(candidate.entities[index]);
        if (referenceTransform == nullptr || candidateTransform == nullptr) throw std::runtime_error("Quaternion corpus lost a transform during setup");
        *referenceTransform = seed;
        *candidateTransform = seed;
        oracle[index].transform = seed;
    }
    std::size_t localWraps = 0U;
    std::size_t worldWraps = 0U;
    constexpr std::size_t repetitions = 3U;
    std::cout << "START quaternion specials entries=" << corpus.size() << " writes=" << repetitions << std::endl;
    for (std::size_t step = 0U; step < repetitions; ++step) {
        diagnostics.frame = step;
        for (std::size_t index = 0U; index < oracle.size(); ++index) {
            TransformComponent& expected = oracle[index].transform;
            localWraps += expected.localVersion == maximumVersion ? 1U : 0U;
            worldWraps += expected.worldVersion == maximumVersion ? 1U : 0U;
            ++expected.localVersion;
            expected.worldDirty = true;
            cpu_reference::ComposeRoot(expected);
            reference.scene.Transforms().Set(reference.entities[index], expected);
        }
        const std::size_t count = candidate.entities.size();
        const auto coverage = std::make_unique<std::atomic_uint32_t[]>(count);
        std::atomic_size_t invalidIds{ 0U };
        const auto stats = candidate.scene.Transforms().ParallelForEachRoot<CpuReferenceAgent>(1U,
            [&candidate, &coverage, &invalidIds, count](kb::scene::TransformRowRange& range) {
                const auto* agents = range.Column<const CpuReferenceAgent>();
                for (std::size_t row = 0U; row < range.Count(); ++row) {
                    const std::uint32_t index = agents[row].index;
                    if (index >= count || candidate.entities[index] != range.Entity(row)) {
                        invalidIds.fetch_add(1U, std::memory_order_relaxed);
                    } else {
                        coverage[index].fetch_add(1U, std::memory_order_relaxed);
                    }
                    const TransformComponent& current = range.Get(row);
                    range.SetLocal(row, current.localPosition, current.localRotation, current.localScale);
                }
            });
        if (invalidIds.load(std::memory_order_relaxed) != 0U || stats.rowsVisited != count || stats.rowsWritten != count || stats.rowsDeferred != 0U) {
            throw std::runtime_error("Quaternion corpus pass did not visit and compose every exact entity once");
        }
        for (std::size_t index = 0U; index < count; ++index) {
            diagnostics.index = index;
            diagnostics.Field("independentVisits", 1U, coverage[index].load(std::memory_order_relaxed), "quaternion-coverage");
        }
        static_cast<void>(reference.scene.Runtime().Update(0.0F));
        static_cast<void>(candidate.scene.Runtime().Update(0.0F));
        // NaNs, infinities and signed zeros are compared by their exact bits too. If a platform produces
        // a different payload/sign, report the concrete entry; never turn this into a tolerance comparison.
        for (std::size_t index = 0U; index < count; ++index) {
            diagnostics.index = index;
            diagnostics.scenario = corpus[index].name;
            const auto referenceTransform = reference.scene.Transforms().Get(reference.entities[index]);
            const auto candidateTransform = candidate.scene.Transforms().Get(candidate.entities[index]);
            diagnostics.Transform(oracle[index].transform, referenceTransform, "frozen-oracle-to-public-Set");
            diagnostics.Transform(oracle[index].transform, candidateTransform, "frozen-oracle-to-typed-pass");
        }
        diagnostics.RequireMatch();
        diagnostics.scenario = "quaternion-specials-version-wrap";
        CompareAll(reference, candidate, oracle, diagnostics);
    }
    if (localWraps != corpus.size() || worldWraps != corpus.size()) throw std::runtime_error("Quaternion corpus failed to exercise each uint32 version wrap");
    std::cout << "PASS quaternion specials entries=" << corpus.size() << " writes=" << repetitions << " localWraps=" << localWraps
              << " worldWraps=" << worldWraps << " mismatches=" << diagnostics.mismatches << " digest=0x"
              << std::hex << diagnostics.digest << std::dec << std::endl;
}

std::size_t ParseSize(std::string_view value) {
    std::size_t parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) throw std::invalid_argument("Expected an unsigned count: " + std::string(value));
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 4) throw std::invalid_argument("Usage: SceneTransformCpuReferenceTests [initial=10000] [frames=30] [spawnPerFrame=0]");
        RunSettings settings;
        if (argc > 1) settings.initial = ParseSize(argv[1]);
        if (argc > 2) settings.frames = ParseSize(argv[2]);
        if (argc > 3) settings.spawnPerFrame = ParseSize(argv[3]);
        const auto limit = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
        if (settings.initial > limit || (settings.frames != 0U && settings.spawnPerFrame > (limit - settings.initial) / settings.frames)) {
            throw std::invalid_argument("CPU reference population exceeds the Agent index domain");
        }
        std::cout << "START exact CPU reference (bitwise fields; full scalar sin/cos/normalize; independent ID coverage)" << std::endl;
        RunPopulation(settings);
        RunBoundaryFrames();
        RunNormalizeCorpus();
        std::cout << "Scene transform CPU reference tests passed" << std::endl;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL exact CPU reference: " << exception.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
