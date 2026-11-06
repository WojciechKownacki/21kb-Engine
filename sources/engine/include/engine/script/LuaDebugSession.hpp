#pragma once

#include "engine/core/JsonValue.hpp"
#include <string_view>

namespace kb::script {
class PucLuaScriptRuntime;

// Owner-thread command session shared by local and transported debugger clients.
class LuaDebugSession final {
public:
    explicit LuaDebugSession(PucLuaScriptRuntime& runtime) noexcept : runtime_(runtime) {}
    ~LuaDebugSession();
    LuaDebugSession(const LuaDebugSession&) = delete;
    LuaDebugSession& operator=(const LuaDebugSession&) = delete;
    [[nodiscard]] kb::core::JsonValue Execute(std::string_view request);
    void Disconnect() noexcept;
    [[nodiscard]] bool Connected() const noexcept { return connected_; }
private:
    PucLuaScriptRuntime& runtime_;
    bool connected_ = false;
};
} // namespace kb::script
