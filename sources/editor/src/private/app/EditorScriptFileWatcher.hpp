#pragma once

#include <filesystem>
#include <optional>
#include <system_error>

namespace kb::editor {

class EditorScriptFileWatcher {
public:
    void Track(const std::filesystem::path& path) {
        if (path_ == path) {
            return;
        }
        path_ = path;
        Acknowledge();
    }

    [[nodiscard]] bool HasChangedOnDisk() const {
        if (path_.empty()) {
            return false;
        }
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(path_, error);
        return !error && (!lastObservedWriteTime_.has_value() || writeTime != *lastObservedWriteTime_);
    }

    void Acknowledge() {
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(path_, error);
        lastObservedWriteTime_ = error ? std::nullopt
            : std::optional<std::filesystem::file_time_type>{ writeTime };
    }

private:
    std::filesystem::path path_;
    std::optional<std::filesystem::file_time_type> lastObservedWriteTime_;
};

} // namespace kb::editor
