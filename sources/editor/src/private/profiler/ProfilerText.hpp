#pragma once

#include "engine/core/ProfilerRegistry.hpp"

#include <string_view>

namespace kb::editor {

// The profiler's names in the editor's language. A counter that is not built in (a system of the
// scene) has no translation and shows its own name.
struct ProfilerText final {
    ProfilerText() = delete;

    [[nodiscard]] static std::string_view Counter(const kb::core::ProfilerCounterInfo& info);
    [[nodiscard]] static std::string_view Group(std::string_view englishGroup);
    [[nodiscard]] static std::string_view Area(kb::core::ProfilerArea area);
};

} // namespace kb::editor
