#pragma once

#include "engine/modules/EngineModuleSdk.h"

#include <memory>

namespace kb::modules {
struct ModuleCallbackState;

// Engine-owned handle: retaining it never retains a plugin function or allocator
// after detach. Invocation then returns STALE_HANDLE without entering the DLL.
class EngineModuleCallback final {
public:
    EngineModuleCallback() = default;
    [[nodiscard]] KbModuleStatus Invoke(float deltaSeconds, KbModuleError* error = nullptr) const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
private:
    friend EngineModuleCallback RetainEngineModuleCallback(KbModuleRegistration);
    std::shared_ptr<ModuleCallbackState> state_;
};

[[nodiscard]] EngineModuleCallback RetainEngineModuleCallback(KbModuleRegistration registration);
} // namespace kb::modules
