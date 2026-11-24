#include "kb/net/SceneReplication.hpp"
#include "ReflectedFieldsCodec.hpp"

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

SceneTransformCodec::SceneTransformCodec(float extent, TransformGroup group) : extent_(extent), group_(group) {
    name_ = group == TransformGroup::Position ? "Transform.Position" : group == TransformGroup::Rotation ? "Transform.Rotation" : "Transform.Scale";
}

bool SceneTransformCodec::Valid() const noexcept {
    return std::isfinite(extent_) && extent_ > 0.0F && extent_ <= 1.0e7F;
}

kb::ecs::ComponentId SceneTransformCodec::SourceComponent(kb::ecs::World& world) const {
    return world.RegisterComponent<kb::scene::TransformComponent>();
}
std::array<kb::ecs::ObserverId, 3U> SceneTransformCodec::ObserveChanges(kb::ecs::World& world, ReplicationChangeSink& sink) const {
    return ObserveReplicationChanges<kb::scene::TransformComponent>(world, sink);
}

std::string SceneTransformCodec::Describe() const {
    return name_ + "|audience:" + std::string(kb::network::ReplicationAudienceName(Audience())) + "|extent:" + std::to_string(extent_) + "|pos16|smallest-three10|scale16";
}

bool SceneTransformCodec::Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const {
    const auto* transform = world.TryGet<kb::scene::TransformComponent>(entity);
    if (transform == nullptr || out.size() != EncodedSize()) return false;
    if (group_ != TransformGroup::Rotation) {
        const auto& value = group_ == TransformGroup::Position ? transform->localPosition : transform->localScale;
        const auto minimum = group_ == TransformGroup::Position ? -extent_ : 0.0F;
        const auto maximum = group_ == TransformGroup::Position ? extent_ : kScaleLimit;
        Write(out.data(), 0U, Encode(value.x, minimum, maximum));
        Write(out.data(), 1U, Encode(value.y, minimum, maximum));
        Write(out.data(), 2U, Encode(value.z, minimum, maximum));
    } else {
        const auto& q = transform->localRotation;
        std::array<float, 4> values{q.x, q.y, q.z, q.w};
        float length = 0.0F;
        for (const auto value : values) length += value * value;
        if (!std::isfinite(length) || length < 1.0e-12F) values = {0.0F, 0.0F, 0.0F, 1.0F};
        else for (auto& value : values) value /= std::sqrt(length);
        std::uint32_t largest = 0U;
        for (std::uint32_t i = 1; i < 4U; ++i) if (std::abs(values[i]) > std::abs(values[largest])) largest = i;
        const float sign = values[largest] < 0.0F ? -1.0F : 1.0F;
        constexpr float limit = 0.7071067811865475F;
        std::array<std::uint32_t, 3> lower{};
        std::array<std::uint32_t, 3> indices{};
        std::size_t component = 0U;
        for (std::uint32_t i = 0; i < 4U; ++i) {
            if (i == largest) continue;
            const auto normalized = std::clamp((values[i] * sign + limit) / (2.0F * limit), 0.0F, 1.0F);
            lower[component] = static_cast<std::uint32_t>(std::floor(normalized * 1023.0F));
            indices[component++] = i;
        }
        std::uint32_t packed = largest;
        double bestDot = -1.0;
        for (unsigned choice = 0; choice < 8U; ++choice) {
            std::uint32_t candidate = largest;
            std::array<float, 4> decoded{};
            float sum = 0.0F;
            for (unsigned i = 0; i < 3U; ++i) {
                const auto quantized = std::min(1023U, lower[i] + ((choice >> i) & 1U));
                candidate |= quantized << (2U + 10U * i);
                const float value = static_cast<float>(quantized) / 1023.0F * (2.0F * limit) - limit;
                decoded[indices[i]] = value;
                sum += value * value;
            }
            decoded[largest] = std::sqrt(std::max(0.0F, 1.0F - sum));
            double dot = 0.0, lengthSquared = 0.0;
            for (unsigned i = 0; i < 4U; ++i) { dot += static_cast<double>(decoded[i]) * values[i] * sign; lengthSquared += static_cast<double>(decoded[i]) * decoded[i]; }
            dot /= std::sqrt(lengthSquared);
            if (dot > bestDot) { bestDot = dot; packed = candidate; }
        }
        for (unsigned i = 0; i < 4U; ++i) out[i] = static_cast<std::uint8_t>(packed >> (8U * i));
    }
    return true;
}

bool SceneTransformCodec::Validate(std::span<const std::uint8_t> bytes) const { return bytes.size() == EncodedSize(); }

void SceneTransformCodec::Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const {
    if (bytes.size() != EncodedSize()) return;
    const auto* existing = world.TryGet<kb::scene::TransformComponent>(entity);
    auto transform = existing == nullptr ? kb::scene::TransformComponent{} : *existing;
    if (group_ == TransformGroup::Rotation) {
        std::uint32_t packed = 0U;
        for (unsigned i = 0; i < 4U; ++i) packed |= static_cast<std::uint32_t>(bytes[i]) << (8U * i);
        const auto largest = packed & 3U;
        std::array<float, 4> values{};
        constexpr float limit = 0.7071067811865475F;
        float sum = 0.0F;
        unsigned shift = 2U;
        for (std::uint32_t i = 0; i < 4U; ++i) {
            if (i == largest) continue;
            values[i] = static_cast<float>((packed >> shift) & 1023U) / 1023.0F * (2.0F * limit) - limit;
            sum += values[i] * values[i];
            shift += 10U;
        }
        values[largest] = std::sqrt(std::max(0.0F, 1.0F - sum));
        const float length = std::sqrt(sum + values[largest] * values[largest]);
        transform.localRotation = {values[0] / length, values[1] / length, values[2] / length, values[3] / length};
    } else {
        const auto minimum = group_ == TransformGroup::Position ? -extent_ : 0.0F;
        const auto maximum = group_ == TransformGroup::Position ? extent_ : kScaleLimit;
        auto& value = group_ == TransformGroup::Position ? transform.localPosition : transform.localScale;
        value = {Decode(Read(bytes.data(), 0U), minimum, maximum), Decode(Read(bytes.data(), 1U), minimum, maximum), Decode(Read(bytes.data(), 2U), minimum, maximum)};
    }
    transform.worldPosition = transform.localPosition;
    transform.worldRotation = transform.localRotation;
    transform.worldScale = transform.localScale;
    transform.worldDirty = false;
    world.Set(entity, transform);
}

void SceneTransformCodec::Remove(kb::ecs::World& world, kb::ecs::Entity entity) const {
    const auto* existing = world.TryGet<kb::scene::TransformComponent>(entity);
    if (existing == nullptr) return;
    auto transform = *existing;
    if (group_ == TransformGroup::Position) transform.localPosition = {};
    else if (group_ == TransformGroup::Rotation) transform.localRotation = {};
    else transform.localScale = {1.0F, 1.0F, 1.0F};
    transform.worldPosition = transform.localPosition;
    transform.worldRotation = transform.localRotation;
    transform.worldScale = transform.localScale;
    world.Set(entity, transform);
}

std::unique_ptr<ReplicationRegistry> BuildReplicationRegistry(const kb::ecs::World& world,
    const kb::network::ReplicationDeclarations& declarations, float worldExtent, std::string& error) {
    kb::network::ReplicationDeclarations canonical;
    if (!kb::network::ParseReplicationDeclarations(kb::network::WriteReplicationDeclarations(declarations), canonical, error)) return nullptr;
    auto registry = std::make_unique<ReplicationRegistry>();
    for (const auto group : {TransformGroup::Position, TransformGroup::Rotation, TransformGroup::Scale}) {
        if (!registry->Add(std::make_unique<SceneTransformCodec>(worldExtent, group), error)) return nullptr;
    }
    for (std::size_t first = 0U; first < canonical.fields.size();) {
        auto last = first + 1U;
        const auto& field = canonical.fields[first];
        while (last < canonical.fields.size() && canonical.fields[last].component == field.component && canonical.fields[last].audience == field.audience) ++last;
        const bool removeWhole = std::ranges::all_of(canonical.fields, [&](const auto& other) { return other.component != field.component || other.audience == field.audience; });
        auto codec = MakeReflectedFieldsCodec(world, {canonical.fields.begin() + first, canonical.fields.begin() + last}, removeWhole, error);
        if (!codec || !registry->Add(std::move(codec), error)) return nullptr;
        first = last;
    }
    for (const auto audience : {ReplicationAudience::Everyone, ReplicationAudience::Owner, ReplicationAudience::Others, ReplicationAudience::InitialOnly}) {
        if (std::ranges::none_of(canonical.variables, [audience](const auto& variable) { return variable.audience == audience; })) continue;
        auto codec = MakeNetworkedVariablesCodec(canonical, audience, error);
        if (!codec || !registry->Add(std::move(codec), error)) return nullptr;
    }
    error.clear();
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
