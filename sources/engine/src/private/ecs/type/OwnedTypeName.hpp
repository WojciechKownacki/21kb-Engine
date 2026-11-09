#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace kb::ecs {
// RTTI belongs to its defining module. Own the name in persistent registries;
// heterogeneous lookup keeps reads allocation-free while a module is loaded.
struct OwnedTypeNameHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view name) const noexcept {
        return std::hash<std::string_view>{}(name);
    }
};
} // namespace kb::ecs
