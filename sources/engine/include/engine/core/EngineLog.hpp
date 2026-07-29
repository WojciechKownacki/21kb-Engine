#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <variant>

namespace kb::core {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error };

using LogFieldValue = std::variant<bool, std::int64_t, std::uint64_t, double,
    std::string>;

struct LogField {
    std::string key;
    LogFieldValue value;
};

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    std::uint64_t entity = 0U;
    std::uint64_t world = 0U;
    std::vector<LogField> fields;
};

class EngineLog final {
public:
    explicit EngineLog(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] bool Trace(LogRecord record, std::uint64_t key,
        std::uint64_t tick) {
        record.level = LogLevel::Trace;
        return Write(std::move(record), key, tick);
    }

    [[nodiscard]] bool Debug(LogRecord record, std::uint64_t key,
        std::uint64_t tick) {
        record.level = LogLevel::Debug;
        return Write(std::move(record), key, tick);
    }

    [[nodiscard]] bool Info(LogRecord record, std::uint64_t key,
        std::uint64_t tick) {
        record.level = LogLevel::Info;
        return Write(std::move(record), key, tick);
    }

    [[nodiscard]] bool Warn(LogRecord record, std::uint64_t key,
        std::uint64_t tick) {
        record.level = LogLevel::Warn;
        return Write(std::move(record), key, tick);
    }

    [[nodiscard]] bool Error(LogRecord record, std::uint64_t key,
        std::uint64_t tick) {
        record.level = LogLevel::Error;
        return Write(std::move(record), key, tick);
    }

    [[nodiscard]] bool Write(LogRecord record, std::uint64_t key,
        std::uint64_t tick) {
        if (capacity_ == 0U) return false;
        if (!rateLimitTick_.has_value() || *rateLimitTick_ != tick) {
            rateLimitTick_ = tick;
            rateLimitKeys_.clear();
        }
        if (std::find(rateLimitKeys_.begin(), rateLimitKeys_.end(), key) !=
            rateLimitKeys_.end()) {
            return false;
        }
        if (rateLimitKeys_.size() < capacity_) rateLimitKeys_.push_back(key);
        if (records_.size() == capacity_) records_.erase(records_.begin());
        records_.push_back(std::move(record));
        return true;
    }

    [[nodiscard]] const std::vector<LogRecord>& Records() const noexcept {
        return records_;
    }

private:
    std::size_t capacity_;
    std::optional<std::uint64_t> rateLimitTick_;
    std::vector<std::uint64_t> rateLimitKeys_;
    std::vector<LogRecord> records_;
};

} // namespace kb::core
