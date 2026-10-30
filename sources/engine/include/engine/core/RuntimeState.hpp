#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace kb::core {

// Detached, process-local provider state. PayloadBytes excludes container capacity
// and retained external resources; it measures the provider's serialized payload.
class RuntimeStateSnapshot {
public:
    virtual ~RuntimeStateSnapshot() = default;
    [[nodiscard]] virtual std::size_t PayloadBytes() const noexcept = 0;
};

// Optional interface, queried separately from an existing provider ABI. Call on
// the owner thread between updates. Capture may finish pending simulation work.
// Validate rejects incompatible identity/topology without applying state.
class IRuntimeStateProvider {
public:
    virtual ~IRuntimeStateProvider() = default;
    [[nodiscard]] virtual std::unique_ptr<RuntimeStateSnapshot> CaptureRuntimeState(std::string& error) = 0;
    [[nodiscard]] virtual bool ValidateRuntimeState(const RuntimeStateSnapshot& snapshot, std::string& error) const = 0;
    [[nodiscard]] virtual bool RestoreRuntimeState(const RuntimeStateSnapshot& snapshot, std::string& error) = 0;
};

} // namespace kb::core
