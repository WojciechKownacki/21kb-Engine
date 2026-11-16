#include "RendererTestSupport.hpp"
#include "engine/particles/ParticleGpuEmitter.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {
#if defined(_WIN32)
constexpr float kStep = 1.0F / 60.0F;
constexpr std::uint32_t kParticles = 1'000U;
constexpr float kGravity = -9.8F;
constexpr float kRestitution = 0.9F;

class EventSurface final : public RenderSurface {
public:
    EventSurface() : window_(CreateWindowExW(0U, L"STATIC", L"Particle collision events", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {}
    ~EventSurface() override { if (window_ != nullptr) DestroyWindow(window_); }
    EventSurface(const EventSurface&) = delete;
    EventSurface& operator=(const EventSurface&) = delete;
    std::uint32_t Width() const noexcept override { return 64U; }
    std::uint32_t Height() const noexcept override { return 64U; }
    void* NativeWindowHandle() const noexcept override { return window_; }
    void* NativeDisplayHandle() const noexcept override { return nullptr; }
private:
    HWND window_ = nullptr;
};

// The particles fall from heights between 0.3 and 1.5 m onto the plane y = 0, side by side along x.
[[nodiscard]] float StartHeight(std::uint32_t index) { return 0.3F + 1.2F * static_cast<float>(index) / static_cast<float>(kParticles); }
[[nodiscard]] float StartX(std::uint32_t index) { return -5.0F + 0.01F * static_cast<float>(index); }

[[nodiscard]] kb::particles::ParticleGpuEmitterCommand MakeFall(float initialSpeed, float height) {
    kb::particles::ParticleGpuEmitterCommand command;
    command.key = { 1U, 1U };
    command.hasParams = true;
    command.params.capacity = 1'024U;
    command.params.acceleration = { 0.0F, kGravity, 0.0F };
    command.params.plane = { .normal = { 0.0F, 1.0F, 0.0F }, .distance = 0.0F, .restitution = kRestitution };
    command.params.hasPlane = true;
    command.params.collisionEvents = true;
    command.params.color.fill({ 1.0F, 1.0F, 1.0F, 1.0F });
    command.params.size.fill(0.1F);
    for (std::uint32_t index = 0U; index < kParticles; ++index) {
        const float y = height < 0.0F ? StartHeight(index) : height;
        command.spawns.push_back({ .position = { StartX(index), y, 0.0F }, .birthTime = 0.0F,
            .velocity = { 0.0F, initialSpeed, 0.0F }, .lifetime = 30.0F });
    }
    return command;
}

// One rendered frame of the simulation: the fixed step ends at `step` / 60 s, as does the scene clock.
void Frame(ParticleGpuEmitterSimulation& simulation, kb::particles::ParticleGpuEmitterCommand& command, std::uint32_t step) {
    command.simTime = static_cast<double>(step) * static_cast<double>(kStep);
    simulation.Apply(1U, { &command, 1U });
    command.spawns.clear();
    simulation.Advance(1U, kStep);
    simulation.Dispatch(0U, 1U, {});
}

// The fixed-step reference of the kernel: constant acceleration, the plane, the bounce of the CPU collision module.
[[nodiscard]] std::uint32_t ReferenceCollisions(std::uint32_t steps) {
    std::uint32_t collisions = 0U;
    for (std::uint32_t index = 0U; index < kParticles; ++index) {
        float y = StartHeight(index);
        float velocity = 0.0F;
        for (std::uint32_t step = 0U; step < steps; ++step) {
            velocity += kGravity * kStep;
            y += velocity * kStep;
            if (y >= 0.0F) continue;
            y = 0.0F;
            velocity = -velocity * kRestitution;
            ++collisions;
        }
    }
    return collisions;
}

void VerifyEventsMatchCpuReference() {
    constexpr std::uint32_t kSteps = 120U;
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "The GPU emitter kernels are unavailable");
    auto command = MakeFall(0.0F, -1.0F);
    kb::particles::ParticleGpuCollisionBatch batch;
    std::uint32_t frame = 0U;
    for (std::uint32_t step = 1U; step <= kSteps; ++step) {
        Frame(simulation, command, step);
        frame = bgfx::frame();
        simulation.CollectCollisionEvents(1U, frame, batch);
    }
    // The last copies finish two frames after the last dispatch; nothing is dispatched meanwhile.
    for (std::uint32_t wait = 0U; wait < 3U; ++wait) {
        frame = bgfx::frame();
        simulation.CollectCollisionEvents(1U, frame, batch);
    }
    const std::uint32_t expected = ReferenceCollisions(kSteps);
    const std::size_t delivered = batch.events.size();
    float highest = 0.0F;
    float normalError = 0.0F;
    bool incoming = true;
    bool known = true;
    for (const auto& event : batch.events) {
        highest = std::max(highest, std::abs(event.position.y));
        normalError = std::max({ normalError, std::abs(event.normal.x), std::abs(event.normal.y - 1.0F), std::abs(event.normal.z) });
        incoming = incoming && event.velocity.y < 0.0F;
        known = known && event.key.instanceId == 1U && event.key.emitterId == 1U &&
            event.position.x >= StartX(0U) - 0.001F && event.position.x <= StartX(kParticles - 1U) + 0.001F;
    }
    const auto stats = simulation.CollisionStats(1U);
    std::fprintf(stderr, "particle_gpu_collision_events steps=%u cpu_reference=%u gpu=%zu lost=%u max_height_error=%.6f "
        "reads=%u pending=%u\n", kSteps, expected, delivered, batch.lost, static_cast<double>(highest), stats.submittedReads, stats.pendingReads);
    Require(expected > 3U * kParticles, "The reference scene produced too few collisions to compare");
    const std::uint32_t difference = expected > delivered ? expected - static_cast<std::uint32_t>(delivered)
                                                          : static_cast<std::uint32_t>(delivered) - expected;
    Require(difference * 50U <= expected, "The GPU collision count differs from the CPU reference by more than 2 percent");
    Require(batch.lost == 0U && stats.lost == 0U && stats.skippedReads == 0U, "Collisions were lost although the buffer was large enough");
    Require(highest < 0.05F, "A collision event is not on the plane");
    Require(normalError < 0.001F && incoming && known, "A collision event lost its normal, its incoming velocity or its emitter");
    simulation.Shutdown();
}

void VerifyOverflowIsCounted() {
    constexpr std::uint32_t kCapacity = 64U;
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(0U, kCapacity), "The GPU emitter kernels are unavailable");
    // Every particle is 1 cm above the plane and moving down: all of them hit it in the first dispatch.
    auto command = MakeFall(-5.0F, 0.01F);
    Frame(simulation, command, 1U);
    const std::uint32_t submitted = bgfx::frame();
    const auto pending = simulation.CollisionStats(1U);
    kb::particles::ParticleGpuCollisionBatch batch;
    simulation.CollectCollisionEvents(1U, submitted, batch);
    // The mechanism: the copy is asynchronous. It is not complete one frame after the dispatch, collecting does not
    // wait for it (it returns at once and leaves the copy pending), and only the caller's own frame() calls advance bgfx.
    const auto early = simulation.CollisionStats(1U);
    Require(pending.submittedReads == 1U && pending.pendingReads == 1U && batch.events.empty() && early.pendingReads == 1U,
        "The collision readback was not asynchronous: it completed within the frame of the dispatch");
    const std::uint32_t next = bgfx::frame();
    Require(next == submitted + 1U, "Collecting collision events advanced bgfx frames");
    simulation.CollectCollisionEvents(1U, next, batch);
    const std::uint32_t last = bgfx::frame();
    Require(last == next + 1U, "Collecting collision events advanced bgfx frames");
    simulation.CollectCollisionEvents(1U, last, batch);
    const auto done = simulation.CollisionStats(1U);
    std::fprintf(stderr, "particle_gpu_collision_overflow capacity=%u recorded=%llu delivered=%zu lost=%u frames_to_read=%u\n",
        kCapacity, static_cast<unsigned long long>(done.recorded), batch.events.size(), batch.lost, last - submitted + 1U);
    Require(batch.events.size() == kCapacity && batch.lost == kParticles - kCapacity,
        "An overflowing event buffer did not deliver exactly its capacity and count the rest as lost");
    Require(done.recorded == kParticles && done.delivered == kCapacity && done.lost == kParticles - kCapacity &&
            done.pendingReads == 0U, "The event diagnostics do not account for every collision");
    simulation.Shutdown();
}

void VerifyNoEventsWithoutConsumer() {
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "The GPU emitter kernels are unavailable");
    auto command = MakeFall(-5.0F, 0.01F);
    command.params.collisionEvents = false;
    Frame(simulation, command, 1U);
    static_cast<void>(bgfx::frame());
    kb::particles::ParticleGpuCollisionBatch batch;
    simulation.CollectCollisionEvents(1U, bgfx::frame(), batch);
    simulation.CollectCollisionEvents(1U, bgfx::frame(), batch);
    const auto stats = simulation.CollisionStats(1U);
    Require(batch.events.empty() && batch.lost == 0U && stats.submittedReads == 0U,
        "An emitter that reports no collisions caused a readback");
    simulation.Shutdown();
}
#endif
} // namespace

void RunParticleGpuCollisionEventTests(bgfx::RendererType::Enum backend) {
#if defined(_WIN32)
    EventSurface surface;
    DisplayConfig config;
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(backend);
    Renderer renderer;
    Require(surface.NativeWindowHandle() != nullptr && renderer.Initialize(surface, &config), "Requested real bgfx device unavailable");
    Require(bgfx::getRendererType() == backend, "The collision event test used an unexpected backend");
    VerifyEventsMatchCpuReference();
    VerifyOverflowIsCounted();
    VerifyNoEventsWithoutConsumer();
    renderer.Shutdown();
#else
    static_cast<void>(backend);
    Require(false, "The GPU collision event verification requires the Windows GPU host");
#endif
}

} // namespace kb::render::tests
