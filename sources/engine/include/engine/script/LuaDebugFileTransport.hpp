#pragma once
#include "engine/script/LuaDebugSession.hpp"
#include <chrono>
#include <filesystem>
#include <string>

namespace kb::script {
// Explicit local endpoint; Poll runs bounded work on the runtime owner thread.
class LuaDebugFileServer final {
public:
    explicit LuaDebugFileServer(PucLuaScriptRuntime& runtime) : session_(runtime) {}
    ~LuaDebugFileServer();
    [[nodiscard]] bool Start(const std::filesystem::path& directory, std::string& error,
        std::chrono::milliseconds lease = std::chrono::seconds{30});
    void Poll();
    void Stop() noexcept;
private:
    LuaDebugSession session_;
    std::filesystem::path directory_;
    std::chrono::milliseconds lease_{30000};
    std::chrono::steady_clock::time_point lastRequest_{};
};
[[nodiscard]] kb::core::JsonValue SendLuaDebugFileRequest(const std::filesystem::path& directory,
    std::string_view request, std::chrono::milliseconds timeout = std::chrono::seconds{5});
} // namespace kb::script
