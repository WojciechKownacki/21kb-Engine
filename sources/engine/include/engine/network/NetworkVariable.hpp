#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace kb::network {

template <typename T>
concept NetworkVariableValue = std::is_arithmetic_v<T> || std::is_enum_v<T>;

template <NetworkVariableValue T>
class NetworkVariable final {
public:
    using ChangedCallback = void (*)(void* context, T previous, T current) noexcept;

    constexpr explicit NetworkVariable(T initial = {}) noexcept : value_(initial) {}
    [[nodiscard]] constexpr T Value() const noexcept { return value_; }
    [[nodiscard]] constexpr std::uint64_t Revision() const noexcept { return revision_; }
    void SetChangedCallback(ChangedCallback callback, void* context = nullptr) noexcept { callback_ = callback; context_ = context; }
    [[nodiscard]] bool Set(T value) noexcept { if(value_==value||revision_==std::numeric_limits<std::uint64_t>::max())return false; const T previous=value_; value_=value; ++revision_; if(callback_!=nullptr)callback_(context_,previous,value_); return true; }
    [[nodiscard]] bool Apply(T value, std::uint64_t revision) noexcept { if(revision<=revision_)return false; const T previous=value_; value_=value; revision_=revision; if(callback_!=nullptr)callback_(context_,previous,value_); return true; }

private:
    T value_{};
    std::uint64_t revision_ = 0U;
    ChangedCallback callback_ = nullptr;
    void* context_ = nullptr;
};

} // namespace kb::network
