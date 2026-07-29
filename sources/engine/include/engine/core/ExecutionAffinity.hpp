#pragma once

#include <cstdint>

namespace kb::core {
enum class ExecutionAffinity : std::uint8_t { MainThread, WorkerSafe, RenderCommand, Forbidden };
struct ApiExecutionContract { ExecutionAffinity affinity = ExecutionAffinity::MainThread; };
[[nodiscard]] constexpr bool MayCall(ExecutionAffinity current, ApiExecutionContract contract) noexcept { return contract.affinity != ExecutionAffinity::Forbidden && (contract.affinity == ExecutionAffinity::WorkerSafe || contract.affinity == current); }
} // namespace kb::core
