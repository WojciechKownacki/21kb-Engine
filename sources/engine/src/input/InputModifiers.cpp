#include "engine/input/InputModifiers.hpp"

#include <algorithm>
#include <cmath>

namespace kb::input {
namespace {

[[nodiscard]] float ParamOrDefault(float value, float fallback) noexcept {
    return value != 0.0F ? value : fallback;
}

[[nodiscard]] float SignedPow(float value, float exponent) noexcept {
    const float magnitude = std::pow(std::fabs(value), exponent);
    return value < 0.0F ? -magnitude : magnitude;
}

InputValue ApplyNegate(InputValue value, const InputModifierDesc& modifier) {
    // Default (all params zero) negates every axis, matching Unreal's defaults;
    // otherwise only the axes whose flag is set are negated.
    const bool anyFlag = modifier.params[0] != 0.0F || modifier.params[1] != 0.0F || modifier.params[2] != 0.0F;
    const bool negX = !anyFlag || modifier.params[0] != 0.0F;
    const bool negY = !anyFlag || modifier.params[1] != 0.0F;
    const bool negZ = !anyFlag || modifier.params[2] != 0.0F;
    if (negX) {
        value.x = -value.x;
    }
    if (negY) {
        value.y = -value.y;
    }
    if (negZ) {
        value.z = -value.z;
    }
    return value;
}

InputValue ApplyScalar(InputValue value, const InputModifierDesc& modifier) {
    // A zero scale is treated as identity so a default-constructed Scalar is a
    // no-op; deliberately zeroing an axis is better expressed via other means.
    value.x *= ParamOrDefault(modifier.params[0], 1.0F);
    value.y *= ParamOrDefault(modifier.params[1], 1.0F);
    value.z *= ParamOrDefault(modifier.params[2], 1.0F);
    return value;
}

InputValue ApplyDeadZone(InputValue value, const InputModifierDesc& modifier) {
    const float lower = ParamOrDefault(modifier.params[0], 0.2F);
    const float upper = ParamOrDefault(modifier.params[1], 1.0F);
    const auto type = static_cast<InputDeadZoneType>(static_cast<std::uint8_t>(modifier.params[2]));
    const float range = std::max(upper - lower, 1e-6F);

    const auto rescale = [&](float magnitude) {
        const float clamped = std::clamp(magnitude, lower, upper);
        return (clamped - lower) / range;
    };

    if (type == InputDeadZoneType::Radial) {
        const float magnitude = value.Magnitude();
        if (magnitude <= lower) {
            return InputValue{.x = 0.0F, .y = 0.0F, .z = 0.0F, .type = value.type};
        }
        const float scale = rescale(magnitude) / magnitude;
        value.x *= scale;
        value.y *= scale;
        value.z *= scale;
        return value;
    }

    const auto axial = [&](float axis) {
        const float magnitude = std::fabs(axis);
        if (magnitude <= lower) {
            return 0.0F;
        }
        const float scaled = rescale(magnitude);
        return axis < 0.0F ? -scaled : scaled;
    };
    value.x = axial(value.x);
    value.y = axial(value.y);
    value.z = axial(value.z);
    return value;
}

InputValue ApplySwizzle(InputValue value, const InputModifierDesc& modifier) {
    const auto order = static_cast<InputSwizzleOrder>(static_cast<std::uint8_t>(modifier.params[0]));
    const float x = value.x;
    const float y = value.y;
    const float z = value.z;
    switch (order) {
        case InputSwizzleOrder::YXZ:
            value.x = y;
            value.y = x;
            value.z = z;
            break;
        case InputSwizzleOrder::ZYX:
            value.x = z;
            value.y = y;
            value.z = x;
            break;
        case InputSwizzleOrder::XZY:
            value.x = x;
            value.y = z;
            value.z = y;
            break;
        case InputSwizzleOrder::YZX:
            value.x = y;
            value.y = z;
            value.z = x;
            break;
        case InputSwizzleOrder::ZXY:
            value.x = z;
            value.y = x;
            value.z = y;
            break;
    }
    return value;
}

InputValue ApplySmooth(InputValue value, const InputModifierDesc& modifier, float dt, ModifierRuntimeState& state) {
    const float rate = ParamOrDefault(modifier.params[0], 10.0F);
    const float alpha = std::clamp(dt * rate, 0.0F, 1.0F);
    if (!state.hasSmoothed) {
        state.smoothedX = value.x;
        state.smoothedY = value.y;
        state.smoothedZ = value.z;
        state.hasSmoothed = true;
    } else {
        state.smoothedX += (value.x - state.smoothedX) * alpha;
        state.smoothedY += (value.y - state.smoothedY) * alpha;
        state.smoothedZ += (value.z - state.smoothedZ) * alpha;
    }
    value.x = state.smoothedX;
    value.y = state.smoothedY;
    value.z = state.smoothedZ;
    return value;
}

InputValue ApplyResponseCurve(InputValue value, const InputModifierDesc& modifier) {
    value.x = SignedPow(value.x, ParamOrDefault(modifier.params[0], 2.0F));
    value.y = SignedPow(value.y, ParamOrDefault(modifier.params[1], 2.0F));
    value.z = SignedPow(value.z, ParamOrDefault(modifier.params[2], 2.0F));
    return value;
}

} // namespace

InputValue ApplyModifier(InputValue value, const InputModifierDesc& modifier, float dt,
                         ModifierRuntimeState& state) {
    switch (modifier.type) {
        case InputModifierType::Negate:
            return ApplyNegate(value, modifier);
        case InputModifierType::Scalar:
            return ApplyScalar(value, modifier);
        case InputModifierType::DeadZone:
            return ApplyDeadZone(value, modifier);
        case InputModifierType::SwizzleAxis:
            return ApplySwizzle(value, modifier);
        case InputModifierType::Smooth:
            return ApplySmooth(value, modifier, dt, state);
        case InputModifierType::ResponseCurveExponential:
            return ApplyResponseCurve(value, modifier);
        case InputModifierType::FovScaling:
            // Not yet supported (requires camera context); pass through unchanged.
            return value;
    }
    return value;
}

InputValue ApplyModifierStack(InputValue value, std::span<const InputModifierDesc> modifiers, float dt,
                              ModifierRuntimeState& state) {
    for (const InputModifierDesc& modifier : modifiers) {
        value = ApplyModifier(value, modifier, dt, state);
    }
    return value;
}

} // namespace kb::input
