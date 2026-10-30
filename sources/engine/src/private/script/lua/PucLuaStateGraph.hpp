#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>

struct lua_State;

namespace kb::script {

struct PucLuaStateLifetime {
    lua_State* state = nullptr;
};

// Retains Lua object identities and detached copies of their mutable values.
// References are released only while the original VM still exists.
class PucLuaStateGraph final {
public:
    explicit PucLuaStateGraph(const std::shared_ptr<PucLuaStateLifetime>& lifetime);
    ~PucLuaStateGraph();
    PucLuaStateGraph(const PucLuaStateGraph&) = delete;
    PucLuaStateGraph& operator=(const PucLuaStateGraph&) = delete;
    [[nodiscard]] bool Capture(std::span<const int> roots, std::string& error);
    [[nodiscard]] bool Validate(std::string& error) const;
    [[nodiscard]] bool Restore(std::string& error);
    [[nodiscard]] std::size_t PayloadBytes() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::script
