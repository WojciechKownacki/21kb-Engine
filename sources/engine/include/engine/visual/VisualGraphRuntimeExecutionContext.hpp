#pragma once

#include "engine/visual/VisualGraphRuntimeValue.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::visual {

class VisualGraphRuntimeExecutionContext final {
public:
    void Store(std::uint32_t nodeId, std::string_view pin, VisualGraphRuntimeValue value);
    [[nodiscard]] const VisualGraphRuntimeValue* TryRead(std::uint32_t nodeId, std::string_view pin) const;
    [[nodiscard]] bool WasStoredInCurrentExecution(std::uint32_t nodeId, std::string_view pin) const;

    [[nodiscard]] bool ReadBool(std::uint32_t nodeId, std::string_view pin, bool fallback = false) const;
    [[nodiscard]] int ReadInt(std::uint32_t nodeId, std::string_view pin, int fallback = 0) const;
    [[nodiscard]] float ReadFloat(std::uint32_t nodeId, std::string_view pin, float fallback = 0.0F) const;
    [[nodiscard]] std::string ReadString(std::uint32_t nodeId, std::string_view pin) const;
    [[nodiscard]] std::uint64_t ReadUInt64(std::uint32_t nodeId, std::string_view pin, std::uint64_t fallback = 0U) const;

    void EmitEvent(std::string eventName);
    void Trace(std::string message);

    [[nodiscard]] const std::vector<std::string>& EmittedEvents() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Traces() const noexcept;
    void BeginExecutionPass();
    void ClearFrameState();

private:
    [[nodiscard]] static std::string Key(std::uint32_t nodeId, std::string_view pin);

    std::unordered_map<std::string, VisualGraphRuntimeValue> values_;
    std::unordered_set<std::string> writesInCurrentExecution_;
    std::vector<std::string> emittedEvents_;
    std::vector<std::string> traces_;
};

} // namespace kb::visual
