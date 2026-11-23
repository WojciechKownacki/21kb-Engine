#include "app/EditorAgentControl.hpp"

#include "app/EditorApplicationState.hpp"
#include "app/EditorPlayCommands.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "project/EditorProjectPaths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

void Reply(const std::string& stamp, bool ok, const std::string& text) {
    const fs::path file = EditorProjectPaths::ProjectRoot() / "Saved" / "Agent" / "EditorReply" / (stamp + ".txt");
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    const fs::path partial = fs::path{ file }.concat(".partial");
    {
        std::ofstream output{ partial, std::ios::binary | std::ios::trunc };
        output << (ok ? "ok\n" : "failed\n") << text << '\n';
    }
    fs::rename(partial, file, error);
}

struct PendingCapture {
    std::string stamp;
    std::string path;
    std::uint64_t id = 0U;
    Clock::time_point started;
};

struct PendingMeasure {
    std::string stamp;
    Clock::time_point started;
    std::chrono::seconds length{ 0 };
    std::vector<double> frames;
};

std::optional<PendingCapture> g_capture;
std::optional<PendingMeasure> g_measure;

constexpr std::chrono::seconds kCaptureLimit{ 15 };

void Invalidate(EditorApplicationState& state) {
    state.sceneViewport.RequestPresent();
    InvalidateRect(state.window, nullptr, FALSE);
}

std::string Number(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", value);
    return text;
}

} // namespace

bool IsAgentControlAction(std::string_view action) noexcept {
    return action == "play" || action == "stop" || action == "capture" || action == "frames";
}

void StartAgentControl(EditorApplicationState& state, const kb::core::JsonValue& request) {
    const auto text = [&request](std::string_view name) {
        const kb::core::JsonValue* value = request.Find(name);
        return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String ? value->AsString() : std::string{};
    };
    const std::string stamp = text("stamp");
    const std::string action = text("action");
    if (stamp.empty()) return;
    if (action == "play") {
        if (state.playMode.Mode() != EditorPlayMode::Stopped) {
            Reply(stamp, true, "The game is already playing.");
        } else if (EditorPlayCommands::Start(state.window, state.dockModel, state.sceneContext, state.playMode)) {
            Invalidate(state);
            Reply(stamp, true, "The game is playing in the editor.");
        } else {
            Reply(stamp, false, "The editor did not start the game: its Play target may be the game's own window, or the scene could not start (its Console says why).");
        }
    } else if (action == "stop") {
        if (state.playMode.Mode() == EditorPlayMode::Stopped) {
            Reply(stamp, true, "The game was not playing.");
        } else {
            state.playMode.Stop();
            static_cast<void>(state.sceneContext.RestorePlayModeSceneSession());
            EditorPlayCommands::RestoreWorkspace(state.dockModel);
            Invalidate(state);
            Reply(stamp, true, "The game stopped.");
        }
    } else if (action == "capture") {
        if (g_capture.has_value()) {
            Reply(stamp, false, "Another picture of the editor is still being taken; ask again in a moment.");
            return;
        }
        const std::string path = text("path");
        std::error_code error;
        fs::create_directories(fs::path{ std::u8string{ reinterpret_cast<const char8_t*>(path.data()), path.size() } }.parent_path(), error);
        const std::uint64_t id = kb::scene::SceneRenderFeedback::RequestScreenCapture(state.sceneContext.Scene(), path);
        if (id == 0U) {
            Reply(stamp, false, "The editor could not start the picture: the scene has a capture pending already.");
            return;
        }
        g_capture = PendingCapture{ stamp, path, id, Clock::now() };
        Invalidate(state);
    } else if (action == "frames") {
        if (g_measure.has_value()) {
            Reply(stamp, false, "A frame measurement is still running; ask again when it has ended.");
            return;
        }
        const kb::core::JsonValue* seconds = request.Find("seconds");
        g_measure = PendingMeasure{ stamp, Clock::now(), std::chrono::seconds{ seconds != nullptr ? static_cast<long long>(seconds->AsNumber(3.0)) : 3LL }, {} };
    }
}

void PumpAgentControl(EditorApplicationState& state, double frameMilliseconds) {
    if (g_capture.has_value()) {
        const kb::scene::SceneScreenCaptureStatus status = kb::scene::SceneRenderFeedback::ScreenCaptureStatus(state.sceneContext.Scene(), g_capture->id);
        if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
            Reply(g_capture->stamp, true, "The editor's view is in " + g_capture->path + " (open it with Read to look at it).");
            g_capture.reset();
        } else if (status == kb::scene::SceneScreenCaptureStatus::Failed || Clock::now() - g_capture->started > kCaptureLimit) {
            Reply(g_capture->stamp, false, status == kb::scene::SceneScreenCaptureStatus::Failed
                ? "The renderer could not read the view back." : "The view was not rendered within 15 seconds; the editor may be minimized.");
            g_capture.reset();
        } else {
            Invalidate(state);
        }
    }
    if (g_measure.has_value()) {
        g_measure->frames.push_back(frameMilliseconds);
        if (Clock::now() - g_measure->started >= g_measure->length) {
            std::vector<double> sorted = g_measure->frames;
            std::ranges::sort(sorted);
            double total = 0.0;
            for (const double frame : sorted) total += frame;
            const auto at = [&sorted](double fraction) { return sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1U))]; };
            const double mean = total / static_cast<double>(sorted.size());
            Reply(g_measure->stamp, true, std::to_string(sorted.size()) + " frames in " + std::to_string(g_measure->length.count()) + " s, "
                + (state.playMode.IsPlaying() ? "the game playing" : "the game not playing (the editor may draw only when something changes)")
                + ": " + Number(1000.0 / mean) + " fps, frame time mean " + Number(mean) + " ms, median " + Number(at(0.5)) + " ms, 95th percentile "
                + Number(at(0.95)) + " ms, slowest " + Number(sorted.back()) + " ms.");
            g_measure.reset();
        }
    }
}

} // namespace kb::editor
