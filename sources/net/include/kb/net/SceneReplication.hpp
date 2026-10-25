#pragma once

#include "kb/net/Replication.hpp"
#include "kb/net/Rpc.hpp"

#include <memory>

// Ready-made pieces for replicating scene entities: a codec for the scene's transform, and the small
// set of RPCs a dedicated server and its clients agree on. A game adds its own components and RPCs
// next to these (the registries are plain objects); both ends must build the same ones.
namespace kb::net {

// Replicates the local transform of an entity: position over [-extent, extent] on each axis with 16
// bits (a step of extent / 32767; 3 cm over a 2 km world), rotation as a quaternion with 16 bits per
// component (renormalised on arrival), scale over [0, 64) with 16 bits. 20 bytes. The hierarchy is not
// replicated: a client sees the local transform as its world transform.
class SceneTransformCodec final : public ComponentCodec {
public:
    explicit SceneTransformCodec(float extent);

    [[nodiscard]] const std::string& Name() const noexcept override { return name_; }
    [[nodiscard]] bool Valid() const noexcept override;
    [[nodiscard]] std::size_t EncodedSize() const noexcept override { return 20U; }
    [[nodiscard]] std::string Describe() const override;
    [[nodiscard]] bool Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const override;
    [[nodiscard]] bool Validate(std::span<const std::uint8_t> bytes) const override;
    void Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const override;
    void Remove(kb::ecs::World& world, kb::ecs::Entity entity) const override;

private:
    std::string name_ = "Transform";
    float extent_;
};

// A registry with the scene transform codec.
[[nodiscard]] std::unique_ptr<ReplicationRegistry> MakeSceneReplicationRegistry(float worldExtent);

// The RPCs every dedicated server defines, first and in this order:
//   Ping(UInt32 token)            client -> server, unreliable; the server answers with Pong(token)
//   Pong(UInt32 token)            server -> client, unreliable
//   Say(String text)              client -> server, reliable, at most 5 per second; the server broadcasts Said
//   Said(UInt32 client, String)   server -> client, reliable
// Further definitions are added after these.
void DefineStandardRpcs(RpcRegistry& registry);

} // namespace kb::net
