#pragma once

#include "engine/platform/CrashReportConsent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneSystemHandle.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace kb::platform {
class UserStorage;
}

namespace kb::scene {

class Scene;

// What the game host connects the consent flow to. Both are optional; the flow never calls `startUpload`
// unless the player's stored answer is yes.
struct CrashReportConsentHooks {
    // Mirrors an answer into the crash reporter's own upload gate.
    std::function<bool(bool granted)> applyConsent;
    // Starts sending the reports earlier runs left behind.
    std::function<void()> startUpload;
};

struct CrashReportConsentPromptOptions {
    // 0 picks the font asset with the first virtual path among the scene's registered fonts.
    std::uint64_t fontAssetId = 0U;
    std::string title = "Send crash reports?";
    std::string message =
        "If the game crashes, it can send a crash report to its developers so they can fix the problem. "
        "A report holds the state of the game at the moment it crashed and basic details of this computer. "
        "Nothing is sent unless you agree. You can change this later in the game's settings.";
    std::string acceptLabel = "Send reports";
    std::string declineLabel = "Don't send";
    // Drawn above every canvas the game authors.
    std::int32_t sortingOrder = 30000;
};

enum class CrashReportConsentState : std::uint8_t {
    // The game sends no crash reports, so there is nothing to ask.
    NotConfigured,
    // The prompt is on screen and nothing has been sent.
    Prompting,
    Granted,
    Declined,
};

// The in-game "send crash reports?" question of a packaged game. On the first launch of a game with crash
// upload configured it shows a prompt built from ordinary scene UI (a canvas above the game's own, a
// panel, the question and two buttons, the decline button focused for keyboard and controller). The
// answer is stored in the game's user storage, mirrored into the crash reporter's gate, and only a yes
// lets earlier reports be sent. Later launches use the stored answer without asking; SetConsent changes it.
// The flow must be destroyed before the scene it prompted in.
class CrashReportConsentFlow final {
public:
    CrashReportConsentFlow(kb::platform::UserStorage& storage, CrashReportConsentHooks hooks);
    ~CrashReportConsentFlow();
    CrashReportConsentFlow(const CrashReportConsentFlow&) = delete;
    CrashReportConsentFlow& operator=(const CrashReportConsentFlow&) = delete;

    // Once, after the game's first scene is loaded. `uploadConfigured` is whether the game names a crash
    // report endpoint; without one the stored answer is still mirrored, but no prompt is shown.
    CrashReportConsentState Begin(Scene& scene, bool uploadConfigured, CrashReportConsentPromptOptions options = {});
    // Changes the answer, e.g. from a settings screen; closes the prompt if it is still open.
    [[nodiscard]] bool SetConsent(Scene& scene, bool granted);
    // The player's answer from the prompt: stored, mirrored, and with a yes the upload starts.
    void Answer(Scene& scene, bool granted);
    // Run by the prompt's scene system every frame while the prompt is open, after the UI update: takes a
    // press of either button, keeps keyboard and controller focus on the prompt, and adopts an answer
    // stored meanwhile by other means (a settings script).
    void UpdatePrompt(Scene& scene);

    [[nodiscard]] CrashReportConsentState State() const noexcept { return state_; }
    [[nodiscard]] kb::platform::CrashUploadConsentChoice StoredChoice() const;
    [[nodiscard]] SceneEntity PromptRoot() const noexcept { return root_; }
    [[nodiscard]] SceneEntity AcceptButton() const noexcept { return accept_; }
    [[nodiscard]] SceneEntity DeclineButton() const noexcept { return decline_; }

private:
    void OpenPrompt(Scene& scene, const CrashReportConsentPromptOptions& options);
    void ClosePrompt(Scene& scene);
    void Apply(bool granted);

    kb::platform::UserStorage& storage_;
    CrashReportConsentHooks hooks_;
    CrashReportConsentState state_ = CrashReportConsentState::NotConfigured;
    bool uploadConfigured_ = false;
    Scene* promptScene_ = nullptr;
    SceneSystemHandle system_{};
    SceneEntity root_{};
    SceneEntity accept_{};
    SceneEntity decline_{};
    bool focusPending_ = false;
    std::shared_ptr<CrashReportConsentFlow*> self_;
};

} // namespace kb::scene
