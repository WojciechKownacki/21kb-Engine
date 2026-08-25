#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace kb::editor {

struct EditorAutosaveTickResult {
    bool saveRequested = false;
    bool visualChanged = false;
};

class EditorAutosaveState final {
public:
    static constexpr double IntervalSeconds = 10.0 * 60.0;
    static constexpr double NotificationSeconds = 4.0;

    [[nodiscard]] EditorAutosaveTickResult Tick(
        double elapsedSeconds,
        bool saveEligible,
        bool hasDirtyDocument) noexcept {
        EditorAutosaveTickResult result{};
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0) {
            return result;
        }

        if (notificationSecondsRemaining_ > 0.0) {
            notificationSecondsRemaining_ =
                std::max(0.0, notificationSecondsRemaining_ - elapsedSeconds);
            if (notificationSecondsRemaining_ == 0.0) {
                notificationText_.clear();
                result.visualChanged = true;
            }
        }

        elapsedSinceSave_ = std::min(
            IntervalSeconds,
            elapsedSinceSave_ + elapsedSeconds);
        if (elapsedSinceSave_ < IntervalSeconds || !saveEligible) {
            return result;
        }

        elapsedSinceSave_ = 0.0;
        result.saveRequested = hasDirtyDocument;
        return result;
    }

    void ResetInterval() noexcept {
        elapsedSinceSave_ = 0.0;
    }

    void Complete(bool succeeded, std::string documentName) {
        notificationText_ = succeeded
            ? "Autosaved " + std::move(documentName)
            : "Autosave failed";
        notificationSucceeded_ = succeeded;
        notificationSecondsRemaining_ = NotificationSeconds;
    }

    [[nodiscard]] bool NotificationVisible() const noexcept {
        return notificationSecondsRemaining_ > 0.0 && !notificationText_.empty();
    }

    [[nodiscard]] bool NotificationSucceeded() const noexcept {
        return notificationSucceeded_;
    }

    [[nodiscard]] const std::string& NotificationText() const noexcept {
        return notificationText_;
    }

    [[nodiscard]] double ElapsedSinceSave() const noexcept {
        return elapsedSinceSave_;
    }

private:
    double elapsedSinceSave_ = 0.0;
    double notificationSecondsRemaining_ = 0.0;
    std::string notificationText_;
    bool notificationSucceeded_ = true;
};

} // namespace kb::editor
