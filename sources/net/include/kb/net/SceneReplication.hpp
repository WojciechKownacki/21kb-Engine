#pragma once

#include "kb/net/Replication.hpp"
#include "kb/net/Rpc.hpp"
#include "engine/network/ReplicationDeclarations.hpp"

#include <memory>

// Ready-made pieces for replicating scene entities: a codec for the scene's transform, and the small
// set of RPCs a dedicated server and its clients agree on. A game adds its own components and RPCs
// next to these (the registries are plain objects); both ends must build the same ones.
namespace kb::net {

enum class TransformGroup { Position, Rotation, Scale };

// Independent transform groups encode position in 6 bytes, rotation in 4, and scale in 6.
class SceneTransformCodec final : public ComponentCodec {
public:
    explicit SceneTransformCodec(float extent, TransformGroup group = TransformGroup::Position);

    [[nodiscard]] const std::string& Name() const noexcept override { return name_; }
    [[nodiscard]] bool Valid() const noexcept override;
    [[nodiscard]] std::size_t EncodedSize() const noexcept override { return group_ == TransformGroup::Rotation ? 4U : 6U; }
    [[nodiscard]] kb::ecs::ComponentId SourceComponent(kb::ecs::World& world) const override;
    [[nodiscard]] std::array<kb::ecs::ObserverId, 3U> ObserveChanges(kb::ecs::World& world, ReplicationChangeSink& sink) const override;
    [[nodiscard]] std::string Describe() const override;
    [[nodiscard]] bool Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const override;
    [[nodiscard]] bool Validate(std::span<const std::uint8_t> bytes) const override;
    void Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const override;
    void Remove(kb::ecs::World& world, kb::ecs::Entity entity) const override;

private:
    std::string name_;
    float extent_;
    TransformGroup group_;
};

// The registry shared by project servers, clients and tools.
[[nodiscard]] std::unique_ptr<ReplicationRegistry> BuildReplicationRegistry(const kb::ecs::World& world,
    const kb::network::ReplicationDeclarations& declarations, float worldExtent, std::string& error);

// The RPCs every dedicated server defines, first and in this order:
//   Ping(UInt32 token)            client -> server, unreliable; the server answers with Pong(token)
//   Pong(UInt32 token)            server -> client, unreliable
//   Say(String text)              client -> server, reliable, at most 5 per second; the server broadcasts Said
//   Said(UInt32 client, String)   server -> client, reliable
// Further definitions are added after these.
void DefineStandardRpcs(RpcRegistry& registry);

} // namespace kb::net
