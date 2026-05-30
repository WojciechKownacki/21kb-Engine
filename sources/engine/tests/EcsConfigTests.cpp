#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldConfigPresets.hpp"

namespace {

void RunWorldConfigTest() {
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk64KB) == 64 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk128KB) == 128 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk256KB) == 256 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk512KB) == 512 * 1024);

    kb::ecs::World defaultWorld;
    kb::tests::Require(defaultWorld.Config().chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk32KB, "ECS default config uses invalid storage chunk profile");
    kb::tests::Require(kb::ecs::ChunkPayloadBytes(defaultWorld.Config().chunkSizeProfile) == 32 * 1024, "ECS default config exposes invalid storage chunk payload");

    kb::ecs::World world(kb::ecs::WorldConfigPresets::BenchmarkDefault());
    kb::tests::Require(world.Config().chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk32KB, "ECS benchmark config uses invalid storage chunk profile");
    kb::tests::Require(kb::ecs::ChunkPayloadBytes(world.Config().chunkSizeProfile) == 32 * 1024, "ECS benchmark config exposes invalid storage chunk payload");
    kb::tests::Require(world.Config().executionGrainSize == 256, "ECS benchmark config exposes invalid execution grain size");
}

} // namespace

namespace kb::tests {

void RunEcsConfigTests() {
    RunWorldConfigTest();
}

} // namespace kb::tests
