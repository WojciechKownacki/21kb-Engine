#pragma once

#include <cstdint>
#include <string_view>

namespace kb::core {
enum class ExecutionAffinity : std::uint8_t { MainThread, WorkerSafe, RenderCommand, Forbidden };
[[nodiscard]] constexpr std::string_view ToString(ExecutionAffinity affinity) noexcept {
    switch (affinity) {
    case ExecutionAffinity::MainThread: return "main_thread";
    case ExecutionAffinity::WorkerSafe: return "worker_safe";
    case ExecutionAffinity::RenderCommand: return "render_thread_command";
    case ExecutionAffinity::Forbidden: return "forbidden";
    }
    return "forbidden";
}
struct ApiExecutionContract { ExecutionAffinity affinity = ExecutionAffinity::MainThread; };
[[nodiscard]] constexpr bool MayCall(ExecutionAffinity current, ApiExecutionContract contract) noexcept { return contract.affinity != ExecutionAffinity::Forbidden && (contract.affinity == ExecutionAffinity::WorkerSafe || contract.affinity == current); }
} // namespace kb::core
