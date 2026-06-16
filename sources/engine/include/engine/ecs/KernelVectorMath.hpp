#pragma once

#include "engine/ecs/Kernel.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

namespace kb::ecs {

template <typename BackendTag>
class KernelLaneMask {
public:
    static constexpr std::size_t LaneCount = BackendTag::FloatLaneCount;

    constexpr KernelLaneMask() noexcept = default;
    explicit constexpr KernelLaneMask(bool value) noexcept {
        for (bool& lane : lanes_) {
            lane = value;
        }
    }

    [[nodiscard]] static constexpr KernelLaneMask Splat(bool value) noexcept {
        return KernelLaneMask{ value };
    }

    [[nodiscard]] constexpr bool Lane(std::size_t index) const noexcept {
        return lanes_[index];
    }

    [[nodiscard]] constexpr bool All() const noexcept {
        for (bool lane : lanes_) {
            if (!lane) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool Any() const noexcept {
        for (bool lane : lanes_) {
            if (lane) {
                return true;
            }
        }
        return false;
    }

    constexpr void SetLane(std::size_t index, bool value) noexcept {
        lanes_[index] = value;
    }

private:
    std::array<bool, LaneCount> lanes_{};
};

template <typename BackendTag>
class KernelFloatLanes {
public:
    using Backend = BackendTag;
    static constexpr std::size_t LaneCount = BackendTag::FloatLaneCount;

    constexpr KernelFloatLanes() noexcept = default;
    explicit constexpr KernelFloatLanes(float value) noexcept {
        for (float& lane : lanes_) {
            lane = value;
        }
    }

    explicit constexpr KernelFloatLanes(std::array<float, LaneCount> lanes) noexcept
        : lanes_(lanes) {}

    [[nodiscard]] static constexpr KernelFloatLanes Zero() noexcept {
        return KernelFloatLanes{};
    }

    [[nodiscard]] static constexpr KernelFloatLanes Splat(float value) noexcept {
        return KernelFloatLanes{ value };
    }

    [[nodiscard]] static KernelFloatLanes Load(const float* values) noexcept {
        KernelFloatLanes result;
        for (std::size_t lane = 0; lane < LaneCount; ++lane) {
            result.lanes_[lane] = values[lane];
        }
        return result;
    }

    [[nodiscard]] static KernelFloatLanes LoadPartial(const float* values, std::size_t count, float fill = 0.0F) noexcept {
        KernelFloatLanes result{ fill };
        for (std::size_t lane = 0; lane < LaneCount && lane < count; ++lane) {
            result.lanes_[lane] = values[lane];
        }
        return result;
    }

    template <typename Component>
    [[nodiscard]] static KernelFloatLanes LoadMember(const Component* components, float Component::*member) noexcept {
        return LoadMemberPartial(components, LaneCount, member);
    }

    template <typename Component>
    [[nodiscard]] static KernelFloatLanes LoadMemberPartial(const Component* components, std::size_t count, float Component::*member, float fill = 0.0F) noexcept {
        KernelFloatLanes result{ fill };
        for (std::size_t lane = 0; lane < LaneCount && lane < count; ++lane) {
            result.lanes_[lane] = components[lane].*member;
        }
        return result;
    }

    void Store(float* values) const noexcept {
        for (std::size_t lane = 0; lane < LaneCount; ++lane) {
            values[lane] = lanes_[lane];
        }
    }

    void StorePartial(float* values, std::size_t count) const noexcept {
        for (std::size_t lane = 0; lane < LaneCount && lane < count; ++lane) {
            values[lane] = lanes_[lane];
        }
    }

    template <typename Component>
    void StoreMember(Component* components, float Component::*member) const noexcept {
        StoreMemberPartial(components, LaneCount, member);
    }

    template <typename Component>
    void StoreMemberPartial(Component* components, std::size_t count, float Component::*member) const noexcept {
        for (std::size_t lane = 0; lane < LaneCount && lane < count; ++lane) {
            components[lane].*member = lanes_[lane];
        }
    }

    [[nodiscard]] constexpr float Lane(std::size_t index) const noexcept {
        return lanes_[index];
    }

    constexpr void SetLane(std::size_t index, float value) noexcept {
        lanes_[index] = value;
    }

private:
    std::array<float, LaneCount> lanes_{};
};

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> operator+(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) + rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> operator-(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) - rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> operator*(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) * rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> operator/(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) / rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> KernelMin(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) < rhs.Lane(lane) ? lhs.Lane(lane) : rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> KernelMax(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) > rhs.Lane(lane) ? lhs.Lane(lane) : rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> KernelClamp(
    KernelFloatLanes<BackendTag> value,
    KernelFloatLanes<BackendTag> minimum,
    KernelFloatLanes<BackendTag> maximum) noexcept {
    return KernelMin(KernelMax(value, minimum), maximum);
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> KernelDeterministicMulAdd(
    KernelFloatLanes<BackendTag> multiplicand,
    KernelFloatLanes<BackendTag> multiplier,
    KernelFloatLanes<BackendTag> addend) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        const float product = multiplicand.Lane(lane) * multiplier.Lane(lane);
        result.SetLane(lane, product + addend.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelLaneMask<BackendTag> KernelGreaterThan(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelLaneMask<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) > rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelLaneMask<BackendTag> KernelLessThan(KernelFloatLanes<BackendTag> lhs, KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelLaneMask<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, lhs.Lane(lane) < rhs.Lane(lane));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> KernelSelect(
    KernelLaneMask<BackendTag> mask,
    KernelFloatLanes<BackendTag> trueValue,
    KernelFloatLanes<BackendTag> falseValue) noexcept {
    KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, mask.Lane(lane) ? trueValue.Lane(lane) : falseValue.Lane(lane));
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
class KernelVectorLanes {
public:
    static_assert(ComponentCount > 0U, "ECS kernel vectors must have at least one component");

    using Scalar = KernelFloatLanes<BackendTag>;
    static constexpr std::size_t LaneCount = Scalar::LaneCount;

    constexpr KernelVectorLanes() noexcept = default;

    explicit constexpr KernelVectorLanes(std::array<Scalar, ComponentCount> components) noexcept
        : components_(components) {}

    template <typename... Scalars>
        requires(sizeof...(Scalars) == ComponentCount && (std::is_same_v<std::decay_t<Scalars>, Scalar> && ...))
    explicit constexpr KernelVectorLanes(Scalars... components) noexcept
        : components_{ components... } {}

    template <typename Component, typename... Members>
        requires(sizeof...(Members) == ComponentCount && (std::is_same_v<Members, float Component::*> && ...))
    [[nodiscard]] static KernelVectorLanes LoadMembers(const Component* components, Members... members) noexcept {
        return LoadMembersPartial(components, LaneCount, 0.0F, members...);
    }

    template <typename Component, typename... Members>
        requires(sizeof...(Members) == ComponentCount && (std::is_same_v<Members, float Component::*> && ...))
    [[nodiscard]] static KernelVectorLanes LoadMembersPartial(const Component* components, std::size_t count, float fill, Members... members) noexcept {
        return KernelVectorLanes{ std::array<Scalar, ComponentCount>{
            Scalar::LoadMemberPartial(components, count, members, fill)...
        } };
    }

    template <typename Component, typename... Members>
        requires(sizeof...(Members) == ComponentCount && (std::is_same_v<Members, float Component::*> && ...))
    void StoreMembers(Component* components, Members... members) const noexcept {
        StoreMembersPartial(components, LaneCount, members...);
    }

    template <typename Component, typename... Members>
        requires(sizeof...(Members) == ComponentCount && (std::is_same_v<Members, float Component::*> && ...))
    void StoreMembersPartial(Component* components, std::size_t count, Members... members) const noexcept {
        StoreMembersPartialImpl(components, count, std::index_sequence_for<Members...>{}, members...);
    }

    template <std::size_t Index>
    [[nodiscard]] constexpr const Scalar& Component() const noexcept {
        static_assert(Index < ComponentCount, "ECS kernel vector component index is out of range");
        return components_[Index];
    }

    template <std::size_t Index>
    [[nodiscard]] constexpr Scalar& Component() noexcept {
        static_assert(Index < ComponentCount, "ECS kernel vector component index is out of range");
        return components_[Index];
    }

    [[nodiscard]] constexpr const Scalar& Component(std::size_t index) const noexcept {
        return components_[index];
    }

    [[nodiscard]] constexpr Scalar& Component(std::size_t index) noexcept {
        return components_[index];
    }

private:
    template <typename Component, typename... Members, std::size_t... Indices>
    void StoreMembersPartialImpl(Component* components, std::size_t count, std::index_sequence<Indices...>, Members... members) const noexcept {
        (components_[Indices].StoreMemberPartial(components, count, members), ...);
    }

    std::array<Scalar, ComponentCount> components_{};
};

template <typename BackendTag>
using KernelFloat2Lanes = KernelVectorLanes<BackendTag, 2U>;

template <typename BackendTag>
using KernelFloat3Lanes = KernelVectorLanes<BackendTag, 3U>;

template <typename BackendTag>
using KernelFloat4Lanes = KernelVectorLanes<BackendTag, 4U>;

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> operator+(
    KernelVectorLanes<BackendTag, ComponentCount> lhs,
    KernelVectorLanes<BackendTag, ComponentCount> rhs) noexcept {
    KernelVectorLanes<BackendTag, ComponentCount> result;
    for (std::size_t component = 0; component < ComponentCount; ++component) {
        result.Component(component) = lhs.Component(component) + rhs.Component(component);
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> operator-(
    KernelVectorLanes<BackendTag, ComponentCount> lhs,
    KernelVectorLanes<BackendTag, ComponentCount> rhs) noexcept {
    KernelVectorLanes<BackendTag, ComponentCount> result;
    for (std::size_t component = 0; component < ComponentCount; ++component) {
        result.Component(component) = lhs.Component(component) - rhs.Component(component);
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> operator*(
    KernelVectorLanes<BackendTag, ComponentCount> lhs,
    KernelVectorLanes<BackendTag, ComponentCount> rhs) noexcept {
    KernelVectorLanes<BackendTag, ComponentCount> result;
    for (std::size_t component = 0; component < ComponentCount; ++component) {
        result.Component(component) = lhs.Component(component) * rhs.Component(component);
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> operator*(
    KernelVectorLanes<BackendTag, ComponentCount> lhs,
    KernelFloatLanes<BackendTag> rhs) noexcept {
    KernelVectorLanes<BackendTag, ComponentCount> result;
    for (std::size_t component = 0; component < ComponentCount; ++component) {
        result.Component(component) = lhs.Component(component) * rhs;
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> operator*(
    KernelFloatLanes<BackendTag> lhs,
    KernelVectorLanes<BackendTag, ComponentCount> rhs) noexcept {
    return rhs * lhs;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> KernelDeterministicMulAdd(
    KernelVectorLanes<BackendTag, ComponentCount> multiplicand,
    KernelFloatLanes<BackendTag> multiplier,
    KernelVectorLanes<BackendTag, ComponentCount> addend) noexcept {
    KernelVectorLanes<BackendTag, ComponentCount> result;
    for (std::size_t component = 0; component < ComponentCount; ++component) {
        result.Component(component) = KernelDeterministicMulAdd(multiplicand.Component(component), multiplier, addend.Component(component));
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelFloatLanes<BackendTag> KernelDot(
    KernelVectorLanes<BackendTag, ComponentCount> lhs,
    KernelVectorLanes<BackendTag, ComponentCount> rhs) noexcept {
    KernelFloatLanes<BackendTag> result = lhs.template Component<0>() * rhs.template Component<0>();
    for (std::size_t component = 1; component < ComponentCount; ++component) {
        result = result + (lhs.Component(component) * rhs.Component(component));
    }
    return result;
}

template <typename BackendTag, std::size_t ComponentCount>
[[nodiscard]] constexpr KernelVectorLanes<BackendTag, ComponentCount> KernelSelect(
    KernelLaneMask<BackendTag> mask,
    KernelVectorLanes<BackendTag, ComponentCount> trueValue,
    KernelVectorLanes<BackendTag, ComponentCount> falseValue) noexcept {
    KernelVectorLanes<BackendTag, ComponentCount> result;
    for (std::size_t component = 0; component < ComponentCount; ++component) {
        result.Component(component) = KernelSelect(mask, trueValue.Component(component), falseValue.Component(component));
    }
    return result;
}

} // namespace kb::ecs
