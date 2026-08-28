#pragma once

#include <cstdint>

namespace kb::modules {

// Order in which a module's lifecycle runs relative to other modules. These are
// the loading phases referenced by ProjectModuleDescriptor::loadingPhase.
enum class EngineModuleLoadingPhase : std::uint8_t {
    EarliestPossible,
    PreDefault,
    Default,
    PostDefault,
};

[[nodiscard]] constexpr const char* ToString(EngineModuleLoadingPhase phase) noexcept {
    switch (phase) {
    case EngineModuleLoadingPhase::EarliestPossible:
        return "EarliestPossible";
    case EngineModuleLoadingPhase::PreDefault:
        return "PreDefault";
    case EngineModuleLoadingPhase::Default:
        return "Default";
    case EngineModuleLoadingPhase::PostDefault:
        return "PostDefault";
    }
    return "Default";
}

} // namespace kb::modules
