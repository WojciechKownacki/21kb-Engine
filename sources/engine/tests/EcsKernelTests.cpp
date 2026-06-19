#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/KernelVectorMath.hpp"
#include "engine/ecs/HotKernel.hpp"
#include "engine/ecs/MemoryTrafficEstimator.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldConfigPresets.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct EcsMovementScaleAsset {
    float multiplier = 1.0F;
};

struct EcsMovementConstants {
    float deltaSeconds = 0.0F;
    float drag = 0.0F;
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

struct alignas(32) EcsKernelAlignedInput {
    float x = 0.0F;
};

struct alignas(32) EcsKernelAlignedOutput {
    float x = 0.0F;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

using EcsMovementKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsVelocity>,
    kb::ecs::KernelOutputComponents<EcsPosition>,
    kb::ecs::KernelReadOnlyAssets<EcsMovementScaleAsset>,
    EcsMovementConstants>;

struct EcsMovementKernel {
    void operator()(kb::ecs::KernelBatch<EcsMovementKernelContract>& batch) const {
        const EcsVelocity* velocities = batch.Inputs<0>();
        EcsPosition* positions = batch.Outputs<0>();
        const EcsMovementScaleAsset& scale = batch.Asset<0>();
        const EcsMovementConstants& constants = batch.Constants();

        for (std::size_t index = 0; index < batch.Count(); ++index) {
            batch.Prefetch(index + 4);
            positions[index].x += velocities[index].x * constants.deltaSeconds * scale.multiplier;
            positions[index].y += velocities[index].y * constants.deltaSeconds * (scale.multiplier - constants.drag);
        }
    }
};

using EcsSimdKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsVelocity>,
    kb::ecs::KernelOutputComponents<EcsPosition>>;

struct EcsHotMovementKernel {
    float scale = 1.0F;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        kb::ecs::ApplyDenseFloat2VelocityBatch(
            batch,
            scale,
            &EcsPosition::x,
            &EcsPosition::y,
            &EcsVelocity::x,
            &EcsVelocity::y,
            kb::ecs::KernelBackend::Scalar);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelSse2Tag) const {
        kb::ecs::ApplyDenseFloat2VelocityBatch(
            batch,
            scale,
            &EcsPosition::x,
            &EcsPosition::y,
            &EcsVelocity::x,
            &EcsVelocity::y,
            kb::ecs::KernelBackend::Sse2);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx2Tag) const {
        kb::ecs::ApplyDenseFloat2VelocityBatch(
            batch,
            scale,
            &EcsPosition::x,
            &EcsPosition::y,
            &EcsVelocity::x,
            &EcsVelocity::y,
            kb::ecs::KernelBackend::Avx2);
    }
};

[[nodiscard]] std::filesystem::path ResolveRepositoryRootForHotPathGuard() {
    std::filesystem::path current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "sources/engine/include/engine/ecs/HotKernel.hpp")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            return {};
        }
        current = current.parent_path();
    }
}

[[nodiscard]] std::string ReadTextFileForHotPathGuard(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    kb::tests::Require(input.is_open(), "ECS hot path guard could not open a guarded source file");
    return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void RequireHotPathSourceDoesNotContain(
    const std::filesystem::path& root,
    std::string_view relativePath,
    std::span<const std::string_view> forbiddenPatterns) {
    const std::string contents = ReadTextFileForHotPathGuard(root / relativePath);
    for (std::string_view pattern : forbiddenPatterns) {
        kb::tests::Require(
            contents.find(pattern) == std::string::npos,
            "ECS hot path guard found a forbidden lookup/dispatch token");
    }
}

struct EcsHotLocalTransform {
    float translationX = 0.0F;
    float translationY = 0.0F;
    float translationZ = 0.0F;
    float rotationZ = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
};

struct EcsHotWorldTransform {
    float matrix[16]{};
};

struct EcsHotWorldAffineTransform {
    float affine[12]{};
};

struct EcsHotTransformVersionState {
    std::uint64_t localVersion = 1U;
    std::uint64_t appliedLocalVersion = 0U;
};

struct EcsHotHierarchyNode {
    std::uint32_t parentIndex = 0xFFFFFFFFU;
    std::uint32_t localVersion = 1U;
    std::uint32_t appliedLocalVersion = 0U;
    std::uint32_t observedParentWorldVersion = 0U;
    std::uint32_t worldVersion = 0U;
};

struct EcsPaddedPosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct EcsPaddedVelocity {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

using EcsAlignedKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsKernelAlignedInput>,
    kb::ecs::KernelOutputComponents<EcsKernelAlignedOutput>>;

void AddProbeMovement(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, float scale) {
    const EcsVelocity* velocities = batch.Inputs<0>();
    EcsPosition* positions = batch.Outputs<0>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        positions[index].x += velocities[index].x * scale;
        positions[index].y += velocities[index].y * scale;
    }
}

struct EcsScalarOnlyProbeKernel {
    int* scalarBatches = nullptr;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        ++(*scalarBatches);
        AddProbeMovement(batch, 1.0F);
    }
};

struct EcsSimdProbeKernel {
    int* scalarBatches = nullptr;
    int* sse2Batches = nullptr;
    int* avx2Batches = nullptr;
    int* avx512Batches = nullptr;
    int* neonBatches = nullptr;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        ++(*scalarBatches);
        AddProbeMovement(batch, 1.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelSse2Tag) const {
        ++(*sse2Batches);
        AddProbeMovement(batch, 2.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx2Tag) const {
        ++(*avx2Batches);
        AddProbeMovement(batch, 4.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx512Tag) const {
        ++(*avx512Batches);
        AddProbeMovement(batch, 8.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelNeonTag) const {
        ++(*neonBatches);
        AddProbeMovement(batch, 3.0F);
    }
};

struct EcsAlignedKernel {
    void operator()(kb::ecs::KernelBatch<EcsAlignedKernelContract>& batch) const {
        const EcsKernelAlignedInput* inputs = batch.Inputs<0>();
        EcsKernelAlignedOutput* outputs = batch.Outputs<0>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            outputs[index].x = inputs[index].x * 2.0F;
        }
    }
};

struct EcsEditorKernelContext {
    int batches = 0;
    float scale = 1.0F;
};

void EcsEditorHotReloadKernelV1(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, void* context) {
    auto* typedContext = static_cast<EcsEditorKernelContext*>(context);
    ++typedContext->batches;
    AddProbeMovement(batch, typedContext->scale);
}

void EcsEditorHotReloadKernelV2(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, void* context) {
    auto* typedContext = static_cast<EcsEditorKernelContext*>(context);
    ++typedContext->batches;
    AddProbeMovement(batch, typedContext->scale * 2.0F);
}

struct EcsEditorClearingKernelContext {
    kb::ecs::EditorKernelBinding<EcsSimdKernelContract>* binding = nullptr;
    int batches = 0;
};

void EcsEditorClearingKernel(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, void* context) {
    auto* typedContext = static_cast<EcsEditorClearingKernelContext*>(context);
    ++typedContext->batches;
    AddProbeMovement(batch, 1.0F);
    typedContext->binding->Clear();
}

[[nodiscard]] float SumPositions(kb::ecs::KernelQuery<EcsSimdKernelContract>& query) {
    float sum = 0.0F;
    query.ForEachBatchKernel([&sum](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            sum += positions[index].x + positions[index].y;
        }
    });
    return sum;
}

void PopulateSimdProbeWorld(kb::ecs::World& world, int entityCount) {
    for (int index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 2.0F });
        world.Set(entity, EcsPosition{ .x = 0.0F, .y = 0.0F });
    }
}

[[nodiscard]] std::uint32_t FloatBits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] float DeterministicMulAdd(float multiplicand, float multiplier, float addend) noexcept {
    const float product = multiplicand * multiplier;
    return product + addend;
}

template <std::size_t Count>
[[nodiscard]] std::array<std::uint32_t, Count * 2U> ReferenceMovementBits(
    std::array<EcsPosition, Count> positions,
    const std::array<EcsVelocity, Count>& velocities) noexcept {
    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float maxSpeedSquared = 2.5F;
    constexpr float highBias = 0.125F;
    constexpr float lowBias = -0.25F;

    std::array<std::uint32_t, Count * 2U> bits{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float speedSquared = velocities[index].x * velocities[index].x + velocities[index].y * velocities[index].y;
        const float bias = speedSquared > maxSpeedSquared ? highBias : lowBias;
        positions[index].x = DeterministicMulAdd(velocities[index].x, deltaSeconds, positions[index].x);
        positions[index].y = DeterministicMulAdd(velocities[index].y, deltaSeconds, positions[index].y + bias);
        bits[index * 2U] = FloatBits(positions[index].x);
        bits[index * 2U + 1U] = FloatBits(positions[index].y);
    }
    return bits;
}

template <typename BackendTag, std::size_t Count>
[[nodiscard]] std::array<std::uint32_t, Count * 2U> VectorMovementBits(
    std::array<EcsPosition, Count> positions,
    const std::array<EcsVelocity, Count>& velocities) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec2Lanes = kb::ecs::KernelFloat2Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float maxSpeedSquared = 2.5F;
    constexpr float highBias = 0.125F;
    constexpr float lowBias = -0.25F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        Vec2Lanes position = Vec2Lanes::LoadMembersPartial(positions.data() + begin, batchCount, 0.0F, &EcsPosition::x, &EcsPosition::y);
        const Vec2Lanes velocity = Vec2Lanes::LoadMembersPartial(velocities.data() + begin, batchCount, 0.0F, &EcsVelocity::x, &EcsVelocity::y);
        const FloatLanes speedSquared = kb::ecs::KernelDot(velocity, velocity);
        const FloatLanes bias = kb::ecs::KernelSelect(
            kb::ecs::KernelGreaterThan(speedSquared, FloatLanes::Splat(maxSpeedSquared)),
            FloatLanes::Splat(highBias),
            FloatLanes::Splat(lowBias));

        position.template Component<1>() = position.template Component<1>() + bias;
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        position.StoreMembersPartial(positions.data() + begin, batchCount, &EcsPosition::x, &EcsPosition::y);
    }

    std::array<std::uint32_t, Count * 2U> bits{};
    for (std::size_t index = 0; index < Count; ++index) {
        bits[index * 2U] = FloatBits(positions[index].x);
        bits[index * 2U + 1U] = FloatBits(positions[index].y);
    }
    return bits;
}

struct EcsKernelLocalTransform {
    float translationX = 0.0F;
    float translationY = 0.0F;
    float translationZ = 0.0F;
    float rotationZ = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
};

struct EcsKernelWorldTransform {
    float matrix[16]{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct EcsKernelLocalBounds {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    float extentX = 0.5F;
    float extentY = 0.5F;
    float extentZ = 0.5F;
};

struct EcsKernelWorldBounds {
    float minX = 0.0F;
    float minY = 0.0F;
    float minZ = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;
    float maxZ = 0.0F;
};

struct EcsKernelPhysicsBody {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float velocityZ = 0.0F;
    float radius = 0.5F;
    float inverseMass = 1.0F;
};

struct EcsKernelPhysicsProxy {
    float predictedX = 0.0F;
    float predictedY = 0.0F;
    float predictedZ = 0.0F;
    float sweptRadius = 0.0F;
};

template <typename BackendTag>
[[nodiscard]] kb::ecs::KernelFloatLanes<BackendTag> AbsLanes(kb::ecs::KernelFloatLanes<BackendTag> value) noexcept {
    kb::ecs::KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < kb::ecs::KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, std::fabs(value.Lane(lane)));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] kb::ecs::KernelFloatLanes<BackendTag> SqrtLanes(kb::ecs::KernelFloatLanes<BackendTag> value) noexcept {
    kb::ecs::KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < kb::ecs::KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, std::sqrt(value.Lane(lane)));
    }
    return result;
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityMovementKernel(std::array<EcsPosition, Count>& positions, const std::array<EcsVelocity, Count>& velocities) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec2Lanes = kb::ecs::KernelFloat2Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0111111114F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        Vec2Lanes position = Vec2Lanes::LoadMembersPartial(positions.data() + begin, batchCount, 0.0F, &EcsPosition::x, &EcsPosition::y);
        const Vec2Lanes velocity = Vec2Lanes::LoadMembersPartial(velocities.data() + begin, batchCount, 0.0F, &EcsVelocity::x, &EcsVelocity::y);
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        position.StoreMembersPartial(positions.data() + begin, batchCount, &EcsPosition::x, &EcsPosition::y);
    }
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityTransformKernel(
    const std::array<EcsKernelLocalTransform, Count>& localTransforms,
    std::array<EcsKernelWorldTransform, Count>& worldTransforms) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;
        const FloatLanes translationX = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationX);
        const FloatLanes translationY = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationY);
        const FloatLanes translationZ = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationZ);
        const FloatLanes rotationZ = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::rotationZ);
        const FloatLanes scaleX = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleX);
        const FloatLanes scaleY = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleY);
        const FloatLanes scaleZ = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleZ);

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            const float cosZ = std::cos(rotationZ.Lane(lane));
            const float sinZ = std::sin(rotationZ.Lane(lane));
            EcsKernelWorldTransform& world = worldTransforms[begin + lane];

            world.matrix[0] = cosZ * scaleX.Lane(lane);
            world.matrix[1] = sinZ * scaleX.Lane(lane);
            world.matrix[2] = 0.0F;
            world.matrix[3] = 0.0F;
            world.matrix[4] = -sinZ * scaleY.Lane(lane);
            world.matrix[5] = cosZ * scaleY.Lane(lane);
            world.matrix[6] = 0.0F;
            world.matrix[7] = 0.0F;
            world.matrix[8] = 0.0F;
            world.matrix[9] = 0.0F;
            world.matrix[10] = scaleZ.Lane(lane);
            world.matrix[11] = 0.0F;
            world.matrix[12] = translationX.Lane(lane);
            world.matrix[13] = translationY.Lane(lane);
            world.matrix[14] = translationZ.Lane(lane);
            world.matrix[15] = 1.0F;
        }
    }
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityBoundsKernel(
    const std::array<EcsKernelLocalBounds, Count>& localBounds,
    const std::array<EcsKernelLocalTransform, Count>& transforms,
    std::array<EcsKernelWorldBounds, Count>& worldBounds) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;
        const FloatLanes centerX = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::centerX);
        const FloatLanes centerY = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::centerY);
        const FloatLanes centerZ = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::centerZ);
        const FloatLanes extentX = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::extentX);
        const FloatLanes extentY = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::extentY);
        const FloatLanes extentZ = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::extentZ);
        const FloatLanes translationX = FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationX);
        const FloatLanes translationY = FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationY);
        const FloatLanes translationZ = FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationZ);
        const FloatLanes scaleX = AbsLanes(FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleX));
        const FloatLanes scaleY = AbsLanes(FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleY));
        const FloatLanes scaleZ = AbsLanes(FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleZ));

        const FloatLanes worldCenterX = kb::ecs::KernelDeterministicMulAdd(centerX, scaleX, translationX);
        const FloatLanes worldCenterY = kb::ecs::KernelDeterministicMulAdd(centerY, scaleY, translationY);
        const FloatLanes worldCenterZ = kb::ecs::KernelDeterministicMulAdd(centerZ, scaleZ, translationZ);
        const FloatLanes worldExtentX = extentX * scaleX;
        const FloatLanes worldExtentY = extentY * scaleY;
        const FloatLanes worldExtentZ = extentZ * scaleZ;

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            EcsKernelWorldBounds& bounds = worldBounds[begin + lane];
            bounds.minX = worldCenterX.Lane(lane) - worldExtentX.Lane(lane);
            bounds.minY = worldCenterY.Lane(lane) - worldExtentY.Lane(lane);
            bounds.minZ = worldCenterZ.Lane(lane) - worldExtentZ.Lane(lane);
            bounds.maxX = worldCenterX.Lane(lane) + worldExtentX.Lane(lane);
            bounds.maxY = worldCenterY.Lane(lane) + worldExtentY.Lane(lane);
            bounds.maxZ = worldCenterZ.Lane(lane) + worldExtentZ.Lane(lane);
        }
    }
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityPhysicsProxyKernel(
    std::array<EcsKernelPhysicsBody, Count>& bodies,
    std::array<EcsKernelPhysicsProxy, Count>& proxies) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec3Lanes = kb::ecs::KernelFloat3Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float gravityY = -9.8125F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;
        Vec3Lanes position = Vec3Lanes::LoadMembersPartial(
            bodies.data() + begin,
            batchCount,
            0.0F,
            &EcsKernelPhysicsBody::positionX,
            &EcsKernelPhysicsBody::positionY,
            &EcsKernelPhysicsBody::positionZ);
        Vec3Lanes velocity = Vec3Lanes::LoadMembersPartial(
            bodies.data() + begin,
            batchCount,
            0.0F,
            &EcsKernelPhysicsBody::velocityX,
            &EcsKernelPhysicsBody::velocityY,
            &EcsKernelPhysicsBody::velocityZ);
        const FloatLanes inverseMass = FloatLanes::LoadMemberPartial(bodies.data() + begin, batchCount, &EcsKernelPhysicsBody::inverseMass);
        const FloatLanes gravityStep = FloatLanes::Splat(gravityY * deltaSeconds) * inverseMass;

        velocity.template Component<1>() = velocity.template Component<1>() + gravityStep;
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        const FloatLanes speed = SqrtLanes(kb::ecs::KernelDot(velocity, velocity));
        const FloatLanes radius = FloatLanes::LoadMemberPartial(bodies.data() + begin, batchCount, &EcsKernelPhysicsBody::radius);
        const FloatLanes sweptRadius = radius + speed * FloatLanes::Splat(deltaSeconds);

        velocity.StoreMembersPartial(
            bodies.data() + begin,
            batchCount,
            &EcsKernelPhysicsBody::velocityX,
            &EcsKernelPhysicsBody::velocityY,
            &EcsKernelPhysicsBody::velocityZ);
        position.StoreMembersPartial(
            bodies.data() + begin,
            batchCount,
            &EcsKernelPhysicsBody::positionX,
            &EcsKernelPhysicsBody::positionY,
            &EcsKernelPhysicsBody::positionZ);

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            EcsKernelPhysicsProxy& proxy = proxies[begin + lane];
            proxy.predictedX = position.template Component<0>().Lane(lane);
            proxy.predictedY = position.template Component<1>().Lane(lane);
            proxy.predictedZ = position.template Component<2>().Lane(lane);
            proxy.sweptRadius = sweptRadius.Lane(lane);
        }
    }
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsPosition, Count> CreateCompatibilityPositions() noexcept {
    std::array<EcsPosition, Count> positions{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        positions[index] = EcsPosition{
            .x = sample * 0.1875F - 4.0F,
            .y = 2.5F - sample * 0.09375F,
        };
    }
    return positions;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsVelocity, Count> CreateCompatibilityVelocities() noexcept {
    std::array<EcsVelocity, Count> velocities{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        velocities[index] = EcsVelocity{
            .x = sample * 0.015625F - 0.375F,
            .y = 0.875F - sample * 0.0234375F,
        };
    }
    return velocities;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsKernelLocalTransform, Count> CreateCompatibilityTransforms() noexcept {
    std::array<EcsKernelLocalTransform, Count> transforms{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        transforms[index] = EcsKernelLocalTransform{
            .translationX = sample * 0.125F - 3.5F,
            .translationY = 1.75F - sample * 0.0625F,
            .translationZ = sample * 0.03125F - 0.75F,
            .rotationZ = sample * 0.02734375F - 0.3125F,
            .scaleX = 0.75F + static_cast<float>(index % 5U) * 0.125F,
            .scaleY = -1.25F + static_cast<float>(index % 7U) * 0.1875F,
            .scaleZ = 0.5F + static_cast<float>(index % 3U) * 0.25F,
        };
    }
    return transforms;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsKernelLocalBounds, Count> CreateCompatibilityBounds() noexcept {
    std::array<EcsKernelLocalBounds, Count> bounds{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        bounds[index] = EcsKernelLocalBounds{
            .centerX = sample * 0.03125F - 0.5F,
            .centerY = 0.25F - sample * 0.015625F,
            .centerZ = sample * 0.046875F - 0.875F,
            .extentX = 0.25F + static_cast<float>(index % 4U) * 0.0625F,
            .extentY = 0.375F + static_cast<float>(index % 5U) * 0.03125F,
            .extentZ = 0.5F + static_cast<float>(index % 6U) * 0.046875F,
        };
    }
    return bounds;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsKernelPhysicsBody, Count> CreateCompatibilityPhysicsBodies() noexcept {
    std::array<EcsKernelPhysicsBody, Count> bodies{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        bodies[index] = EcsKernelPhysicsBody{
            .positionX = sample * 0.09375F - 2.0F,
            .positionY = 3.0F - sample * 0.0546875F,
            .positionZ = sample * 0.0390625F - 0.25F,
            .velocityX = sample * 0.017578125F - 0.375F,
            .velocityY = 1.25F - sample * 0.025390625F,
            .velocityZ = sample * 0.01171875F + 0.125F,
            .radius = 0.25F + static_cast<float>(index % 5U) * 0.03125F,
            .inverseMass = 0.25F + static_cast<float>(index % 4U) * 0.125F,
        };
    }
    return bodies;
}

template <std::size_t Count>
void RequirePositionsBitEqual(const std::array<EcsPosition, Count>& expected, const std::array<EcsPosition, Count>& actual, const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        kb::tests::Require(FloatBits(expected[index].x) == FloatBits(actual[index].x), message);
        kb::tests::Require(FloatBits(expected[index].y) == FloatBits(actual[index].y), message);
    }
}

template <std::size_t Count>
void RequireWorldTransformsBitEqual(
    const std::array<EcsKernelWorldTransform, Count>& expected,
    const std::array<EcsKernelWorldTransform, Count>& actual,
    const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        for (std::size_t element = 0; element < 16U; ++element) {
            kb::tests::Require(FloatBits(expected[index].matrix[element]) == FloatBits(actual[index].matrix[element]), message);
        }
    }
}

template <std::size_t Count>
void RequireWorldBoundsBitEqual(
    const std::array<EcsKernelWorldBounds, Count>& expected,
    const std::array<EcsKernelWorldBounds, Count>& actual,
    const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        kb::tests::Require(FloatBits(expected[index].minX) == FloatBits(actual[index].minX), message);
        kb::tests::Require(FloatBits(expected[index].minY) == FloatBits(actual[index].minY), message);
        kb::tests::Require(FloatBits(expected[index].minZ) == FloatBits(actual[index].minZ), message);
        kb::tests::Require(FloatBits(expected[index].maxX) == FloatBits(actual[index].maxX), message);
        kb::tests::Require(FloatBits(expected[index].maxY) == FloatBits(actual[index].maxY), message);
        kb::tests::Require(FloatBits(expected[index].maxZ) == FloatBits(actual[index].maxZ), message);
    }
}

template <std::size_t Count>
void RequirePhysicsBitEqual(
    const std::array<EcsKernelPhysicsBody, Count>& expectedBodies,
    const std::array<EcsKernelPhysicsBody, Count>& actualBodies,
    const std::array<EcsKernelPhysicsProxy, Count>& expectedProxies,
    const std::array<EcsKernelPhysicsProxy, Count>& actualProxies,
    const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        kb::tests::Require(FloatBits(expectedBodies[index].positionX) == FloatBits(actualBodies[index].positionX), message);
        kb::tests::Require(FloatBits(expectedBodies[index].positionY) == FloatBits(actualBodies[index].positionY), message);
        kb::tests::Require(FloatBits(expectedBodies[index].positionZ) == FloatBits(actualBodies[index].positionZ), message);
        kb::tests::Require(FloatBits(expectedBodies[index].velocityX) == FloatBits(actualBodies[index].velocityX), message);
        kb::tests::Require(FloatBits(expectedBodies[index].velocityY) == FloatBits(actualBodies[index].velocityY), message);
        kb::tests::Require(FloatBits(expectedBodies[index].velocityZ) == FloatBits(actualBodies[index].velocityZ), message);
        kb::tests::Require(FloatBits(expectedProxies[index].predictedX) == FloatBits(actualProxies[index].predictedX), message);
        kb::tests::Require(FloatBits(expectedProxies[index].predictedY) == FloatBits(actualProxies[index].predictedY), message);
        kb::tests::Require(FloatBits(expectedProxies[index].predictedZ) == FloatBits(actualProxies[index].predictedZ), message);
        kb::tests::Require(FloatBits(expectedProxies[index].sweptRadius) == FloatBits(actualProxies[index].sweptRadius), message);
    }
}

template <typename BackendTag>
void RunKernelScalarSimdCompatibilityCase(const char* message) {
    constexpr std::size_t kCount = 29U;

    const std::array<EcsVelocity, kCount> velocities = CreateCompatibilityVelocities<kCount>();
    std::array<EcsPosition, kCount> expectedPositions = CreateCompatibilityPositions<kCount>();
    std::array<EcsPosition, kCount> actualPositions = expectedPositions;
    RunCompatibilityMovementKernel<kb::ecs::KernelScalarTag>(expectedPositions, velocities);
    RunCompatibilityMovementKernel<BackendTag>(actualPositions, velocities);
    RequirePositionsBitEqual(expectedPositions, actualPositions, message);

    const std::array<EcsKernelLocalTransform, kCount> transforms = CreateCompatibilityTransforms<kCount>();
    std::array<EcsKernelWorldTransform, kCount> expectedWorldTransforms{};
    std::array<EcsKernelWorldTransform, kCount> actualWorldTransforms{};
    RunCompatibilityTransformKernel<kb::ecs::KernelScalarTag>(transforms, expectedWorldTransforms);
    RunCompatibilityTransformKernel<BackendTag>(transforms, actualWorldTransforms);
    RequireWorldTransformsBitEqual(expectedWorldTransforms, actualWorldTransforms, message);

    const std::array<EcsKernelLocalBounds, kCount> localBounds = CreateCompatibilityBounds<kCount>();
    std::array<EcsKernelWorldBounds, kCount> expectedWorldBounds{};
    std::array<EcsKernelWorldBounds, kCount> actualWorldBounds{};
    RunCompatibilityBoundsKernel<kb::ecs::KernelScalarTag>(localBounds, transforms, expectedWorldBounds);
    RunCompatibilityBoundsKernel<BackendTag>(localBounds, transforms, actualWorldBounds);
    RequireWorldBoundsBitEqual(expectedWorldBounds, actualWorldBounds, message);

    std::array<EcsKernelPhysicsBody, kCount> expectedBodies = CreateCompatibilityPhysicsBodies<kCount>();
    std::array<EcsKernelPhysicsBody, kCount> actualBodies = expectedBodies;
    std::array<EcsKernelPhysicsProxy, kCount> expectedProxies{};
    std::array<EcsKernelPhysicsProxy, kCount> actualProxies{};
    RunCompatibilityPhysicsProxyKernel<kb::ecs::KernelScalarTag>(expectedBodies, expectedProxies);
    RunCompatibilityPhysicsProxyKernel<BackendTag>(actualBodies, actualProxies);
    RequirePhysicsBitEqual(expectedBodies, actualBodies, expectedProxies, actualProxies, message);
}

void RunEcsKernelContractScalarExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 8,
    });

    for (int index = 0; index < 17; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
    }

    kb::ecs::SystemAccess access = kb::ecs::DeclareKernelAccess<EcsMovementKernelContract>(world);
    kb::tests::Require(access.ReadComponents().size() == 1U, "ECS kernel access contract did not declare input component reads");
    kb::tests::Require(access.WriteComponents().size() == 1U, "ECS kernel access contract did not declare output component writes");
    kb::tests::Require(access.ReadComponents().front() != access.WriteComponents().front(), "ECS kernel access contract overlapped input and output components");

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    const EcsMovementScaleAsset asset{ .multiplier = 3.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.5F,
        .drag = 1.0F,
    };

    kb::ecs::ExecuteKernelScalar<EcsMovementKernelContract>(
        query,
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 8,
            .prefetchDistance = 4,
        },
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);

    int visited = 0;
    float sumX = 0.0F;
    float sumY = 0.0F;
    query.ForEachBatchKernel([&visited, &sumX, &sumY](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sumX += positions[index].x;
            sumY += positions[index].y;
        }
    });

    kb::tests::Require(visited == 17, "ECS scalar kernel did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(sumX, 187.0F), "ECS scalar kernel did not apply input components, output components, assets and constants to X");
    kb::tests::Require(kb::tests::NearlyEqual(sumY, 85.0F), "ECS scalar kernel did not apply input components, output components, assets and constants to Y");
}

void RunEcsKernelRequestedSimdFallsBackToScalarTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 5,
    });
    PopulateSimdProbeWorld(world, 13);

    int scalarBatches = 0;
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::QueryBatchExecutionScratch scratch;
    query.PrepareMutableBatchExecution(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 }, scratch);
    kb::ecs::ExecuteKernelAvx2<EcsSimdKernelContract>(
        query,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
        EcsScalarOnlyProbeKernel{ .scalarBatches = &scalarBatches },
        kb::ecs::BindKernelAssets(),
        constants,
        scratch);

    kb::tests::Require(scalarBatches == 3, "ECS kernel did not use scalar fallback for a requested SIMD backend without an overload");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 39.0F), "ECS scalar fallback did not process all kernel rows");
}

void RunEcsKernelSse2AndAvx2DispatchTest() {
    const kb::ecs::KernelNoConstants constants{};

    {
        kb::ecs::World world(kb::ecs::WorldConfig{
            .executionGrainSize = 4,
        });
        PopulateSimdProbeWorld(world, 8);

        int scalarBatches = 0;
        int sse2Batches = 0;
        int avx2Batches = 0;
        int avx512Batches = 0;
        int neonBatches = 0;
        kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
        auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
            std::move(query),
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
            kb::ecs::KernelBackendPreference::Sse2,
            EcsSimdProbeKernel{
                .scalarBatches = &scalarBatches,
                .sse2Batches = &sse2Batches,
                .avx2Batches = &avx2Batches,
                .avx512Batches = &avx512Batches,
                .neonBatches = &neonBatches,
            },
            kb::ecs::BindKernelAssets(),
            constants);

        compiled.Execute();
        kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
        if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Sse2)) {
            kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Sse2, "ECS kernel did not resolve the requested SSE2 backend");
            kb::tests::Require(sse2Batches == 2 && scalarBatches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not dispatch through the SSE2 overload");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 48.0F), "ECS SSE2 kernel path did not process all rows");
        } else {
            kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve SSE2 to scalar on unsupported hardware");
            kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not fall back to scalar when SSE2 was unavailable");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for SSE2 did not process all rows");
        }
    }

    {
        kb::ecs::World world(kb::ecs::WorldConfig{
            .executionGrainSize = 4,
        });
        PopulateSimdProbeWorld(world, 8);

        int scalarBatches = 0;
        int sse2Batches = 0;
        int avx2Batches = 0;
        int avx512Batches = 0;
        int neonBatches = 0;
        kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
        kb::ecs::ExecuteKernelAvx2<EcsSimdKernelContract>(
            query,
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
            EcsSimdProbeKernel{
                .scalarBatches = &scalarBatches,
                .sse2Batches = &sse2Batches,
                .avx2Batches = &avx2Batches,
                .avx512Batches = &avx512Batches,
                .neonBatches = &neonBatches,
            },
            kb::ecs::BindKernelAssets(),
            constants);

        if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
            kb::tests::Require(avx2Batches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not dispatch through the AVX2 overload");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 96.0F), "ECS AVX2 kernel path did not process all rows");
        } else {
            kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not fall back to scalar when AVX2 was unavailable");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 24.0F), "ECS scalar fallback for AVX2 did not process all rows");
        }
    }
}

void RunEcsKernelNeonDispatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    int neonBatches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Neon,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
            .neonBatches = &neonBatches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    compiled.Execute();
    kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Neon)) {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Neon, "ECS kernel did not resolve the requested NEON backend");
        kb::tests::Require(neonBatches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not dispatch through the NEON overload");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 72.0F), "ECS NEON kernel path did not process all rows");
    } else {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve NEON to scalar on unsupported hardware");
        kb::tests::Require(scalarBatches == 2 && neonBatches == 0 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not fall back to scalar when NEON was unavailable");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for NEON did not process all rows");
    }
}

void RunEcsKernelAvx512DispatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    int neonBatches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Avx512,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
            .neonBatches = &neonBatches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    compiled.Execute();
    kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx512)) {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Avx512, "ECS kernel did not resolve the requested AVX-512 backend");
        kb::tests::Require(avx512Batches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx2Batches == 0 && neonBatches == 0, "ECS kernel did not dispatch through the AVX-512 overload");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 192.0F), "ECS AVX-512 kernel path did not process all rows");
    } else {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve AVX-512 to scalar on unsupported hardware");
        kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not fall back to scalar when AVX-512 was unavailable");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for AVX-512 did not process all rows");
    }
}

void RunEcsCompiledKernelBackendPreferenceUpdateTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    int neonBatches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Scalar,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
            .neonBatches = &neonBatches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS compiled kernel did not cache the scalar backend");
    compiled.Execute();
    kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS compiled kernel did not execute the cached scalar backend");

    compiled.SetBackendPreference(kb::ecs::KernelBackendPreference::Sse2);
    const kb::ecs::KernelBackend expectedBackend = kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Sse2)
        ? kb::ecs::KernelBackend::Sse2
        : kb::ecs::KernelBackend::Scalar;
    kb::tests::Require(compiled.ResolvedBackend() == expectedBackend, "ECS compiled kernel did not refresh the cached backend after preference change");

    compiled.Execute();
    if (expectedBackend == kb::ecs::KernelBackend::Sse2) {
        kb::tests::Require(scalarBatches == 2 && sse2Batches == 2 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS compiled kernel did not execute the refreshed SSE2 backend");
    } else {
        kb::tests::Require(scalarBatches == 4 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS compiled kernel did not execute the refreshed scalar fallback backend");
    }
}

void RunEcsCompiledKernelQueryExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    for (int index = 0; index < 10; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
    }

    const EcsMovementScaleAsset asset{ .multiplier = 4.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.25F,
        .drag = 0.5F,
    };

    auto kernelQuery = kb::ecs::CompileKernelQuery<EcsMovementKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 4,
            .prefetchDistance = 2,
        },
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);

    kb::tests::Require(kernelQuery.IsValid(), "Compiled ECS kernel query did not create a valid typed query");
    kb::tests::Require(kernelQuery.Settings().maxBatchSize == 4U, "Compiled ECS kernel query did not retain execution settings");

    kernelQuery.Execute();
    kernelQuery.Execute();

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    int visited = 0;
    float sumX = 0.0F;
    float sumY = 0.0F;
    query.ForEachBatchKernel([&visited, &sumX, &sumY](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sumX += positions[index].x;
            sumY += positions[index].y;
        }
    });

    kb::tests::Require(visited == 10, "Compiled ECS kernel query did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(sumX, 85.0F), "Compiled ECS kernel query did not persist output component writes to X");
    kb::tests::Require(kb::tests::NearlyEqual(sumY, 80.0F), "Compiled ECS kernel query did not persist output component writes to Y");
}

void RunEcsKernelAlignedComponentContractTest() {
    static_assert(alignof(EcsKernelAlignedInput) == 32U);
    static_assert(alignof(EcsKernelAlignedOutput) == 32U);

    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    for (int index = 0; index < 9; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsKernelAlignedInput{ .x = static_cast<float>(index) });
        world.Set(entity, EcsKernelAlignedOutput{});
    }

    auto compiled = kb::ecs::CompileKernelQuery<EcsAlignedKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Avx2,
        EcsAlignedKernel{},
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});
    compiled.Execute();

    kb::ecs::KernelQuery<EcsAlignedKernelContract> query = world.CreateQuery<EcsKernelAlignedInput, EcsKernelAlignedOutput>();
    int visited = 0;
    float sum = 0.0F;
    query.ForEachBatchKernel([&visited, &sum](const kb::ecs::QueryBatch<EcsKernelAlignedInput, EcsKernelAlignedOutput>& batch) {
        const EcsKernelAlignedOutput* outputs = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sum += outputs[index].x;
        }
    });

    kb::tests::Require(visited == 9, "ECS aligned kernel contract did not visit all entities");
    kb::tests::Require(kb::tests::NearlyEqual(sum, 72.0F), "ECS aligned kernel contract did not write expected output");
}

void RunEcsCompiledKernelMatchesQueryPathTest() {
    constexpr int kCount = 17;
    kb::ecs::World kernelWorld(kb::ecs::WorldConfig{ .executionGrainSize = 5 });
    kb::ecs::World queryWorld(kb::ecs::WorldConfig{ .executionGrainSize = 5 });

    std::vector<kb::ecs::Entity> kernelEntities;
    std::vector<kb::ecs::Entity> queryEntities;
    kernelEntities.reserve(kCount);
    queryEntities.reserve(kCount);
    for (int index = 0; index < kCount; ++index) {
        const EcsPosition position{
            .x = static_cast<float>(index) * 0.25F - 2.0F,
            .y = static_cast<float>(index) * -0.125F + 3.0F,
        };
        const EcsVelocity velocity{
            .x = static_cast<float>(index) * 0.5F + 1.0F,
            .y = static_cast<float>(index) * -0.25F + 0.75F,
        };

        const kb::ecs::Entity kernelEntity = kernelWorld.CreateEntity();
        kernelWorld.Set(kernelEntity, position);
        kernelWorld.Set(kernelEntity, velocity);
        kernelEntities.push_back(kernelEntity);

        const kb::ecs::Entity queryEntity = queryWorld.CreateEntity();
        queryWorld.Set(queryEntity, position);
        queryWorld.Set(queryEntity, velocity);
        queryEntities.push_back(queryEntity);
    }

    const EcsMovementScaleAsset asset{ .multiplier = 3.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.125F,
        .drag = 0.5F,
    };
    auto compiled = kb::ecs::CompileKernelQuery<EcsMovementKernelContract>(
        kernelWorld,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
        kb::ecs::KernelBackendPreference::Scalar,
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);
    compiled.Execute();

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = queryWorld.CreateQuery<EcsVelocity, EcsPosition>();
    query.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 }, [&asset, &constants](kb::ecs::MutableQueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsVelocity* velocities = batch.Components<0>();
        EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            positions[index].x += velocities[index].x * constants.deltaSeconds * asset.multiplier;
            positions[index].y += velocities[index].y * constants.deltaSeconds * (asset.multiplier - constants.drag);
        }
    });

    for (std::size_t index = 0; index < kernelEntities.size(); ++index) {
        const EcsPosition* kernelPosition = kernelWorld.TryGet<EcsPosition>(kernelEntities[index]);
        const EcsPosition* queryPosition = queryWorld.TryGet<EcsPosition>(queryEntities[index]);
        kb::tests::Require(kernelPosition != nullptr && queryPosition != nullptr, "ECS compiled/query path comparison lost a position component");
        kb::tests::Require(FloatBits(kernelPosition->x) == FloatBits(queryPosition->x), "ECS compiled kernel path did not match query path X bit pattern");
        kb::tests::Require(FloatBits(kernelPosition->y) == FloatBits(queryPosition->y), "ECS compiled kernel path did not match query path Y bit pattern");
    }
}

void RunEcsCompiledKernelSystemRegistrationTest() {
    kb::ecs::World world;
    PopulateSimdProbeWorld(world, 12);

    int scalarBatches = 0;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .profilerEnabled = true } };
    scheduler.Add(
        std::make_unique<kb::ecs::CompiledKernelSystem<EcsSimdKernelContract, EcsScalarOnlyProbeKernel>>(
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
            EcsScalarOnlyProbeKernel{ .scalarBatches = &scalarBatches }),
        world);

    scheduler.Update(world, 0.0F);

    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    kb::tests::Require(scalarBatches > 0, "Compiled ECS kernel system did not execute any kernel batches");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 36.0F), "Compiled ECS kernel system did not update component data");
    kb::tests::Require(scheduler.LastProfilerTrace().events.size() == 1U, "Compiled ECS kernel system trace omitted execution event");
    kb::tests::Require(scheduler.LastProfilerTrace().events[0].executionPath == "compiled_kernel", "Compiled ECS kernel system trace did not report compiled kernel execution path");

    scheduler.Shutdown(world);
}

void RunEcsEditorHotReloadKernelPathTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 3,
    });
    PopulateSimdProbeWorld(world, 6);

    EcsEditorKernelContext context{
        .scale = 1.0F,
    };
    auto binding = kb::ecs::MakeEditorKernelBinding<EcsSimdKernelContract>(&EcsEditorHotReloadKernelV1, &context);
    auto compiled = kb::ecs::CompileEditorKernelQuery<EcsSimdKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 3 },
        binding,
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});

    kb::tests::Require(compiled.IsValid(), "ECS editor kernel query did not create a valid hot-reload binding");
    kb::tests::Require(compiled.Execute(), "ECS editor kernel query did not execute the initial binding");
    kb::ecs::KernelQuery<EcsSimdKernelContract> firstVerifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    kb::tests::Require(context.batches == 2, "ECS editor kernel path did not batch through the initial binding");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(firstVerifyQuery), 18.0F), "ECS editor kernel initial binding did not update all rows");

    binding->Bind(&EcsEditorHotReloadKernelV2, &context);
    kb::tests::Require(compiled.Execute(), "ECS editor kernel query did not execute the replaced binding");
    kb::ecs::KernelQuery<EcsSimdKernelContract> secondVerifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    kb::tests::Require(context.batches == 4, "ECS editor kernel path rebuilt or skipped batches after hot reload");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(secondVerifyQuery), 54.0F), "ECS editor kernel replaced binding did not update all rows");
}

void RunEcsEditorKernelClearedDuringExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 3,
    });
    PopulateSimdProbeWorld(world, 6);

    auto binding = kb::ecs::MakeEditorKernelBinding<EcsSimdKernelContract>(&EcsEditorClearingKernel, nullptr);
    EcsEditorClearingKernelContext context{
        .binding = binding.get(),
    };
    binding->Bind(&EcsEditorClearingKernel, &context);

    auto compiled = kb::ecs::CompileEditorKernelQuery<EcsSimdKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 3 },
        binding,
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});

    kb::tests::Require(!compiled.Execute(), "ECS editor kernel query did not report a binding cleared during execution");
    kb::tests::Require(context.batches == 1, "ECS editor kernel clearing test did not stop invoking cleared bindings");
}

void RunEcsKernelVectorMathBitDeterminismTest() {
    constexpr std::size_t kCount = 23U;
    std::array<EcsPosition, kCount> positions{};
    std::array<EcsVelocity, kCount> velocities{};

    for (std::size_t index = 0; index < kCount; ++index) {
        const float sample = static_cast<float>(index);
        positions[index] = EcsPosition{
            .x = sample * 0.125F - 1.5F,
            .y = 3.0F - sample * 0.0625F,
        };
        velocities[index] = EcsVelocity{
            .x = sample * 0.03125F - 0.5F,
            .y = 1.25F - sample * 0.046875F,
        };
    }

    const std::array<std::uint32_t, kCount * 2U> reference = ReferenceMovementBits(positions, velocities);
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelScalarTag>(positions, velocities) == reference, "ECS scalar vector math was not bit-for-bit with reference movement");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelNeonTag>(positions, velocities) == reference, "ECS NEON-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelSse2Tag>(positions, velocities) == reference, "ECS SSE2-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelAvx2Tag>(positions, velocities) == reference, "ECS AVX2-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelAvx512Tag>(positions, velocities) == reference, "ECS AVX-512-width vector math was not bit-for-bit deterministic");
}

void RunEcsKernelScalarSimdCompatibilityTest() {
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelNeonTag>("ECS NEON kernel compatibility output diverged from scalar");
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelSse2Tag>("ECS SSE2 kernel compatibility output diverged from scalar");
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelAvx2Tag>("ECS AVX2 kernel compatibility output diverged from scalar");
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelAvx512Tag>("ECS AVX-512 kernel compatibility output diverged from scalar");
}

template <typename BackendTag>
void RunKernelVectorMathUnalignedFallbackCase() {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

    alignas(FloatLanes::PreferredAlignment) std::array<float, FloatLanes::LaneCount> alignedValues{};
    for (std::size_t lane = 0; lane < FloatLanes::LaneCount; ++lane) {
        alignedValues[lane] = static_cast<float>(lane) * 0.5F - 2.0F;
    }
    kb::tests::Require(FloatLanes::IsAligned(alignedValues.data()), "ECS kernel aligned test buffer was not aligned to the backend lane width");
    const FloatLanes alignedLoaded = FloatLanes::LoadAligned(alignedValues.data());
    kb::tests::Require(FloatBits(alignedLoaded.Lane(FloatLanes::LaneCount - 1U)) == FloatBits(alignedValues.back()), "ECS kernel aligned load read invalid data");

    std::array<std::byte, sizeof(float) * FloatLanes::LaneCount + 1U> inputBytes{};
    std::array<std::byte, sizeof(float) * FloatLanes::LaneCount + 1U> outputBytes{};
    std::byte* input = inputBytes.data() + 1U;
    std::byte* output = outputBytes.data() + 1U;
    kb::tests::Require(!FloatLanes::IsAligned(input), "ECS kernel unaligned test input unexpectedly met backend alignment");
    kb::tests::Require(!FloatLanes::IsAligned(output), "ECS kernel unaligned test output unexpectedly met backend alignment");

    for (std::size_t lane = 0; lane < FloatLanes::LaneCount; ++lane) {
        const float value = static_cast<float>(lane) * 1.25F + 0.75F;
        std::memcpy(input + lane * sizeof(float), &value, sizeof(float));
    }

    const FloatLanes shifted = FloatLanes::Load(reinterpret_cast<const float*>(input)) + FloatLanes::Splat(3.5F);
    shifted.Store(reinterpret_cast<float*>(output));

    for (std::size_t lane = 0; lane < FloatLanes::LaneCount; ++lane) {
        const float expected = static_cast<float>(lane) * 1.25F + 4.25F;
        float actual = 0.0F;
        std::memcpy(&actual, output + lane * sizeof(float), sizeof(float));
        kb::tests::Require(FloatBits(actual) == FloatBits(expected), "ECS kernel vector math unaligned fallback did not preserve float bits");
    }
}

void RunEcsKernelVectorMathUnalignedFallbackTest() {
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelScalarTag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelNeonTag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelSse2Tag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelAvx2Tag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelAvx512Tag>();
}

template <typename BackendTag, std::size_t Count>
void RunHotKernelVectorCase(const std::array<EcsPosition, Count>& sourcePositions, const std::array<EcsVelocity, Count>& velocities, const std::array<EcsPosition, Count>& expected) {
    std::array<EcsPosition, Count> positions = sourcePositions;
    kb::ecs::ApplyDenseFloat2VelocityVector<BackendTag>(
        positions.data(),
        velocities.data(),
        positions.size(),
        0.125F,
        &EcsPosition::x,
        &EcsPosition::y,
        &EcsVelocity::x,
        &EcsVelocity::y);

    for (std::size_t index = 0U; index < positions.size(); ++index) {
        kb::tests::Require(FloatBits(positions[index].x) == FloatBits(expected[index].x), "ECS hot vector kernel X output diverged from scalar");
        kb::tests::Require(FloatBits(positions[index].y) == FloatBits(expected[index].y), "ECS hot vector kernel Y output diverged from scalar");
    }
}

template <typename BackendTag, std::size_t Count>
void RunHotKernelColumnVectorCase(
    const std::array<float, Count>& sourcePositionX,
    const std::array<float, Count>& sourcePositionY,
    const std::array<float, Count>& velocityX,
    const std::array<float, Count>& velocityY,
    const std::array<float, Count>& expectedX,
    const std::array<float, Count>& expectedY) {
    std::array<float, Count> positionX = sourcePositionX;
    std::array<float, Count> positionY = sourcePositionY;
    kb::ecs::ApplyDenseFloat2VelocityColumnsVector<BackendTag>(
        positionX.data(),
        positionY.data(),
        velocityX.data(),
        velocityY.data(),
        positionX.size(),
        0.0625F);

    for (std::size_t index = 0U; index < Count; ++index) {
        kb::tests::Require(FloatBits(positionX[index]) == FloatBits(expectedX[index]), "ECS hot SoA vector kernel X output diverged from scalar");
        kb::tests::Require(FloatBits(positionY[index]) == FloatBits(expectedY[index]), "ECS hot SoA vector kernel Y output diverged from scalar");
    }
}

void RunEcsHotKernelDenseFloat2UpdateTest() {
    constexpr std::size_t kCount = 31U;
    std::array<EcsPosition, kCount> sourcePositions{};
    std::array<EcsVelocity, kCount> velocities{};
    for (std::size_t index = 0U; index < kCount; ++index) {
        const float value = static_cast<float>(index);
        sourcePositions[index] = EcsPosition{
            .x = value * 0.5F - 7.0F,
            .y = 3.0F - value * 0.25F,
        };
        velocities[index] = EcsVelocity{
            .x = value * 0.03125F + 1.0F,
            .y = 0.75F - value * 0.015625F,
        };
    }

    std::array<EcsPosition, kCount> expected = sourcePositions;
    kb::ecs::ApplyDenseFloat2VelocityScalar(
        expected.data(),
        velocities.data(),
        expected.size(),
        0.125F,
        &EcsPosition::x,
        &EcsPosition::y,
        &EcsVelocity::x,
        &EcsVelocity::y);

    RunHotKernelVectorCase<kb::ecs::KernelScalarTag>(sourcePositions, velocities, expected);
    RunHotKernelVectorCase<kb::ecs::KernelNeonTag>(sourcePositions, velocities, expected);
    RunHotKernelVectorCase<kb::ecs::KernelSse2Tag>(sourcePositions, velocities, expected);
    RunHotKernelVectorCase<kb::ecs::KernelAvx2Tag>(sourcePositions, velocities, expected);
    RunHotKernelVectorCase<kb::ecs::KernelAvx512Tag>(sourcePositions, velocities, expected);

    kb::tests::Require(
        kb::ecs::CanUseDensePackedFloat2Velocity(&EcsPosition::x, &EcsPosition::y, &EcsVelocity::x, &EcsVelocity::y),
        "ECS hot packed float2 kernel did not recognize the canonical Position+Velocity layout");
    kb::tests::Require(
        !kb::ecs::CanUseDensePackedFloat2Velocity(&EcsPaddedPosition::x, &EcsPaddedPosition::y, &EcsPaddedVelocity::x, &EcsPaddedVelocity::y),
        "ECS hot packed float2 kernel accepted a padded component layout");

    std::array<EcsPosition, kCount> reduced = sourcePositions;
    const double reducedChecksum = kb::ecs::ApplyDenseFloat2VelocityAndReduceX(
        reduced.data(),
        velocities.data(),
        reduced.size(),
        0.125F,
        &EcsPosition::x,
        &EcsPosition::y,
        &EcsVelocity::x,
        &EcsVelocity::y,
        kb::ecs::KernelBackend::Sse2);
    double expectedChecksum = 0.0;
    for (std::size_t index = 0U; index < kCount; ++index) {
        kb::tests::Require(FloatBits(reduced[index].x) == FloatBits(expected[index].x), "ECS fused hot update wrote unexpected X");
        kb::tests::Require(FloatBits(reduced[index].y) == FloatBits(expected[index].y), "ECS fused hot update wrote unexpected Y");
        expectedChecksum += static_cast<double>(expected[index].x);
    }
    kb::tests::Require(std::fabs(reducedChecksum - expectedChecksum) <= 0.000001, "ECS fused hot update produced an invalid reduction");
}

void RunEcsHotKernelDenseColumnFloat2UpdateTest() {
    constexpr std::size_t kCount = 37U;
    std::array<float, kCount> sourcePositionX{};
    std::array<float, kCount> sourcePositionY{};
    std::array<float, kCount> velocityX{};
    std::array<float, kCount> velocityY{};
    for (std::size_t index = 0U; index < kCount; ++index) {
        const float value = static_cast<float>(index);
        sourcePositionX[index] = value * 0.25F - 2.0F;
        sourcePositionY[index] = 8.0F - value * 0.125F;
        velocityX[index] = value * 0.5F + 1.0F;
        velocityY[index] = -value * 0.25F - 0.5F;
    }

    std::array<float, kCount> expectedX = sourcePositionX;
    std::array<float, kCount> expectedY = sourcePositionY;
    kb::ecs::ApplyDenseFloat2VelocityColumnsScalar(
        expectedX.data(),
        expectedY.data(),
        velocityX.data(),
        velocityY.data(),
        expectedX.size(),
        0.0625F);

    RunHotKernelColumnVectorCase<kb::ecs::KernelScalarTag>(sourcePositionX, sourcePositionY, velocityX, velocityY, expectedX, expectedY);
    RunHotKernelColumnVectorCase<kb::ecs::KernelNeonTag>(sourcePositionX, sourcePositionY, velocityX, velocityY, expectedX, expectedY);
    RunHotKernelColumnVectorCase<kb::ecs::KernelSse2Tag>(sourcePositionX, sourcePositionY, velocityX, velocityY, expectedX, expectedY);
    RunHotKernelColumnVectorCase<kb::ecs::KernelAvx2Tag>(sourcePositionX, sourcePositionY, velocityX, velocityY, expectedX, expectedY);
    RunHotKernelColumnVectorCase<kb::ecs::KernelAvx512Tag>(sourcePositionX, sourcePositionY, velocityX, velocityY, expectedX, expectedY);
}

void RunEcsHotKernelRangeAndTrafficTest() {
    constexpr std::size_t kCount = 9U;
    std::array<EcsPosition, kCount> positions{};
    std::array<EcsVelocity, kCount> velocities{};
    for (std::size_t index = 0U; index < kCount; ++index) {
        positions[index] = EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index * 2U) };
        velocities[index] = EcsVelocity{ .x = 10.0F, .y = -2.0F };
    }

    kb::ecs::ApplyDenseFloat2Velocity(
        positions.data(),
        velocities.data(),
        kb::ecs::HotKernelRange{ .begin = 2U, .count = 4U },
        0.5F,
        &EcsPosition::x,
        &EcsPosition::y,
        &EcsVelocity::x,
        &EcsVelocity::y,
        kb::ecs::KernelBackend::Scalar);

    kb::tests::Require(kb::tests::NearlyEqual(positions[1].x, 1.0F) && kb::tests::NearlyEqual(positions[1].y, 2.0F), "ECS hot kernel range modified data before the selected range");
    kb::tests::Require(kb::tests::NearlyEqual(positions[2].x, 7.0F) && kb::tests::NearlyEqual(positions[2].y, 3.0F), "ECS hot kernel range did not update the first selected row");
    kb::tests::Require(kb::tests::NearlyEqual(positions[5].x, 10.0F) && kb::tests::NearlyEqual(positions[5].y, 9.0F), "ECS hot kernel range did not update the last selected row");
    kb::tests::Require(kb::tests::NearlyEqual(positions[6].x, 6.0F) && kb::tests::NearlyEqual(positions[6].y, 12.0F), "ECS hot kernel range modified data after the selected range");

    constexpr std::size_t kTrafficCount = 7U;
    constexpr kb::ecs::HotKernelMemoryTraffic traffic = kb::ecs::EstimateDenseFloat2VelocityUpdateTraffic<EcsPosition, EcsVelocity>(kTrafficCount);
    static_assert(traffic.bytesRead == kTrafficCount * (sizeof(EcsPosition) + sizeof(EcsVelocity)));
    static_assert(traffic.bytesWritten == kTrafficCount * sizeof(EcsPosition));
    static_assert(traffic.TotalBytes() == kTrafficCount * (sizeof(EcsPosition) * 2U + sizeof(EcsVelocity)));

    constexpr kb::ecs::HotKernelMemoryTraffic columnTraffic = kb::ecs::EstimateDenseFloat2VelocityColumnUpdateTraffic(kTrafficCount);
    static_assert(columnTraffic.bytesRead == kTrafficCount * sizeof(float) * 4U);
    static_assert(columnTraffic.bytesWritten == kTrafficCount * sizeof(float) * 2U);
    static_assert(columnTraffic.TotalBytes() == kTrafficCount * sizeof(float) * 6U);

    constexpr kb::ecs::MemoryTrafficEstimate directTraffic =
        kb::ecs::EstimateMemoryTraffic(10U, sizeof(EcsPosition) + sizeof(EcsVelocity), sizeof(EcsPosition));
    static_assert(directTraffic.entityCount == 10U);
    static_assert(directTraffic.bytesRead == 10U * (sizeof(EcsPosition) + sizeof(EcsVelocity)));
    static_assert(directTraffic.bytesWritten == 10U * sizeof(EcsPosition));
    static_assert(directTraffic.TotalBytes() == 10U * (sizeof(EcsPosition) * 2U + sizeof(EcsVelocity)));

    const std::array<std::size_t, 2U> componentSizes{ sizeof(EcsPosition), sizeof(EcsVelocity) };
    const kb::ecs::MemoryTrafficEstimate readOnlyTraffic =
        kb::ecs::EstimateReadOnlyQueryTraffic(5U, componentSizes);
    kb::tests::Require(readOnlyTraffic.bytesRead == 5U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS memory traffic estimator returned invalid read bytes");
    kb::tests::Require(readOnlyTraffic.bytesWritten == 0U, "ECS memory traffic estimator wrote bytes for read-only query traffic");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(readOnlyTraffic.BytesPerEntity()), static_cast<float>(sizeof(EcsPosition) + sizeof(EcsVelocity))), "ECS memory traffic estimator returned invalid read bytes/entity");

    const kb::ecs::MemoryTrafficEstimate mutableTraffic =
        kb::ecs::EstimateMutableQueryTraffic(5U, componentSizes);
    kb::tests::Require(mutableTraffic.bytesRead == 5U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS memory traffic estimator returned invalid mutable read bytes");
    kb::tests::Require(mutableTraffic.bytesWritten == 5U * (sizeof(EcsPosition) + sizeof(EcsVelocity)), "ECS memory traffic estimator returned invalid mutable write bytes");
    kb::tests::Require(mutableTraffic.TotalBytes() == readOnlyTraffic.TotalBytes() * 2U, "ECS memory traffic estimator did not count mutable read/write traffic");

    constexpr kb::ecs::MemoryBandwidthSample achieved = kb::ecs::EstimateMemoryBandwidth(500000000U, 100000000U);
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(achieved.GigabytesPerSecond()), 5.0F), "ECS memory traffic estimator returned invalid GB/s");
    constexpr kb::ecs::RooflineReport roofline =
        kb::ecs::BuildRooflineReport(achieved, kb::ecs::EstimateMemoryBandwidth(1000000000U, 100000000U), 45.0);
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(roofline.BaselinePercent()), 50.0F), "ECS roofline report returned invalid baseline percent");
    kb::tests::Require(roofline.MeetsTarget(), "ECS roofline report rejected a sample above target");
}

void RequireHotTransformMatrix(const EcsHotLocalTransform& local, const EcsHotWorldTransform& world, const char* label) {
    const float cosZ = static_cast<float>(std::cos(static_cast<double>(local.rotationZ)));
    const float sinZ = static_cast<float>(std::sin(static_cast<double>(local.rotationZ)));
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[0], cosZ * local.scaleX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[1], sinZ * local.scaleX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[4], -sinZ * local.scaleY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[5], cosZ * local.scaleY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[10], local.scaleZ), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[12], local.translationX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[13], local.translationY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[14], local.translationZ), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[15], 1.0F), label);
}

void RequireHotTransformAffine(const EcsHotLocalTransform& local, const EcsHotWorldAffineTransform& world, const char* label) {
    const float cosZ = static_cast<float>(std::cos(static_cast<double>(local.rotationZ)));
    const float sinZ = static_cast<float>(std::sin(static_cast<double>(local.rotationZ)));
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[0], cosZ * local.scaleX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[1], sinZ * local.scaleX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[2], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[3], -sinZ * local.scaleY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[4], cosZ * local.scaleY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[5], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[6], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[7], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[8], local.scaleZ), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[9], local.translationX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[10], local.translationY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[11], local.translationZ), label);
}

void RequireHotTranslationMatrix(const EcsHotLocalTransform& local, const EcsHotWorldTransform& world, const char* label) {
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[0], 1.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[1], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[2], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[3], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[4], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[5], 1.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[6], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[7], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[8], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[9], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[10], 1.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[11], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[12], local.translationX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[13], local.translationY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[14], local.translationZ), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.matrix[15], 1.0F), label);
}

void RequireHotTranslationAffine(const EcsHotLocalTransform& local, const EcsHotWorldAffineTransform& world, const char* label) {
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[0], 1.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[1], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[2], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[3], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[4], 1.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[5], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[6], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[7], 0.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[8], 1.0F), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[9], local.translationX), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[10], local.translationY), label);
    kb::tests::Require(kb::tests::NearlyEqual(world.affine[11], local.translationZ), label);
}

void RunEcsHotKernelTransformZToMatrix4x4Test() {
    constexpr std::size_t kCount = 5U;
    std::array<EcsHotLocalTransform, kCount> locals{
        EcsHotLocalTransform{ .translationX = 1.0F, .translationY = 2.0F, .translationZ = 3.0F, .rotationZ = 0.0F, .scaleX = 1.0F, .scaleY = 2.0F, .scaleZ = 3.0F },
        EcsHotLocalTransform{ .translationX = 4.0F, .translationY = 5.0F, .translationZ = 6.0F, .rotationZ = 0.25F, .scaleX = 1.5F, .scaleY = 1.25F, .scaleZ = 1.0F },
        EcsHotLocalTransform{ .translationX = 7.0F, .translationY = 8.0F, .translationZ = 9.0F, .rotationZ = -0.5F, .scaleX = 0.5F, .scaleY = 0.75F, .scaleZ = 1.25F },
        EcsHotLocalTransform{ .translationX = 10.0F, .translationY = 11.0F, .translationZ = 12.0F, .rotationZ = 1.0F, .scaleX = 2.0F, .scaleY = 2.5F, .scaleZ = 3.0F },
        EcsHotLocalTransform{ .translationX = 13.0F, .translationY = 14.0F, .translationZ = 15.0F, .rotationZ = -1.25F, .scaleX = 3.0F, .scaleY = 1.0F, .scaleZ = 0.25F },
    };
    std::array<EcsHotWorldTransform, kCount> worlds{};
    for (EcsHotWorldTransform& world : worlds) {
        world.matrix[0] = -99.0F;
        world.matrix[15] = -99.0F;
    }

    kb::ecs::ApplyDenseTransformZToMatrix4x4<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldTransform::matrix>(
        locals.data(),
        worlds.data(),
        kb::ecs::HotKernelRange{ .begin = 1U, .count = 3U });

    kb::tests::Require(kb::tests::NearlyEqual(worlds[0].matrix[0], -99.0F), "ECS transform hot kernel modified data before the selected range");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[4].matrix[0], -99.0F), "ECS transform hot kernel modified data after the selected range");
    RequireHotTransformMatrix(locals[1], worlds[1], "ECS transform hot kernel produced an invalid first selected matrix");
    RequireHotTransformMatrix(locals[2], worlds[2], "ECS transform hot kernel produced an invalid middle selected matrix");
    RequireHotTransformMatrix(locals[3], worlds[3], "ECS transform hot kernel produced an invalid last selected matrix");

    std::array<EcsHotWorldTransform, kCount> reducedWorlds{};
    const double reducedChecksum = kb::ecs::ApplyDenseTransformZToMatrix4x4ScalarAndReduce<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldTransform::matrix>(
        locals.data(),
        reducedWorlds.data(),
        locals.size());
    double expectedTransformChecksum = 0.0;
    for (std::size_t index = 0U; index < locals.size(); ++index) {
        RequireHotTransformMatrix(locals[index], reducedWorlds[index], "ECS fused transform hot kernel wrote an invalid matrix");
        expectedTransformChecksum += static_cast<double>(reducedWorlds[index].matrix[0])
            + static_cast<double>(reducedWorlds[index].matrix[5])
            + static_cast<double>(reducedWorlds[index].matrix[10])
            + static_cast<double>(reducedWorlds[index].matrix[12]);
    }
    kb::tests::Require(std::fabs(reducedChecksum - expectedTransformChecksum) <= 0.000001, "ECS fused transform hot kernel produced an invalid reduction");

    std::array<EcsHotWorldTransform, kCount> translationWorlds{};
    for (EcsHotWorldTransform& world : translationWorlds) {
        for (float& value : world.matrix) {
            value = -17.0F;
        }
    }
    kb::ecs::ApplyDenseTransformTranslationToMatrix4x4<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotWorldTransform::matrix>(
        locals.data(),
        translationWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 1U, .count = 3U });
    kb::tests::Require(kb::tests::NearlyEqual(translationWorlds[0].matrix[0], -17.0F), "ECS translation transform kernel modified data before the selected range");
    kb::tests::Require(kb::tests::NearlyEqual(translationWorlds[4].matrix[15], -17.0F), "ECS translation transform kernel modified data after the selected range");
    RequireHotTranslationMatrix(locals[1], translationWorlds[1], "ECS translation transform kernel produced an invalid first selected matrix");
    RequireHotTranslationMatrix(locals[2], translationWorlds[2], "ECS translation transform kernel produced an invalid middle selected matrix");
    RequireHotTranslationMatrix(locals[3], translationWorlds[3], "ECS translation transform kernel produced an invalid last selected matrix");

    std::array<EcsHotWorldTransform, kCount> reducedTranslationWorlds{};
    const double reducedTranslationChecksum = kb::ecs::ApplyDenseTransformTranslationToMatrix4x4ScalarAndReduce<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotWorldTransform::matrix>(
        locals.data(),
        reducedTranslationWorlds.data(),
        locals.size());
    double expectedTranslationChecksum = 0.0;
    for (std::size_t index = 0U; index < locals.size(); ++index) {
        RequireHotTranslationMatrix(locals[index], reducedTranslationWorlds[index], "ECS fused translation transform kernel wrote an invalid matrix");
        expectedTranslationChecksum += static_cast<double>(reducedTranslationWorlds[index].matrix[0])
            + static_cast<double>(reducedTranslationWorlds[index].matrix[5])
            + static_cast<double>(reducedTranslationWorlds[index].matrix[10])
            + static_cast<double>(reducedTranslationWorlds[index].matrix[12]);
    }
    kb::tests::Require(
        std::fabs(reducedTranslationChecksum - expectedTranslationChecksum) <= 0.000001,
        "ECS fused translation transform kernel produced an invalid reduction");

    std::array<EcsHotWorldAffineTransform, kCount> translationAffineWorlds{};
    for (EcsHotWorldAffineTransform& world : translationAffineWorlds) {
        for (float& value : world.affine) {
            value = -19.0F;
        }
    }
    kb::ecs::ApplyDenseTransformTranslationToAffine3x4<
        EcsHotLocalTransform,
        EcsHotWorldAffineTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotWorldAffineTransform::affine>(
        locals.data(),
        translationAffineWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 1U, .count = 3U });
    kb::tests::Require(kb::tests::NearlyEqual(translationAffineWorlds[0].affine[0], -19.0F), "ECS translation affine kernel modified data before the selected range");
    kb::tests::Require(kb::tests::NearlyEqual(translationAffineWorlds[4].affine[11], -19.0F), "ECS translation affine kernel modified data after the selected range");
    RequireHotTranslationAffine(locals[1], translationAffineWorlds[1], "ECS translation affine kernel produced an invalid first selected transform");
    RequireHotTranslationAffine(locals[2], translationAffineWorlds[2], "ECS translation affine kernel produced an invalid middle selected transform");
    RequireHotTranslationAffine(locals[3], translationAffineWorlds[3], "ECS translation affine kernel produced an invalid last selected transform");

    std::array<EcsHotWorldAffineTransform, kCount> reducedTranslationAffineWorlds{};
    const double reducedTranslationAffineChecksum = kb::ecs::ApplyDenseTransformTranslationToAffine3x4AndReduce<
        EcsHotLocalTransform,
        EcsHotWorldAffineTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotWorldAffineTransform::affine>(
        locals.data(),
        reducedTranslationAffineWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 0U, .count = locals.size() });
    double expectedTranslationAffineChecksum = 0.0;
    for (std::size_t index = 0U; index < locals.size(); ++index) {
        RequireHotTranslationAffine(locals[index], reducedTranslationAffineWorlds[index], "ECS fused translation affine kernel wrote an invalid transform");
        expectedTranslationAffineChecksum += static_cast<double>(reducedTranslationAffineWorlds[index].affine[0])
            + static_cast<double>(reducedTranslationAffineWorlds[index].affine[4])
            + static_cast<double>(reducedTranslationAffineWorlds[index].affine[8])
            + static_cast<double>(reducedTranslationAffineWorlds[index].affine[9]);
    }
    kb::tests::Require(
        std::fabs(reducedTranslationAffineChecksum - expectedTranslationAffineChecksum) <= 0.000001,
        "ECS fused translation affine kernel produced an invalid reduction");

    std::array<EcsHotTransformVersionState, kCount> translationVersionStates{};
    std::array<EcsHotWorldTransform, kCount> versionedTranslationWorlds{};
    const kb::ecs::DenseTransformVersionedApplyStats firstTranslationStats =
        kb::ecs::ApplyDenseTransformTranslationToMatrix4x4VersionedAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotWorldTransform::matrix>(
            locals.data(),
            versionedTranslationWorlds.data(),
            translationVersionStates.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = translationVersionStates.size() });
    kb::tests::Require(firstTranslationStats.visited == kCount, "ECS versioned translation transform kernel did not visit the full range");
    kb::tests::Require(firstTranslationStats.updated == kCount, "ECS versioned translation transform kernel did not update dirty transforms");
    kb::tests::Require(firstTranslationStats.skipped == 0U, "ECS versioned translation transform kernel skipped dirty transforms");
    for (std::size_t index = 0U; index < kCount; ++index) {
        kb::tests::Require(
            translationVersionStates[index].appliedLocalVersion == translationVersionStates[index].localVersion,
            "ECS versioned translation transform kernel did not publish applied version");
        RequireHotTranslationMatrix(locals[index], versionedTranslationWorlds[index], "ECS versioned translation transform kernel wrote an invalid matrix");
    }

    for (EcsHotWorldTransform& world : versionedTranslationWorlds) {
        world.matrix[0] = -42.0F;
    }
    const kb::ecs::DenseTransformVersionedApplyStats cleanTranslationStats =
        kb::ecs::ApplyDenseTransformTranslationToMatrix4x4VersionedAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotWorldTransform::matrix>(
            locals.data(),
            versionedTranslationWorlds.data(),
            translationVersionStates.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = translationVersionStates.size() });
    kb::tests::Require(cleanTranslationStats.updated == 0U, "ECS versioned translation transform clean pass updated data");
    kb::tests::Require(cleanTranslationStats.skipped == kCount, "ECS versioned translation transform clean pass skipped an invalid count");
    for (const EcsHotWorldTransform& world : versionedTranslationWorlds) {
        kb::tests::Require(kb::tests::NearlyEqual(world.matrix[0], -42.0F), "ECS versioned translation transform clean pass wrote matrix data");
    }

    ++translationVersionStates[2].localVersion;
    std::array<kb::ecs::DenseTransformVersionRangeSummary, 2U> translationSummaries{};
    const std::size_t translationSummaryCount =
        kb::ecs::BuildDenseTransformVersionRangeSummaries<
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion>(
            translationVersionStates.data(),
            translationVersionStates.size(),
            3U,
            translationSummaries);
    kb::tests::Require(translationSummaryCount == translationSummaries.size(), "ECS translation transform summary builder returned an invalid count");
    const kb::ecs::DenseTransformVersionedApplyStats summaryTranslationStats =
        kb::ecs::ApplyDenseTransformTranslationToMatrix4x4VersionedSummariesAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotWorldTransform::matrix>(
            locals.data(),
            versionedTranslationWorlds.data(),
            translationVersionStates.data(),
            translationSummaries);
    kb::tests::Require(summaryTranslationStats.updated == 1U, "ECS versioned translation summary pass did not update exactly one transform");
    kb::tests::Require(summaryTranslationStats.skipped == kCount - 1U, "ECS versioned translation summary pass skipped an invalid count");
    kb::tests::Require(
        translationVersionStates[2].appliedLocalVersion == translationVersionStates[2].localVersion,
        "ECS versioned translation summary pass did not publish the dirty version");
    RequireHotTranslationMatrix(locals[2], versionedTranslationWorlds[2], "ECS versioned translation summary pass wrote an invalid dirty matrix");
    for (const kb::ecs::DenseTransformVersionRangeSummary& summary : translationSummaries) {
        kb::tests::Require(summary.dirtyCount == 0U, "ECS versioned translation summary pass did not clear dirty summaries");
    }

    kb::ecs::World translationWorld(kb::ecs::WorldConfig{ .executionGrainSize = 4 });
    std::vector<kb::ecs::Entity> translationEntities;
    translationEntities.reserve(kCount);
    for (std::size_t index = 0U; index < kCount; ++index) {
        const kb::ecs::Entity entity = translationWorld.CreateEntity();
        translationWorld.Set(entity, locals[index]);
        translationWorld.Set(entity, EcsHotWorldTransform{});
        translationEntities.push_back(entity);
    }
    kb::ecs::Query<EcsHotLocalTransform, EcsHotWorldTransform> translationQuery =
        translationWorld.CreateQuery<EcsHotLocalTransform, EcsHotWorldTransform>();
    double batchTranslationChecksum = 0.0;
    std::size_t batchTranslationVisited = 0U;
    translationQuery.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 2 },
        [&batchTranslationChecksum, &batchTranslationVisited](kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldTransform>& batch) {
            using Batch = kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldTransform>;
            batchTranslationChecksum += kb::ecs::ApplyDenseTransformTranslationToMatrix4x4BatchAndReduce<
                Batch,
                EcsHotLocalTransform,
                EcsHotWorldTransform,
                &EcsHotLocalTransform::translationX,
                &EcsHotLocalTransform::translationY,
                &EcsHotLocalTransform::translationZ,
                &EcsHotWorldTransform::matrix>(batch);
            batchTranslationVisited += batch.Count();
        });
    kb::tests::Require(batchTranslationVisited == kCount, "ECS translation transform batch helper did not visit every entity");
    kb::tests::Require(
        std::fabs(batchTranslationChecksum - expectedTranslationChecksum) <= 0.000001,
        "ECS translation transform batch helper produced an invalid reduction");
    for (std::size_t index = 0U; index < translationEntities.size(); ++index) {
        const EcsHotWorldTransform* world = translationWorld.TryGet<EcsHotWorldTransform>(translationEntities[index]);
        kb::tests::Require(world != nullptr, "ECS translation transform batch helper lost a world transform");
        RequireHotTranslationMatrix(locals[index], *world, "ECS translation transform batch helper wrote an invalid matrix");
    }

    kb::ecs::World translationAffineWorld(kb::ecs::WorldConfig{ .executionGrainSize = 4 });
    std::vector<kb::ecs::Entity> translationAffineEntities;
    translationAffineEntities.reserve(kCount);
    for (std::size_t index = 0U; index < kCount; ++index) {
        const kb::ecs::Entity entity = translationAffineWorld.CreateEntity();
        translationAffineWorld.Set(entity, locals[index]);
        translationAffineWorld.Set(entity, EcsHotWorldAffineTransform{});
        translationAffineEntities.push_back(entity);
    }
    kb::ecs::Query<EcsHotLocalTransform, EcsHotWorldAffineTransform> translationAffineQuery =
        translationAffineWorld.CreateQuery<EcsHotLocalTransform, EcsHotWorldAffineTransform>();
    double batchTranslationAffineChecksum = 0.0;
    std::size_t batchTranslationAffineVisited = 0U;
    translationAffineQuery.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 2 },
        [&batchTranslationAffineChecksum, &batchTranslationAffineVisited](kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldAffineTransform>& batch) {
            using Batch = kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldAffineTransform>;
            batchTranslationAffineChecksum += kb::ecs::ApplyDenseTransformTranslationToAffine3x4BatchAndReduce<
                Batch,
                EcsHotLocalTransform,
                EcsHotWorldAffineTransform,
                &EcsHotLocalTransform::translationX,
                &EcsHotLocalTransform::translationY,
                &EcsHotLocalTransform::translationZ,
                &EcsHotWorldAffineTransform::affine>(batch);
            batchTranslationAffineVisited += batch.Count();
        });
    kb::tests::Require(batchTranslationAffineVisited == kCount, "ECS translation affine batch helper did not visit every entity");
    kb::tests::Require(
        std::fabs(batchTranslationAffineChecksum - expectedTranslationAffineChecksum) <= 0.000001,
        "ECS translation affine batch helper produced an invalid reduction");
    for (std::size_t index = 0U; index < translationAffineEntities.size(); ++index) {
        const EcsHotWorldAffineTransform* world = translationAffineWorld.TryGet<EcsHotWorldAffineTransform>(translationAffineEntities[index]);
        kb::tests::Require(world != nullptr, "ECS translation affine batch helper lost a world transform");
        RequireHotTranslationAffine(locals[index], *world, "ECS translation affine batch helper wrote an invalid transform");
    }

    kb::ecs::World transformWorld(kb::ecs::WorldConfig{ .executionGrainSize = 4 });
    std::vector<kb::ecs::Entity> transformEntities;
    transformEntities.reserve(kCount);
    for (std::size_t index = 0U; index < kCount; ++index) {
        const kb::ecs::Entity entity = transformWorld.CreateEntity();
        transformWorld.Set(entity, locals[index]);
        transformWorld.Set(entity, EcsHotWorldTransform{});
        transformEntities.push_back(entity);
    }
    kb::ecs::Query<EcsHotLocalTransform, EcsHotWorldTransform> transformQuery =
        transformWorld.CreateQuery<EcsHotLocalTransform, EcsHotWorldTransform>();
    transformQuery.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 2 },
        [](kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldTransform>& batch) {
            using Batch = kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldTransform>;
            kb::ecs::ApplyDenseTransformZToMatrix4x4Batch<
                Batch,
                EcsHotLocalTransform,
                EcsHotWorldTransform,
                &EcsHotLocalTransform::translationX,
                &EcsHotLocalTransform::translationY,
                &EcsHotLocalTransform::translationZ,
                &EcsHotLocalTransform::rotationZ,
                &EcsHotLocalTransform::scaleX,
                &EcsHotLocalTransform::scaleY,
                &EcsHotLocalTransform::scaleZ,
                &EcsHotWorldTransform::matrix>(batch);
        });
    for (std::size_t index = 0U; index < transformEntities.size(); ++index) {
        const EcsHotWorldTransform* world = transformWorld.TryGet<EcsHotWorldTransform>(transformEntities[index]);
        kb::tests::Require(world != nullptr, "ECS transform batch helper lost a world transform");
        RequireHotTransformMatrix(locals[index], *world, "ECS transform batch helper wrote an invalid matrix");
    }

    double batchTransformChecksum = 0.0;
    std::size_t batchTransformVisited = 0U;
    transformQuery.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 2 },
        [&batchTransformChecksum, &batchTransformVisited](kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldTransform>& batch) {
            using Batch = kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldTransform>;
            batchTransformChecksum += kb::ecs::ApplyDenseTransformZToMatrix4x4BatchAndReduce<
                Batch,
                EcsHotLocalTransform,
                EcsHotWorldTransform,
                &EcsHotLocalTransform::translationX,
                &EcsHotLocalTransform::translationY,
                &EcsHotLocalTransform::translationZ,
                &EcsHotLocalTransform::rotationZ,
                &EcsHotLocalTransform::scaleX,
                &EcsHotLocalTransform::scaleY,
                &EcsHotLocalTransform::scaleZ,
                &EcsHotWorldTransform::matrix>(batch);
            batchTransformVisited += batch.Count();
        });
    kb::tests::Require(batchTransformVisited == kCount, "ECS transform batch reduce helper did not visit every entity");
    kb::tests::Require(
        std::fabs(batchTransformChecksum - expectedTransformChecksum) <= 0.000001,
        "ECS transform batch reduce helper produced an invalid reduction");

    std::array<EcsHotWorldAffineTransform, kCount> affineWorlds{};
    for (EcsHotWorldAffineTransform& world : affineWorlds) {
        world.affine[0] = -123.0F;
        world.affine[11] = -123.0F;
    }
    kb::ecs::ApplyDenseTransformZToAffine3x4<
        EcsHotLocalTransform,
        EcsHotWorldAffineTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldAffineTransform::affine>(
        locals.data(),
        affineWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 1U, .count = 3U });
    kb::tests::Require(kb::tests::NearlyEqual(affineWorlds[0].affine[0], -123.0F), "ECS affine transform kernel modified data before the selected range");
    kb::tests::Require(kb::tests::NearlyEqual(affineWorlds[4].affine[11], -123.0F), "ECS affine transform kernel modified data after the selected range");
    RequireHotTransformAffine(locals[1], affineWorlds[1], "ECS affine transform kernel produced an invalid first selected transform");
    RequireHotTransformAffine(locals[2], affineWorlds[2], "ECS affine transform kernel produced an invalid middle selected transform");
    RequireHotTransformAffine(locals[3], affineWorlds[3], "ECS affine transform kernel produced an invalid last selected transform");

    std::array<EcsHotWorldAffineTransform, kCount> reducedAffineWorlds{};
    const double reducedAffineChecksum = kb::ecs::ApplyDenseTransformZToAffine3x4AndReduce<
        EcsHotLocalTransform,
        EcsHotWorldAffineTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldAffineTransform::affine>(
        locals.data(),
        reducedAffineWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 0U, .count = locals.size() });
    double expectedAffineChecksum = 0.0;
    for (std::size_t index = 0U; index < locals.size(); ++index) {
        RequireHotTransformAffine(locals[index], reducedAffineWorlds[index], "ECS affine transform reduce kernel wrote an invalid transform");
        expectedAffineChecksum += static_cast<double>(reducedAffineWorlds[index].affine[0])
            + static_cast<double>(reducedAffineWorlds[index].affine[4])
            + static_cast<double>(reducedAffineWorlds[index].affine[8])
            + static_cast<double>(reducedAffineWorlds[index].affine[9]);
    }
    kb::tests::Require(
        std::fabs(reducedAffineChecksum - expectedAffineChecksum) <= 0.000001,
        "ECS affine transform reduce kernel produced an invalid reduction");

    kb::ecs::World affineWorld(kb::ecs::WorldConfig{ .executionGrainSize = 4 });
    std::vector<kb::ecs::Entity> affineEntities;
    affineEntities.reserve(kCount);
    for (std::size_t index = 0U; index < kCount; ++index) {
        const kb::ecs::Entity entity = affineWorld.CreateEntity();
        affineWorld.Set(entity, locals[index]);
        affineWorld.Set(entity, EcsHotWorldAffineTransform{});
        affineEntities.push_back(entity);
    }
    kb::ecs::Query<EcsHotLocalTransform, EcsHotWorldAffineTransform> affineQuery =
        affineWorld.CreateQuery<EcsHotLocalTransform, EcsHotWorldAffineTransform>();
    double batchAffineChecksum = 0.0;
    std::size_t batchAffineVisited = 0U;
    affineQuery.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 2 },
        [&batchAffineChecksum, &batchAffineVisited](kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldAffineTransform>& batch) {
            using Batch = kb::ecs::MutableQueryBatch<EcsHotLocalTransform, EcsHotWorldAffineTransform>;
            batchAffineChecksum += kb::ecs::ApplyDenseTransformZToAffine3x4BatchAndReduce<
                Batch,
                EcsHotLocalTransform,
                EcsHotWorldAffineTransform,
                &EcsHotLocalTransform::translationX,
                &EcsHotLocalTransform::translationY,
                &EcsHotLocalTransform::translationZ,
                &EcsHotLocalTransform::rotationZ,
                &EcsHotLocalTransform::scaleX,
                &EcsHotLocalTransform::scaleY,
                &EcsHotLocalTransform::scaleZ,
                &EcsHotWorldAffineTransform::affine>(batch);
            batchAffineVisited += batch.Count();
        });
    kb::tests::Require(batchAffineVisited == kCount, "ECS affine transform batch helper did not visit every entity");
    kb::tests::Require(
        std::fabs(batchAffineChecksum - expectedAffineChecksum) <= 0.000001,
        "ECS affine transform batch helper produced an invalid reduction");
    for (std::size_t index = 0U; index < affineEntities.size(); ++index) {
        const EcsHotWorldAffineTransform* world = affineWorld.TryGet<EcsHotWorldAffineTransform>(affineEntities[index]);
        kb::tests::Require(world != nullptr, "ECS affine transform batch helper lost a world transform");
        RequireHotTransformAffine(locals[index], *world, "ECS affine transform batch helper wrote an invalid transform");
    }

    constexpr kb::ecs::HotKernelMemoryTraffic traffic =
        kb::ecs::EstimateDenseTransformZToMatrix4x4Traffic<EcsHotLocalTransform, EcsHotWorldTransform>(kCount);
    static_assert(traffic.bytesRead == kCount * sizeof(EcsHotLocalTransform));
    static_assert(traffic.bytesWritten == kCount * sizeof(EcsHotWorldTransform));
    static_assert(traffic.TotalBytes() == kCount * (sizeof(EcsHotLocalTransform) + sizeof(EcsHotWorldTransform)));

    constexpr kb::ecs::HotKernelMemoryTraffic affineTraffic =
        kb::ecs::EstimateDenseTransformZToAffine3x4Traffic<EcsHotLocalTransform, EcsHotWorldAffineTransform>(kCount);
    static_assert(sizeof(EcsHotWorldAffineTransform) == sizeof(float) * 12U);
    static_assert(affineTraffic.bytesRead == kCount * sizeof(EcsHotLocalTransform));
    static_assert(affineTraffic.bytesWritten == kCount * sizeof(EcsHotWorldAffineTransform));
    static_assert(affineTraffic.bytesWritten == (traffic.bytesWritten * 3U) / 4U);
    static_assert(affineTraffic.TotalBytes() < traffic.TotalBytes());

    constexpr kb::ecs::HotKernelMemoryTraffic translationAffineTraffic =
        kb::ecs::EstimateDenseTransformTranslationToAffine3x4Traffic<EcsHotLocalTransform, EcsHotWorldAffineTransform>(kCount);
    static_assert(translationAffineTraffic.bytesRead == kCount * sizeof(EcsHotLocalTransform));
    static_assert(translationAffineTraffic.bytesWritten == kCount * sizeof(EcsHotWorldAffineTransform));
    static_assert(translationAffineTraffic.TotalBytes() == affineTraffic.TotalBytes());
    static_assert(translationAffineTraffic.TotalBytes() < traffic.TotalBytes());

    constexpr std::size_t kApproxCount = 33U;
    std::array<EcsHotLocalTransform, kApproxCount> approxLocals{};
    std::array<EcsHotWorldTransform, kApproxCount> exactWorlds{};
    std::array<EcsHotWorldTransform, kApproxCount> fastWorlds{};
    for (std::size_t index = 0U; index < kApproxCount; ++index) {
        const float sample = static_cast<float>(index);
        approxLocals[index] = EcsHotLocalTransform{
            .translationX = sample * 0.25F - 3.0F,
            .translationY = 8.0F - sample * 0.125F,
            .translationZ = sample * 0.0625F,
            .rotationZ = sample * 0.391F - 6.25F,
            .scaleX = 1.0F + static_cast<float>(index % 5U) * 0.125F,
            .scaleY = 0.75F + static_cast<float>(index % 7U) * 0.0625F,
            .scaleZ = 1.25F + static_cast<float>(index % 3U) * 0.03125F,
        };
    }
    kb::ecs::ApplyDenseTransformZToMatrix4x4Scalar<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldTransform::matrix>(
        approxLocals.data(),
        exactWorlds.data(),
        approxLocals.size(),
        kb::ecs::TransformTrigPolicy::Exact);
    kb::ecs::ApplyDenseTransformZToMatrix4x4<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldTransform::matrix>(
        approxLocals.data(),
        fastWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 0U, .count = approxLocals.size() },
        kb::ecs::TransformTrigPolicy::FastApprox,
        kb::ecs::KernelBackend::Sse2);
    for (std::size_t index = 0U; index < kApproxCount; ++index) {
        for (std::size_t element = 0U; element < 16U; ++element) {
            kb::tests::Require(
                kb::tests::NearlyEqual(fastWorlds[index].matrix[element], exactWorlds[index].matrix[element]),
                "ECS transform fast trig kernel diverged from exact output");
        }
    }

    std::array<EcsHotWorldAffineTransform, kApproxCount> fastAffineWorlds{};
    kb::ecs::ApplyDenseTransformZToAffine3x4<
        EcsHotLocalTransform,
        EcsHotWorldAffineTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldAffineTransform::affine>(
        approxLocals.data(),
        fastAffineWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 0U, .count = approxLocals.size() },
        kb::ecs::TransformTrigPolicy::FastApprox,
        kb::ecs::KernelBackend::Sse2);
    for (std::size_t index = 0U; index < kApproxCount; ++index) {
        const EcsHotWorldTransform& matrixWorld = fastWorlds[index];
        const EcsHotWorldAffineTransform& compactWorld = fastAffineWorlds[index];
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[0], matrixWorld.matrix[0]), "ECS affine fast trig kernel diverged at basis X0");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[1], matrixWorld.matrix[1]), "ECS affine fast trig kernel diverged at basis X1");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[2], matrixWorld.matrix[2]), "ECS affine fast trig kernel diverged at basis X2");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[3], matrixWorld.matrix[4]), "ECS affine fast trig kernel diverged at basis Y0");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[4], matrixWorld.matrix[5]), "ECS affine fast trig kernel diverged at basis Y1");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[5], matrixWorld.matrix[6]), "ECS affine fast trig kernel diverged at basis Y2");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[6], matrixWorld.matrix[8]), "ECS affine fast trig kernel diverged at basis Z0");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[7], matrixWorld.matrix[9]), "ECS affine fast trig kernel diverged at basis Z1");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[8], matrixWorld.matrix[10]), "ECS affine fast trig kernel diverged at basis Z2");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[9], matrixWorld.matrix[12]), "ECS affine fast trig kernel diverged at translation X");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[10], matrixWorld.matrix[13]), "ECS affine fast trig kernel diverged at translation Y");
        kb::tests::Require(kb::tests::NearlyEqual(compactWorld.affine[11], matrixWorld.matrix[14]), "ECS affine fast trig kernel diverged at translation Z");
    }

    if (kb::ecs::IsKernelBackendCompiled(kb::ecs::KernelBackend::Avx2) && kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
        std::array<EcsHotWorldTransform, kApproxCount> avxPreferredWorlds{};
        kb::ecs::ApplyDenseTransformZToMatrix4x4<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            avxPreferredWorlds.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = approxLocals.size() },
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Avx2);
        for (std::size_t index = 0U; index < kApproxCount; ++index) {
            for (std::size_t element = 0U; element < 16U; ++element) {
                kb::tests::Require(
                    kb::tests::NearlyEqual(avxPreferredWorlds[index].matrix[element], fastWorlds[index].matrix[element]),
                    "ECS transform fast trig AVX-preferred path diverged from the optimized fallback output");
            }
        }
    }

    std::array<EcsHotWorldAffineTransform, kApproxCount> fastReducedAffineWorlds{};
    const double fastReducedAffineChecksum = kb::ecs::ApplyDenseTransformZToAffine3x4AndReduce<
        EcsHotLocalTransform,
        EcsHotWorldAffineTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldAffineTransform::affine>(
        approxLocals.data(),
        fastReducedAffineWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 0U, .count = approxLocals.size() },
        kb::ecs::TransformTrigPolicy::FastApprox,
        kb::ecs::KernelBackend::Sse2);
    double expectedFastAffineChecksum = 0.0;
    for (std::size_t index = 0U; index < kApproxCount; ++index) {
        for (std::size_t element = 0U; element < 12U; ++element) {
            kb::tests::Require(
                kb::tests::NearlyEqual(fastReducedAffineWorlds[index].affine[element], fastAffineWorlds[index].affine[element]),
                "ECS fused affine fast trig kernel diverged from write-only output");
        }
        expectedFastAffineChecksum += static_cast<double>(fastReducedAffineWorlds[index].affine[0])
            + static_cast<double>(fastReducedAffineWorlds[index].affine[4])
            + static_cast<double>(fastReducedAffineWorlds[index].affine[8])
            + static_cast<double>(fastReducedAffineWorlds[index].affine[9]);
    }
    kb::tests::Require(
        std::fabs(fastReducedAffineChecksum - expectedFastAffineChecksum) <= 0.000001,
        "ECS fused affine fast trig kernel produced an invalid reduction");

    std::array<EcsHotWorldTransform, kApproxCount> fastReducedWorlds{};
    const double fastReducedChecksum = kb::ecs::ApplyDenseTransformZToMatrix4x4AndReduce<
        EcsHotLocalTransform,
        EcsHotWorldTransform,
        &EcsHotLocalTransform::translationX,
        &EcsHotLocalTransform::translationY,
        &EcsHotLocalTransform::translationZ,
        &EcsHotLocalTransform::rotationZ,
        &EcsHotLocalTransform::scaleX,
        &EcsHotLocalTransform::scaleY,
        &EcsHotLocalTransform::scaleZ,
        &EcsHotWorldTransform::matrix>(
        approxLocals.data(),
        fastReducedWorlds.data(),
        kb::ecs::HotKernelRange{ .begin = 0U, .count = approxLocals.size() },
        kb::ecs::TransformTrigPolicy::FastApprox,
        kb::ecs::KernelBackend::Sse2);
    double expectedFastChecksum = 0.0;
    for (std::size_t index = 0U; index < kApproxCount; ++index) {
        for (std::size_t element = 0U; element < 16U; ++element) {
            kb::tests::Require(
                kb::tests::NearlyEqual(fastReducedWorlds[index].matrix[element], fastWorlds[index].matrix[element]),
                "ECS fused transform fast trig kernel diverged from write-only output");
        }
        expectedFastChecksum += static_cast<double>(fastReducedWorlds[index].matrix[0])
            + static_cast<double>(fastReducedWorlds[index].matrix[5])
            + static_cast<double>(fastReducedWorlds[index].matrix[10])
            + static_cast<double>(fastReducedWorlds[index].matrix[12]);
    }
    kb::tests::Require(std::fabs(fastReducedChecksum - expectedFastChecksum) <= 0.000001, "ECS fused transform fast trig kernel produced an invalid reduction");

    if (kb::ecs::IsKernelBackendCompiled(kb::ecs::KernelBackend::Avx2) && kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
        std::array<EcsHotWorldTransform, kApproxCount> avxPreferredReducedWorlds{};
        const double avxPreferredReducedChecksum = kb::ecs::ApplyDenseTransformZToMatrix4x4AndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            avxPreferredReducedWorlds.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = approxLocals.size() },
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Avx2);
        kb::tests::Require(
            std::fabs(avxPreferredReducedChecksum - fastReducedChecksum) <= 0.000001,
            "ECS fused transform fast trig AVX-preferred path produced a different reduction");
        for (std::size_t index = 0U; index < kApproxCount; ++index) {
            for (std::size_t element = 0U; element < 16U; ++element) {
                kb::tests::Require(
                    kb::tests::NearlyEqual(avxPreferredReducedWorlds[index].matrix[element], fastReducedWorlds[index].matrix[element]),
                    "ECS fused transform fast trig AVX-preferred path diverged from the optimized fallback output");
            }
        }
    }

    std::array<EcsHotWorldTransform, kApproxCount> versionedWorlds{};
    std::array<EcsHotTransformVersionState, kApproxCount> versionStates{};
    const kb::ecs::DenseTransformVersionedApplyStats firstVersionedStats =
        kb::ecs::ApplyDenseTransformZToMatrix4x4VersionedAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            versionedWorlds.data(),
            versionStates.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = versionStates.size() },
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Sse2);
    kb::tests::Require(firstVersionedStats.visited == kApproxCount, "ECS versioned transform kernel did not visit the full range");
    kb::tests::Require(firstVersionedStats.updated == kApproxCount, "ECS versioned transform kernel did not update every dirty transform");
    kb::tests::Require(firstVersionedStats.skipped == 0U, "ECS versioned transform kernel skipped a dirty transform");
    for (std::size_t index = 0U; index < kApproxCount; ++index) {
        kb::tests::Require(versionStates[index].appliedLocalVersion == versionStates[index].localVersion, "ECS versioned transform kernel did not publish applied local version");
        for (std::size_t element = 0U; element < 16U; ++element) {
            kb::tests::Require(
                kb::tests::NearlyEqual(versionedWorlds[index].matrix[element], fastReducedWorlds[index].matrix[element]),
                "ECS versioned transform dirty pass diverged from fused output");
        }
    }

    for (EcsHotWorldTransform& world : versionedWorlds) {
        world.matrix[0] = -321.0F;
        world.matrix[15] = -654.0F;
    }
    const kb::ecs::DenseTransformVersionedApplyStats cleanVersionedStats =
        kb::ecs::ApplyDenseTransformZToMatrix4x4VersionedAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            versionedWorlds.data(),
            versionStates.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = versionStates.size() },
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Sse2);
    kb::tests::Require(cleanVersionedStats.visited == kApproxCount, "ECS versioned transform clean pass did not visit the full range");
    kb::tests::Require(cleanVersionedStats.updated == 0U, "ECS versioned transform clean pass updated a clean transform");
    kb::tests::Require(cleanVersionedStats.skipped == kApproxCount, "ECS versioned transform clean pass did not skip every clean transform");
    kb::tests::Require(cleanVersionedStats.checksum == 0.0, "ECS versioned transform clean pass produced a dirty checksum");
    for (const EcsHotWorldTransform& world : versionedWorlds) {
        kb::tests::Require(kb::tests::NearlyEqual(world.matrix[0], -321.0F), "ECS versioned transform clean pass wrote matrix data");
        kb::tests::Require(kb::tests::NearlyEqual(world.matrix[15], -654.0F), "ECS versioned transform clean pass wrote matrix tail data");
    }

    ++versionStates[3].localVersion;
    const kb::ecs::DenseTransformVersionedApplyStats partialVersionedStats =
        kb::ecs::ApplyDenseTransformZToMatrix4x4VersionedAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            versionedWorlds.data(),
            versionStates.data(),
            kb::ecs::HotKernelRange{ .begin = 2U, .count = 3U },
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Sse2);
    kb::tests::Require(partialVersionedStats.visited == 3U, "ECS versioned transform partial pass visited an invalid range");
    kb::tests::Require(partialVersionedStats.updated == 1U, "ECS versioned transform partial pass did not update exactly one dirty transform");
    kb::tests::Require(partialVersionedStats.skipped == 2U, "ECS versioned transform partial pass skipped an invalid count");
    kb::tests::Require(versionStates[3].appliedLocalVersion == versionStates[3].localVersion, "ECS versioned transform partial pass did not publish applied local version");
    kb::tests::Require(!kb::tests::NearlyEqual(versionedWorlds[3].matrix[0], -321.0F), "ECS versioned transform partial pass did not write the dirty matrix");
    kb::tests::Require(kb::tests::NearlyEqual(versionedWorlds[2].matrix[0], -321.0F), "ECS versioned transform partial pass wrote a clean matrix before the dirty entity");
    kb::tests::Require(kb::tests::NearlyEqual(versionedWorlds[4].matrix[0], -321.0F), "ECS versioned transform partial pass wrote a clean matrix after the dirty entity");

    constexpr std::size_t kSummaryRangeSize = 8U;
    const std::size_t expectedSummaryCount = kb::ecs::DenseTransformVersionRangeSummaryCount(versionStates.size(), kSummaryRangeSize);
    std::vector<kb::ecs::DenseTransformVersionRangeSummary> summaries(expectedSummaryCount);
    const std::size_t cleanSummaryCount =
        kb::ecs::BuildDenseTransformVersionRangeSummaries<
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion>(
            versionStates.data(),
            versionStates.size(),
            kSummaryRangeSize,
            summaries);
    kb::tests::Require(cleanSummaryCount == expectedSummaryCount, "ECS transform summary builder returned an invalid summary count");
    for (const kb::ecs::DenseTransformVersionRangeSummary& summary : summaries) {
        kb::tests::Require(summary.dirtyCount == 0U, "ECS transform summary builder marked a clean range dirty");
    }

    for (EcsHotWorldTransform& world : versionedWorlds) {
        world.matrix[0] = -777.0F;
    }
    const kb::ecs::DenseTransformVersionedApplyStats cleanSummaryStats =
        kb::ecs::ApplyDenseTransformZToMatrix4x4VersionedSummariesAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            versionedWorlds.data(),
            versionStates.data(),
            summaries,
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Sse2);
    kb::tests::Require(cleanSummaryStats.updated == 0U, "ECS transform summary clean pass updated a clean transform");
    kb::tests::Require(cleanSummaryStats.skipped == versionStates.size(), "ECS transform summary clean pass did not skip every clean transform");
    for (const EcsHotWorldTransform& world : versionedWorlds) {
        kb::tests::Require(kb::tests::NearlyEqual(world.matrix[0], -777.0F), "ECS transform summary clean pass wrote matrix data");
    }

    ++versionStates[17].localVersion;
    const std::size_t dirtySummaryCount =
        kb::ecs::BuildDenseTransformVersionRangeSummaries<
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion>(
            versionStates.data(),
            versionStates.size(),
            kSummaryRangeSize,
            summaries);
    kb::tests::Require(dirtySummaryCount == expectedSummaryCount, "ECS transform summary dirty rebuild returned an invalid summary count");
    std::size_t dirtySummaryRanges = 0U;
    for (const kb::ecs::DenseTransformVersionRangeSummary& summary : summaries) {
        dirtySummaryRanges += summary.dirtyCount == 0U ? 0U : 1U;
    }
    kb::tests::Require(dirtySummaryRanges == 1U, "ECS transform summary builder did not isolate one dirty range");
    const kb::ecs::DenseTransformVersionedApplyStats dirtySummaryStats =
        kb::ecs::ApplyDenseTransformZToMatrix4x4VersionedSummariesAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotTransformVersionState,
            &EcsHotTransformVersionState::localVersion,
            &EcsHotTransformVersionState::appliedLocalVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            approxLocals.data(),
            versionedWorlds.data(),
            versionStates.data(),
            summaries,
            kb::ecs::TransformTrigPolicy::FastApprox,
            kb::ecs::KernelBackend::Sse2);
    kb::tests::Require(dirtySummaryStats.updated == 1U, "ECS transform summary dirty pass did not update exactly one transform");
    kb::tests::Require(dirtySummaryStats.skipped == versionStates.size() - 1U, "ECS transform summary dirty pass skipped an invalid entity count");
    kb::tests::Require(versionStates[17].appliedLocalVersion == versionStates[17].localVersion, "ECS transform summary dirty pass did not publish applied version");
    for (const kb::ecs::DenseTransformVersionRangeSummary& summary : summaries) {
        kb::tests::Require(summary.dirtyCount == 0U, "ECS transform summary dirty pass did not clear the dirty range");
    }
}

void RunEcsHotKernelTransformHierarchyTest() {
    constexpr std::uint32_t kInvalidParent = 0xFFFFFFFFU;
    std::array<EcsHotLocalTransform, 4U> locals{
        EcsHotLocalTransform{ .translationX = 10.0F, .translationY = 1.0F, .translationZ = 0.0F },
        EcsHotLocalTransform{ .translationX = 2.0F, .translationY = 3.0F, .translationZ = 0.0F },
        EcsHotLocalTransform{ .translationX = 4.0F, .translationY = 5.0F, .translationZ = 0.0F },
        EcsHotLocalTransform{ .translationX = 100.0F, .translationY = 200.0F, .translationZ = 0.0F },
    };
    std::array<EcsHotWorldTransform, locals.size()> worlds{};
    std::array<EcsHotHierarchyNode, locals.size()> nodes{
        EcsHotHierarchyNode{ .parentIndex = kInvalidParent, .localVersion = 1U },
        EcsHotHierarchyNode{ .parentIndex = 0U, .localVersion = 1U },
        EcsHotHierarchyNode{ .parentIndex = 1U, .localVersion = 1U },
        EcsHotHierarchyNode{ .parentIndex = kInvalidParent, .localVersion = 1U },
    };

    const kb::ecs::DenseTransformVersionedApplyStats firstStats =
        kb::ecs::ApplyDenseTransformHierarchyZToMatrix4x4RangeAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotHierarchyNode,
            &EcsHotHierarchyNode::parentIndex,
            &EcsHotHierarchyNode::localVersion,
            &EcsHotHierarchyNode::appliedLocalVersion,
            &EcsHotHierarchyNode::observedParentWorldVersion,
            &EcsHotHierarchyNode::worldVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            locals.data(),
            worlds.data(),
            nodes.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = 3U },
            kInvalidParent,
            kb::ecs::TransformTrigPolicy::Exact);

    kb::tests::Require(firstStats.visited == 3U, "ECS hierarchy transform kernel visited an invalid count");
    kb::tests::Require(firstStats.updated == 3U, "ECS hierarchy transform kernel did not update dirty hierarchy rows");
    kb::tests::Require(firstStats.skipped == 0U, "ECS hierarchy transform kernel skipped dirty hierarchy rows");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[0].matrix[12], 10.0F), "ECS hierarchy transform kernel wrote invalid root X");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[1].matrix[12], 12.0F), "ECS hierarchy transform kernel did not compose child X");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[2].matrix[12], 16.0F), "ECS hierarchy transform kernel did not compose grandchild X");
    kb::tests::Require(nodes[2].observedParentWorldVersion == nodes[1].worldVersion, "ECS hierarchy transform kernel did not publish parent version");
    kb::tests::Require(nodes[3].appliedLocalVersion == 0U, "ECS hierarchy transform kernel modified data after selected range");

    worlds[1].matrix[12] = -5.0F;
    const kb::ecs::DenseTransformVersionedApplyStats cleanStats =
        kb::ecs::ApplyDenseTransformHierarchyZToMatrix4x4RangeAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotHierarchyNode,
            &EcsHotHierarchyNode::parentIndex,
            &EcsHotHierarchyNode::localVersion,
            &EcsHotHierarchyNode::appliedLocalVersion,
            &EcsHotHierarchyNode::observedParentWorldVersion,
            &EcsHotHierarchyNode::worldVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            locals.data(),
            worlds.data(),
            nodes.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = 3U },
            kInvalidParent,
            kb::ecs::TransformTrigPolicy::Exact);
    kb::tests::Require(cleanStats.updated == 0U, "ECS hierarchy transform clean pass updated rows");
    kb::tests::Require(cleanStats.skipped == 3U, "ECS hierarchy transform clean pass skipped an invalid count");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[1].matrix[12], -5.0F), "ECS hierarchy transform clean pass wrote matrix data");

    ++nodes[1].localVersion;
    const kb::ecs::DenseTransformVersionedApplyStats dirtyParentStats =
        kb::ecs::ApplyDenseTransformHierarchyZToMatrix4x4RangeAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotHierarchyNode,
            &EcsHotHierarchyNode::parentIndex,
            &EcsHotHierarchyNode::localVersion,
            &EcsHotHierarchyNode::appliedLocalVersion,
            &EcsHotHierarchyNode::observedParentWorldVersion,
            &EcsHotHierarchyNode::worldVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            locals.data(),
            worlds.data(),
            nodes.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = 3U },
            kInvalidParent,
            kb::ecs::TransformTrigPolicy::Exact);
    kb::tests::Require(dirtyParentStats.updated == 2U, "ECS hierarchy transform kernel did not propagate dirty parent updates");
    kb::tests::Require(dirtyParentStats.skipped == 1U, "ECS hierarchy transform kernel did not skip clean root");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[2].matrix[12], 16.0F), "ECS hierarchy transform dirty parent pass lost child composition");

    std::array<const EcsHotLocalTransform*, locals.size()> localPointers{};
    std::array<EcsHotWorldTransform*, worlds.size()> worldPointers{};
    std::array<EcsHotHierarchyNode*, nodes.size()> nodePointers{};
    for (std::size_t index = 0U; index < locals.size(); ++index) {
        localPointers[index] = &locals[index];
        worldPointers[index] = &worlds[index];
        nodePointers[index] = &nodes[index];
    }
    ++nodes[0].localVersion;
    const kb::ecs::DenseTransformVersionedApplyStats pointerStats =
        kb::ecs::ApplyDenseTransformHierarchyZToMatrix4x4PointerRangeAndReduce<
            EcsHotLocalTransform,
            EcsHotWorldTransform,
            EcsHotHierarchyNode,
            &EcsHotHierarchyNode::parentIndex,
            &EcsHotHierarchyNode::localVersion,
            &EcsHotHierarchyNode::appliedLocalVersion,
            &EcsHotHierarchyNode::observedParentWorldVersion,
            &EcsHotHierarchyNode::worldVersion,
            &EcsHotLocalTransform::translationX,
            &EcsHotLocalTransform::translationY,
            &EcsHotLocalTransform::translationZ,
            &EcsHotLocalTransform::rotationZ,
            &EcsHotLocalTransform::scaleX,
            &EcsHotLocalTransform::scaleY,
            &EcsHotLocalTransform::scaleZ,
            &EcsHotWorldTransform::matrix>(
            localPointers.data(),
            worldPointers.data(),
            nodePointers.data(),
            kb::ecs::HotKernelRange{ .begin = 0U, .count = 3U },
            kInvalidParent,
            kb::ecs::TransformTrigPolicy::Exact);
    kb::tests::Require(pointerStats.updated == 3U, "ECS pointer hierarchy transform kernel did not propagate dirty root updates");
    kb::tests::Require(kb::tests::NearlyEqual(worlds[2].matrix[12], 16.0F), "ECS pointer hierarchy transform kernel lost child composition");
}

void RunEcsHotKernelCompiledBatchPathTest() {
    constexpr int kCount = 13;
    kb::ecs::World world(kb::ecs::WorldConfig{ .executionGrainSize = 4 });
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(kCount);

    for (int index = 0; index < kCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = -4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index * 2) });
        entities.push_back(entity);
    }

    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Auto,
        EcsHotMovementKernel{ .scale = 0.25F },
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});
    kb::tests::Require(compiled.IsValid(), "ECS hot compiled kernel did not create a valid query");
    compiled.Execute();

    for (int index = 0; index < kCount; ++index) {
        const EcsPosition* position = world.TryGet<EcsPosition>(entities[static_cast<std::size_t>(index)]);
        kb::tests::Require(position != nullptr, "ECS hot compiled kernel lost an output component");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, static_cast<float>(index) + 0.5F), "ECS hot compiled kernel wrote unexpected X");
        kb::tests::Require(kb::tests::NearlyEqual(position->y, static_cast<float>(index * 2) - 1.0F), "ECS hot compiled kernel wrote unexpected Y");
    }
}

void RunEcsHotPathRuntimeTelemetryGuardTest() {
    constexpr std::size_t kCount = 64U;
    std::vector<EcsPosition> positions(kCount);
    std::vector<EcsVelocity> velocities(kCount);
    for (std::size_t index = 0; index < kCount; ++index) {
        positions[index] = EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index * 2U) };
        velocities[index] = EcsVelocity{ .x = 1.0F, .y = -2.0F };
    }

    kb::ecs::World world(kb::ecs::WorldConfigPresets::BenchmarkNativeOnly());
    const kb::ecs::World::BulkComponentView positionView = kb::ecs::World::MakeBulkComponentView<EcsPosition>(positions);
    const kb::ecs::World::BulkComponentView velocityView = kb::ecs::World::MakeBulkComponentView<EcsVelocity>(velocities);
    const std::array views{ positionView, velocityView };
    const std::vector<kb::ecs::Entity> entities = world.CreateEntitiesNativeOnly(kCount, views);

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::UnsafeHotQuery<EcsPosition, EcsVelocity> hotQuery;
    kb::tests::Require(
        hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = 16 }),
        "ECS hot path telemetry guard could not build unsafe query");

    const kb::ecs::WorldTelemetrySnapshot before = world.TelemetrySnapshot();
    const kb::ecs::UnsafeHotRangeDispatchStats stats = hotQuery.ForEachMutableRange(16U, [](kb::ecs::UnsafeHotMutableChunk<EcsPosition, EcsVelocity>& batch) {
        EcsPosition* batchPositions = batch.Components<0>();
        const EcsVelocity* batchVelocities = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            batchPositions[index].x += batchVelocities[index].x;
            batchPositions[index].y += batchVelocities[index].y;
        }
    });
    const kb::ecs::WorldTelemetrySnapshot after = world.TelemetrySnapshot();

    kb::tests::Require(stats.entities == kCount, "ECS hot path telemetry guard did not visit every entity");
    kb::tests::Require(stats.ranges == 4U, "ECS hot path telemetry guard did not use the configured range size");
    kb::tests::Require(after.queryExecutions == before.queryExecutions, "ECS hot path guard used the safe query executor during iteration");
    kb::tests::Require(after.compatMutableIterations == before.compatMutableIterations, "ECS hot path guard used compatibility mutable iteration");
    kb::tests::Require(after.compatMutableEntitiesVisited == before.compatMutableEntitiesVisited, "ECS hot path guard visited entities through compatibility iteration");

    const kb::ecs::ComponentId positionId = query.ComponentIds()[0];
    const EcsPosition* firstPosition = static_cast<const EcsPosition*>(world.NativeStorage().ComponentData(entities.front(), positionId));
    kb::tests::Require(firstPosition != nullptr, "ECS hot path telemetry guard lost native component storage");
    kb::tests::Require(kb::tests::NearlyEqual(firstPosition->x, 1.0F), "ECS hot path telemetry guard did not mutate native position X");
    kb::tests::Require(kb::tests::NearlyEqual(firstPosition->y, -2.0F), "ECS hot path telemetry guard did not mutate native position Y");
}

void RunEcsHotPathSourceGuardTest() {
    const std::filesystem::path root = ResolveRepositoryRootForHotPathGuard();
    kb::tests::Require(!root.empty(), "ECS hot path source guard could not resolve repository root");

    constexpr std::array forbiddenKernelTokens{
        std::string_view{ "TryGet<" },
        std::string_view{ "TryGetMutable<" },
        std::string_view{ "TryGetComponent(" },
        std::string_view{ "TryGetMutableComponent(" },
        std::string_view{ "ComponentStorageQuery::" },
        std::string_view{ "WorldComponentReader::" },
        std::string_view{ "World::ForEachMutable" },
        std::string_view{ ".ForEachMutable(" },
        std::string_view{ "std::function" },
        std::string_view{ "std::unordered_map" },
        std::string_view{ "std::lock_guard" },
    };

    constexpr std::array guardedFiles{
        std::string_view{ "sources/engine/include/engine/ecs/HotKernel.hpp" },
        std::string_view{ "sources/engine/src/private/scene/transform/SceneTransformRootHotKernel.hpp" },
        std::string_view{ "sources/engine/src/private/scene/transform/SceneTransformDirtyFrontier.hpp" },
    };

    for (std::string_view relativePath : guardedFiles) {
        RequireHotPathSourceDoesNotContain(root, relativePath, forbiddenKernelTokens);
    }
}

void RunEcsKernelBackendReportNamesTest() {
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Scalar) == "scalar", "ECS kernel backend report name changed for scalar");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Neon) == "neon", "ECS kernel backend report name changed for NEON");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Sse2) == "sse2", "ECS kernel backend report name changed for SSE2");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Avx2) == "avx2", "ECS kernel backend report name changed for AVX2");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Avx512) == "avx512", "ECS kernel backend report name changed for AVX-512");
    kb::tests::Require(kb::ecs::KernelBackendPreferenceName(kb::ecs::KernelBackendPreference::Auto) == "auto", "ECS kernel backend preference report name changed for auto");
    kb::tests::Require(kb::ecs::KernelBackendPreferenceName(kb::ecs::KernelBackendPreference::Neon) == "neon", "ECS kernel backend preference report name changed for NEON");
}

void RunEcsPreferredKernelBackendReportTest() {
    const kb::ecs::KernelBackend preferred = kb::ecs::PreferredKernelBackend();
    const kb::ecs::KernelBackendReport preferredReport = kb::ecs::PreferredKernelBackendReport();
    kb::tests::Require(preferredReport.backend == preferred, "ECS preferred kernel backend report returned a different backend");
    kb::tests::Require(preferredReport.name == kb::ecs::KernelBackendName(preferred), "ECS preferred kernel backend report returned an invalid name");
    kb::tests::Require(preferredReport.floatLanes == kb::ecs::KernelBackendFloatLaneCount(preferred), "ECS preferred kernel backend report returned an invalid lane count");
    kb::tests::Require(preferredReport.nativelyCompiled == kb::ecs::IsKernelBackendCompiled(preferred), "ECS preferred kernel backend report returned invalid compile status");
    kb::tests::Require(preferredReport.hardwareSupported == kb::ecs::IsKernelBackendSupported(preferred), "ECS preferred kernel backend report returned invalid hardware status");
    kb::tests::Require(preferredReport.autoSelectable == kb::ecs::IsKernelBackendAutoSelectable(preferred), "ECS preferred kernel backend report returned invalid auto-select status");
    kb::tests::Require(preferredReport.preferred, "ECS preferred kernel backend report did not mark the preferred backend");

    const kb::ecs::KernelBackendReport scalarReport = kb::ecs::MakeKernelBackendReport(kb::ecs::KernelBackend::Scalar);
    kb::tests::Require(scalarReport.backend == kb::ecs::KernelBackend::Scalar, "ECS scalar kernel backend report returned a different backend");
    kb::tests::Require(scalarReport.name == "scalar", "ECS scalar kernel backend report returned an invalid name");
    kb::tests::Require(scalarReport.floatLanes == 1U, "ECS scalar kernel backend report returned an invalid lane count");
    kb::tests::Require(scalarReport.nativelyCompiled, "ECS scalar kernel backend report must always be compiled");
    kb::tests::Require(scalarReport.hardwareSupported, "ECS scalar kernel backend report must always be supported");
    kb::tests::Require(scalarReport.autoSelectable, "ECS scalar kernel backend report must always be auto-selectable");

    kb::tests::Require(kb::ecs::IsKernelBackendSupported(preferred), "ECS preferred kernel backend selected an unsupported hardware path");
    kb::tests::Require(kb::ecs::IsKernelBackendCompiled(preferred), "ECS preferred kernel backend selected a path without native code in this build");
    kb::tests::Require(kb::ecs::IsKernelBackendAutoSelectable(preferred), "ECS preferred kernel backend selected a path unavailable to auto dispatch");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Scalar) == 1U, "ECS scalar lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Neon) == 4U, "ECS NEON lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Sse2) == 4U, "ECS SSE2 lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx2) == 8U, "ECS AVX2 lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx512) == 16U, "ECS AVX-512 lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(preferred) >= 1U, "ECS preferred backend reported an invalid lane count");

    kb::ecs::KernelBackend expected = kb::ecs::KernelBackend::Scalar;
    if (kb::ecs::IsKernelBackendAutoSelectable(kb::ecs::KernelBackend::Neon)) {
        expected = kb::ecs::KernelBackend::Neon;
    } else if (kb::ecs::IsKernelBackendAutoSelectable(kb::ecs::KernelBackend::Avx512)) {
        expected = kb::ecs::KernelBackend::Avx512;
    } else if (kb::ecs::IsKernelBackendAutoSelectable(kb::ecs::KernelBackend::Avx2)) {
        expected = kb::ecs::KernelBackend::Avx2;
    } else if (kb::ecs::IsKernelBackendAutoSelectable(kb::ecs::KernelBackend::Sse2)) {
        expected = kb::ecs::KernelBackend::Sse2;
    }
    kb::tests::Require(preferred == expected, "ECS preferred kernel backend priority changed unexpectedly");
}

void RunEcsReleaseNativeVectorBuildContractTest() {
#if defined(NDEBUG) && (defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__))
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
        kb::tests::Require(
            kb::ecs::IsKernelBackendCompiled(kb::ecs::KernelBackend::Avx2),
            "ECS Release desktop build is running on AVX2 hardware without AVX2 native code");
        kb::tests::Require(
            kb::ecs::IsKernelBackendAutoSelectable(kb::ecs::KernelBackend::Avx2),
            "ECS Release desktop build cannot auto-select AVX2 despite AVX2 hardware support");
        kb::tests::Require(
            kb::ecs::KernelBackendFloatLaneCount(kb::ecs::PreferredKernelBackend()) >= kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx2),
            "ECS Release desktop build selected a backend narrower than AVX2 on AVX2 hardware");
    }
#endif
}

} // namespace

namespace kb::tests {

void RunEcsKernelTests() {
    RunEcsKernelContractScalarExecutionTest();
    RunEcsKernelRequestedSimdFallsBackToScalarTest();
    RunEcsKernelSse2AndAvx2DispatchTest();
    RunEcsKernelNeonDispatchTest();
    RunEcsKernelAvx512DispatchTest();
    RunEcsCompiledKernelBackendPreferenceUpdateTest();
    RunEcsCompiledKernelQueryExecutionTest();
    RunEcsKernelAlignedComponentContractTest();
    RunEcsCompiledKernelMatchesQueryPathTest();
    RunEcsCompiledKernelSystemRegistrationTest();
    RunEcsEditorHotReloadKernelPathTest();
    RunEcsEditorKernelClearedDuringExecutionTest();
    RunEcsKernelVectorMathBitDeterminismTest();
    RunEcsKernelScalarSimdCompatibilityTest();
    RunEcsKernelVectorMathUnalignedFallbackTest();
    RunEcsHotKernelDenseFloat2UpdateTest();
    RunEcsHotKernelDenseColumnFloat2UpdateTest();
    RunEcsHotKernelRangeAndTrafficTest();
    RunEcsHotKernelTransformZToMatrix4x4Test();
    RunEcsHotKernelTransformHierarchyTest();
    RunEcsHotKernelCompiledBatchPathTest();
    RunEcsHotPathRuntimeTelemetryGuardTest();
    RunEcsHotPathSourceGuardTest();
    RunEcsKernelBackendReportNamesTest();
    RunEcsPreferredKernelBackendReportTest();
    RunEcsReleaseNativeVectorBuildContractTest();
}

} // namespace kb::tests
