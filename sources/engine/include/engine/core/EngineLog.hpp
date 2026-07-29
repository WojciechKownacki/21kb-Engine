#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::core {
enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error };
struct LogField { std::string key; std::string value; };
struct LogRecord { LogLevel level = LogLevel::Info; std::string category; std::string message; std::uint64_t entity = 0U; std::uint64_t world = 0U; std::vector<LogField> fields; };
class EngineLog final { public: explicit EngineLog(std::size_t capacity) : capacity_(capacity) {} [[nodiscard]] bool Write(LogRecord record, std::uint64_t key, std::uint64_t tick) { if(key==lastKey_&&tick==lastTick_)return false; lastKey_=key; lastTick_=tick; if(records_.size()==capacity_)records_.erase(records_.begin()); records_.push_back(std::move(record)); return true; } [[nodiscard]] const std::vector<LogRecord>& Records() const noexcept { return records_; } private: std::size_t capacity_; std::uint64_t lastKey_=0U,lastTick_=0U; std::vector<LogRecord> records_; };
} // namespace kb::core
