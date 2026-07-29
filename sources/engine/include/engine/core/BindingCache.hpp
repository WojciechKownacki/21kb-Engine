#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace kb::core {

using BindingId = std::uint32_t;
inline constexpr BindingId kInvalidBindingId = 0U;

class BindingCache final {
public:
    explicit BindingCache(std::size_t reservedBindings = 0U) {
        bindings_.reserve(reservedBindings);
    }

    [[nodiscard]] BindingId Register(std::string name) {
        if (name.empty()) {
            return kInvalidBindingId;
        }

        if (const BindingId existing = Find(name); existing != kInvalidBindingId) {
            return existing;
        }
        if (nextId_ == std::numeric_limits<BindingId>::max()) {
            return kInvalidBindingId;
        }

        const BindingId id = nextId_++;
        bindings_.push_back(Binding{ .name = std::move(name), .id = id });
        return id;
    }

    [[nodiscard]] BindingId Find(std::string_view name) const noexcept {
        for (const Binding& binding : bindings_) {
            if (binding.name == name) {
                return binding.id;
            }
        }
        return kInvalidBindingId;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return bindings_.size();
    }

private:
    struct Binding {
        std::string name;
        BindingId id = kInvalidBindingId;
    };

    std::vector<Binding> bindings_;
    BindingId nextId_ = 1U;
};

} // namespace kb::core
