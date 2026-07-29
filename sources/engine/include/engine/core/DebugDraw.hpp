#pragma once

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::core {

enum class DebugDrawKind { Line, Ray, Box, Sphere, Text };

struct DebugDrawCommand {
    DebugDrawKind kind{};
    kb::math::Vec3 a{};
    kb::math::Vec3 b{};
    float duration = 0.0F;
    std::uint32_t channel = 0U;
    std::string text;
};

class DebugDrawBuffer final {
public:
    explicit DebugDrawBuffer(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] static constexpr bool Enabled() noexcept {
#if defined(NDEBUG)
        return false;
#else
        return true;
#endif
    }

    [[nodiscard]] bool DrawLine(kb::math::Vec3 from, kb::math::Vec3 to,
        float duration, std::uint32_t channel = 0U) {
        return Add({ .kind = DebugDrawKind::Line, .a = from, .b = to,
            .duration = duration, .channel = channel });
    }

    [[nodiscard]] bool DrawRay(kb::math::Vec3 origin, kb::math::Vec3 direction,
        float duration, std::uint32_t channel = 0U) {
        return Add({ .kind = DebugDrawKind::Ray, .a = origin, .b = direction,
            .duration = duration, .channel = channel });
    }

    [[nodiscard]] bool DrawBox(kb::math::Vec3 center, kb::math::Vec3 extents,
        float duration, std::uint32_t channel = 0U) {
        return Add({ .kind = DebugDrawKind::Box, .a = center, .b = extents,
            .duration = duration, .channel = channel });
    }

    [[nodiscard]] bool DrawSphere(kb::math::Vec3 center, float radius,
        float duration, std::uint32_t channel = 0U) {
        return Add({ .kind = DebugDrawKind::Sphere, .a = center,
            .b = { radius, 0.0F, 0.0F }, .duration = duration,
            .channel = channel });
    }

    [[nodiscard]] bool DrawText(kb::math::Vec3 position, std::string_view text,
        float duration, std::uint32_t channel = 0U) {
        return Add({ .kind = DebugDrawKind::Text, .a = position,
            .duration = duration, .channel = channel, .text = std::string{ text } });
    }

    [[nodiscard]] bool Add(DebugDrawCommand command) {
        if (!Enabled() || commands_.size() == capacity_ ||
            !std::isfinite(command.duration) || command.duration <= 0.0F ||
            !IsFinite(command.a) || !IsFinite(command.b) ||
            (command.kind == DebugDrawKind::Sphere && command.b.x <= 0.0F) ||
            (command.kind == DebugDrawKind::Text && command.text.empty())) {
            return false;
        }
        commands_.push_back(std::move(command));
        return true;
    }

    void Advance(float delta) {
        if (!std::isfinite(delta) || delta < 0.0F) return;
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
            [delta](DebugDrawCommand& command) {
                command.duration -= delta;
                return command.duration <= 0.0F;
            }), commands_.end());
    }

    [[nodiscard]] const std::vector<DebugDrawCommand>& Commands() const noexcept {
        return commands_;
    }

private:
    [[nodiscard]] static bool IsFinite(const kb::math::Vec3& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    std::size_t capacity_;
    std::vector<DebugDrawCommand> commands_;
};

} // namespace kb::core
