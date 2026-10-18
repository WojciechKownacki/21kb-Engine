#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

inline constexpr std::size_t kMaxSocketAttachmentNameBytes = 128U;

// Holds this entity at a socket - or, with no socket of that name, at a bone - of the character
// it hangs under: a sword in a hand, a quiver on a back, a helmet on a head. The character is the
// nearest ancestor with an animated skeleton, so attaching is parenting the object to the character
// and naming the socket, as Unreal's AttachToComponent with a socket name. The offset is applied in
// the socket's space. Every frame after animation the socket sets the object's position and
// rotation; its scale stays its own Transform's, so a sword is sized as any other object.
struct SocketAttachmentOffset {
    Vec3 position{};
    Quat rotation{};
};

struct SocketAttachmentComponent {
    static constexpr std::string_view StableId = "kb21.motion.socket-attachment";
    static constexpr std::uint32_t SchemaVersion = 1U;

    // ECS rows stay trivially copyable, so the name lives inline; read and write it with
    // SocketAttachmentName and SetSocketAttachmentName.
    std::array<char, kMaxSocketAttachmentNameBytes + 1U> socket{};
    std::uint32_t socketLength = 0U;
    SocketAttachmentOffset offset{};
    bool enabled = true;
};

[[nodiscard]] inline std::string_view SocketAttachmentName(const SocketAttachmentComponent& value) noexcept {
    return std::string_view{ value.socket.data(), value.socketLength <= kMaxSocketAttachmentNameBytes ? value.socketLength : 0U };
}

// False (and the component unchanged) for a name longer than 128 bytes or containing a NUL.
[[nodiscard]] inline bool SetSocketAttachmentName(SocketAttachmentComponent& value, std::string_view name) noexcept {
    if (name.size() > kMaxSocketAttachmentNameBytes || name.find('\0') != std::string_view::npos) return false;
    value.socket.fill('\0');
    for (std::size_t index = 0U; index < name.size(); ++index) value.socket[index] = name[index];
    value.socketLength = static_cast<std::uint32_t>(name.size());
    return true;
}

[[nodiscard]] inline SocketAttachmentComponent MakeSocketAttachment(
    std::string_view name, Vec3 offsetPosition = {}, Quat offsetRotation = {}) noexcept {
    SocketAttachmentComponent value{};
    static_cast<void>(SetSocketAttachmentName(value, name));
    value.offset = { .position = offsetPosition, .rotation = offsetRotation };
    return value;
}

[[nodiscard]] inline bool IsSocketAttachmentComponentValid(const SocketAttachmentComponent& value) noexcept {
    const auto finite = [](float component) { return std::isfinite(component); };
    const SocketAttachmentOffset& offset = value.offset;
    const float rotationLength = offset.rotation.x * offset.rotation.x + offset.rotation.y * offset.rotation.y +
        offset.rotation.z * offset.rotation.z + offset.rotation.w * offset.rotation.w;
    return value.socketLength <= kMaxSocketAttachmentNameBytes && value.socket[value.socketLength] == '\0' &&
        finite(offset.position.x) && finite(offset.position.y) && finite(offset.position.z) &&
        finite(offset.rotation.x) && finite(offset.rotation.y) && finite(offset.rotation.z) && finite(offset.rotation.w) &&
        rotationLength > 0.000001F;
}

} // namespace kb::scene
