#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>

namespace kb::editor::diagnostics {

class EditorLagTrace final {
public:
    [[nodiscard]] static std::uint64_t NextEventId() noexcept {
        if (!Enabled()) return 0U;
        return Instance().nextEventId_.fetch_add(1U, std::memory_order_relaxed);
    }

    static void Input(
        std::uint64_t eventId,
        std::string_view message,
        std::uintptr_t window,
        std::int64_t x,
        std::int64_t y) noexcept {
        if (!Enabled()) return;
        EditorLagTrace& trace = Instance();
        std::ostringstream detail;
        detail << "message=" << message << " hwnd=0x" << std::hex << window << std::dec
               << " x=" << x << " y=" << y;
        trace.Write("input", eventId, 0.0, detail.str(), false);
    }

    static void Slow(
        std::string_view category,
        std::uint64_t eventId,
        double durationMs,
        std::string_view detail,
        double thresholdMs = 8.0) noexcept {
        if (durationMs < thresholdMs) {
            return;
        }
        if (!Enabled()) return;
        // Slow-path telemetry must never turn a late frame into a chain of late
        // frames. Keep it buffered; explicit markers still flush transition data.
        Instance().Write(category, eventId, durationMs, detail, false);
    }

    static void Marker(std::string_view category, std::string_view detail) noexcept {
        if (!Enabled()) return;
        Instance().Write(category, NextEventId(), 0.0, detail, true);
    }

private:
    [[nodiscard]] static bool Enabled() noexcept {
        static const bool enabled = [] {
#if defined(_MSC_VER)
            char* value = nullptr;
            std::size_t length = 0U;
            if (_dupenv_s(&value, &length, "KB_EDITOR_LAG_TRACE") != 0) return false;
            const bool explicitlyEnabled = value != nullptr && length == 2U && value[0] == '1';
            const bool explicitlyDisabled = value != nullptr && length == 2U && value[0] == '0';
            std::free(value);
            if (explicitlyEnabled) return true;
            if (explicitlyDisabled) return false;
#else
            const char* value = std::getenv("KB_EDITOR_LAG_TRACE");
            if (value != nullptr && value[0] == '1' && value[1] == '\0') return true;
            if (value != nullptr && value[0] == '0' && value[1] == '\0') return false;
#endif
#if !defined(NDEBUG)
            // Debug editor builds are the diagnostic product. Slow-only records stay enabled so a
            // repro remains useful even when the launcher did not propagate an environment variable.
            return true;
#else
            return false;
#endif
        }();
        return enabled;
    }

    EditorLagTrace() noexcept
        : started_(std::chrono::steady_clock::now()) {
        std::error_code error;
        path_ = std::filesystem::current_path(error) / "Saved" / "Logs" / "editor-lag-trace.log";
        if (error) {
            return;
        }
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error) {
            return;
        }
        stream_.open(path_, std::ios::out | std::ios::app);
        if (stream_.is_open()) {
            const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            stream_ << "\n=== editor lag trace session epoch_ms=" << epochMs
                    << " cwd=" << std::filesystem::current_path(error).generic_string() << " ===\n";
            stream_.flush();
        }
    }

    [[nodiscard]] static EditorLagTrace& Instance() noexcept {
        static EditorLagTrace trace;
        return trace;
    }

    void Write(
        std::string_view category,
        std::uint64_t eventId,
        double durationMs,
        std::string_view detail,
        bool flush) noexcept {
        if (!stream_.is_open()) {
            return;
        }
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_).count();
        const std::size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        std::scoped_lock lock(mutex_);
        stream_ << "t=" << elapsedMs << "ms thread=" << threadId
                << " event=" << eventId << " category=" << category;
        if (durationMs > 0.0) {
            stream_ << " duration=" << durationMs << "ms";
        }
        if (!detail.empty()) {
            stream_ << ' ' << detail;
        }
        stream_ << '\n';
        if (flush) {
            stream_.flush();
        }
    }

    std::chrono::steady_clock::time_point started_{};
    std::filesystem::path path_{};
    std::ofstream stream_{};
    std::mutex mutex_{};
    std::atomic<std::uint64_t> nextEventId_{1U};
};

} // namespace kb::editor::diagnostics
