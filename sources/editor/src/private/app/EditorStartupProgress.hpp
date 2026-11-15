#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace kb::editor {

// The stages of editor startup, in the order they run. Each has a share of the bar; the shares are
// estimates of relative cost, not measurements.
enum class EditorStartupStage : std::uint8_t {
    Project,
    Modules,
    Assets,
    Scene,
    Interface,
    Renderer,
};

inline constexpr std::size_t kEditorStartupStageCount = 6U;

// How far along startup is, from the stages reached and the items done inside the current one.
// It never goes back, stays below 1 until Finish, and is exactly 1 after it. A stage that reports
// no items ("unknown length") adds its share when the next stage begins.
class EditorStartupProgressModel {
public:
    // Moves on to `stage`; a stage that is not after the current one leaves the bar where it is.
    void Begin(EditorStartupStage stage) noexcept;
    // Items done of the current stage; `total` 0 leaves its length unknown.
    void SetItems(std::size_t done, std::size_t total) noexcept;
    void Finish() noexcept;

    [[nodiscard]] double Fraction() const noexcept;
    [[nodiscard]] bool Finished() const noexcept {
        return finished_;
    }

private:
    void Raise(double value) noexcept;

    std::size_t stage_ = 0U;
    bool started_ = false;
    bool finished_ = false;
    double inner_ = 0.0;
    double fraction_ = 0.0;
};

// What the startup screen shows: the bar and two lines of text already in the editor's language.
struct EditorStartupSnapshot {
    double fraction = 0.0;
    std::string stage;
    std::string detail;
};

// The one interface the startup code reports through. Startup runs on the main thread and so do
// all calls here; the sink (the startup screen) is handed a copy and may live on another thread.
// With no sink - every headless run - the calls only keep the model and cost nothing else.
class EditorStartupProgress {
public:
    EditorStartupProgress() = delete;

    using Sink = std::function<void(const EditorStartupSnapshot&)>;

    // Installs (or, empty, removes) the sink and hands it the current state at once.
    static void SetSink(Sink sink);

    static void Begin(EditorStartupStage stage);
    // The second line under the stage ("Plugins: 4", the scene's name), replacing the last one.
    static void Note(std::string_view detail);
    // Shader binaries loaded so far, `name` being the one just loaded.
    static void Shaders(std::size_t done, std::size_t total, std::string_view name);
    static void Finish();

    [[nodiscard]] static double Fraction();

    // The texts, in the editor's language when its catalog is loaded and in English before.
    [[nodiscard]] static std::string StageText(EditorStartupStage stage);
    [[nodiscard]] static std::string FinishedText();
    [[nodiscard]] static std::string PluginsText(std::size_t count);
    [[nodiscard]] static std::string ShadersText(std::size_t done, std::size_t total, std::string_view name);
};

} // namespace kb::editor
