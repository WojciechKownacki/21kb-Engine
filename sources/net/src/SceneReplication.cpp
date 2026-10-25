#include "kb/net/SceneReplication.hpp"

#include "engine/scene/TransformComponent.hpp"

#include <algorithm>
#include <cmath>

namespace kb::net {

namespace {

constexpr std::uint32_t kLevels = 65535U;
constexpr float kScaleLimit = 64.0F;

[[nodiscard]] std::uint16_t Encode(float value, float minimum, float maximum) noexcept {
    const float clamped = std::isnan(value) ? minimum : std::clamp(value, minimum, maximum);
    return static_cast<std::uint16_t>(std::lround((clamped - minimum) / (maximum - minimum) * static_cast<float>(kLevels)));
}

[[nodiscard]] float Decode(std::uint16_t value, float minimum, float maximum) noexcept {
    return minimum + (maximum - minimum) * (static_cast<float>(value) / static_cast<float>(kLevels));
}

[[nodiscard]] std::uint16_t Read(const std::uint8_t* bytes, std::size_t index) noexcept {
    return static_cast<std::uint16_t>(bytes[index * 2U] | (bytes[index * 2U + 1U] << 8U));
}

void Write(std::uint8_t* bytes, std::size_t index, std::uint16_t value) noexcept {
    bytes[index * 2U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[index * 2U + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

} // namespace

SceneTransformCodec::SceneTransformCodec(float extent) : extent_(extent) {}

bool SceneTransformCodec::Valid() const noexcept {
    return std::isfinite(extent_) && extent_ > 0.0F && extent_ <= 1.0e7F;
}

std::string SceneTransformCodec::Describe() const {
    return "SceneTransform|extent:" + std::to_string(extent_) + "|pos16|quat16|scale16";
}

bool SceneTransformCodec::Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const {
    const kb::scene::TransformComponent* transform = world.TryGet<kb::scene::TransformComponent>(entity);
    if (transform == nullptr || out.size() != EncodedSize()) {
        return false;
    }
    std::uint8_t* bytes = out.data();
    Write(bytes, 0U, Encode(transform->localPosition.x, -extent_, extent_));
    Write(bytes, 1U, Encode(transform->localPosition.y, -extent_, extent_));
    Write(bytes, 2U, Encode(transform->localPosition.z, -extent_, extent_));
    Write(bytes, 3U, Encode(transform->localRotation.x, -1.0F, 1.0F));
    Write(bytes, 4U, Encode(transform->localRotation.y, -1.0F, 1.0F));
    Write(bytes, 5U, Encode(transform->localRotation.z, -1.0F, 1.0F));
    Write(bytes, 6U, Encode(transform->localRotation.w, -1.0F, 1.0F));
    Write(bytes, 7U, Encode(transform->localScale.x, 0.0F, kScaleLimit));
    Write(bytes, 8U, Encode(transform->localScale.y, 0.0F, kScaleLimit));
    Write(bytes, 9U, Encode(transform->localScale.z, 0.0F, kScaleLimit));
    return true;
}

bool SceneTransformCodec::Validate(std::span<const std::uint8_t> bytes) const {
    // Every 16 bit value is inside its range, so only the size can be wrong; a quaternion of length zero
    // is replaced by the identity when applied.
    return bytes.size() == EncodedSize();
}

void SceneTransformCodec::Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const {
    if (bytes.size() != EncodedSize()) {
        return;
    }
    const std::uint8_t* data = bytes.data();
    const kb::scene::TransformComponent* existing = world.TryGet<kb::scene::TransformComponent>(entity);
    kb::scene::TransformComponent transform = existing != nullptr ? *existing : kb::scene::TransformComponent{};
    transform.localPosition = { Decode(Read(data, 0U), -extent_, extent_), Decode(Read(data, 1U), -extent_, extent_), Decode(Read(data, 2U), -extent_, extent_) };
    kb::scene::Quat rotation{ Decode(Read(data, 3U), -1.0F, 1.0F), Decode(Read(data, 4U), -1.0F, 1.0F), Decode(Read(data, 5U), -1.0F, 1.0F),
        Decode(Read(data, 6U), -1.0F, 1.0F) };
    const float length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
    if (length < 1.0e-4F) {
        rotation = kb::scene::Quat{};
    } else {
        rotation = { rotation.x / length, rotation.y / length, rotation.z / length, rotation.w / length };
    }
    transform.localRotation = rotation;
    transform.localScale = { Decode(Read(data, 7U), 0.0F, kScaleLimit), Decode(Read(data, 8U), 0.0F, kScaleLimit), Decode(Read(data, 9U), 0.0F, kScaleLimit) };
    transform.worldPosition = transform.localPosition;
    transform.worldRotation = transform.localRotation;
    transform.worldScale = transform.localScale;
    transform.worldDirty = false;
    world.Set(entity, transform);
}

void SceneTransformCodec::Remove(kb::ecs::World& world, kb::ecs::Entity entity) const {
    if (world.Has<kb::scene::TransformComponent>(entity)) {
        world.Remove<kb::scene::TransformComponent>(entity);
    }
}

std::unique_ptr<ReplicationRegistry> MakeSceneReplicationRegistry(float worldExtent) {
    auto registry = std::make_unique<ReplicationRegistry>();
    std::string error;
    if (!registry->Add(std::make_unique<SceneTransformCodec>(worldExtent), error)) {
        return nullptr;
    }
    return registry;
}

void DefineStandardRpcs(RpcRegistry& registry) {
    std::string error;
    static_cast<void>(registry.Define({ "Ping", RpcDirection::ClientToServer, false, { { RpcArgType::UInt32, 0.0, 4294967295.0 } }, -1, 60U }, error));
    static_cast<void>(registry.Define({ "Pong", RpcDirection::ServerToClient, false, { { RpcArgType::UInt32, 0.0, 4294967295.0 } }, -1, 60U }, error));
    static_cast<void>(registry.Define({ "Say", RpcDirection::ClientToServer, true, { { RpcArgType::String, 0.0, 0.0, 200U } }, -1, 5U }, error));
    static_cast<void>(registry.Define({ "Said", RpcDirection::ServerToClient, true,
        { { RpcArgType::UInt32, 0.0, 4294967295.0 }, { RpcArgType::String, 0.0, 0.0, 200U } }, -1, 60U }, error));
}

} // namespace kb::net
