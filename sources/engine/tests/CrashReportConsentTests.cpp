#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/platform/CrashReportConsent.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneCrashReportConsent.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/UIComponentSet.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::tests {
namespace {

constexpr float kWidth = 1280.0F;
constexpr float kHeight = 720.0F;
constexpr float kFrame = 1.0F / 60.0F;

[[nodiscard]] std::filesystem::path ConsentRoot(std::string_view name) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-crash-consent-tests" / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

// What the game host would do with the flow's decisions, recorded instead of touching the crash reporter.
struct RecordedHooks {
    std::vector<bool> applied;
    int uploads = 0;

    [[nodiscard]] kb::scene::CrashReportConsentHooks Hooks() {
        return kb::scene::CrashReportConsentHooks{
            .applyConsent = [this](bool granted) { applied.push_back(granted); return true; },
            .startUpload = [this] { ++uploads; },
        };
    }
};

// One game frame: the UI sees the player's input, then the scene runs its systems, the prompt's among them.
void Frame(kb::scene::Scene& scene, const kb::scene::SceneUIInput& input = {}) {
    static_cast<void>(scene.UI().Update(kWidth, kHeight, input, kFrame));
    static_cast<void>(scene.Runtime().Update(kFrame));
}

void Click(kb::scene::Scene& scene, kb::scene::SceneEntity button) {
    const kb::scene::SceneUIFrame& frame = scene.UI().Frame();
    const auto element = std::ranges::find(frame.elements, button, &kb::scene::SceneUIFrameElement::entity);
    Require(element != frame.elements.end(), "The consent prompt's button is not laid out on screen");
    kb::scene::SceneUIInput input;
    input.pointerAvailable = true;
    input.pointerPosition = { element->rect.x + element->rect.width * 0.5F, element->rect.y + element->rect.height * 0.5F };
    Require(scene.UI().HitTest(input.pointerPosition) == button, "The consent prompt's button is covered by something else");
    Frame(scene, input);
    input.primaryDown = true;
    Frame(scene, input);
    input.primaryDown = false;
    Frame(scene, input);
}

void TestConsentChoiceStorage() {
    kb::platform::UserStorage storage{ ConsentRoot("storage"), 1U << 20U };
    using kb::platform::CrashUploadConsentChoice;
    Require(kb::platform::ReadCrashUploadConsentChoice(storage) == CrashUploadConsentChoice::Undecided,
        "Crash upload consent must start undecided");
    Require(kb::platform::WriteCrashUploadConsentChoice(storage, true) &&
            kb::platform::ReadCrashUploadConsentChoice(storage) == CrashUploadConsentChoice::Granted,
        "A yes must be stored in the game's user storage");
    Require(kb::platform::WriteCrashUploadConsentChoice(storage, false) &&
            kb::platform::ReadCrashUploadConsentChoice(storage) == CrashUploadConsentChoice::Declined,
        "A no must replace a stored yes");
    Require(storage.Write(kb::platform::kCrashUploadConsentStorageKey, "upload=YES") &&
            kb::platform::ReadCrashUploadConsentChoice(storage) == CrashUploadConsentChoice::Undecided,
        "A stored value this code did not write must not count as consent");
    Require(kb::platform::ClearCrashUploadConsentChoice(storage) &&
            kb::platform::ReadCrashUploadConsentChoice(storage) == CrashUploadConsentChoice::Undecided &&
            kb::platform::ClearCrashUploadConsentChoice(storage),
        "Clearing the answer must make the next launch ask again");
}

void TestFirstLaunchAsksAndYesStartsUpload() {
    const std::filesystem::path root = ConsentRoot("accept");
    kb::platform::UserStorage storage{ root, 1U << 20U };
    RecordedHooks hooks;
    {
        kb::scene::Scene scene;
        kb::scene::CrashReportConsentFlow flow{ storage, hooks.Hooks() };
        Require(flow.Begin(scene, true) == kb::scene::CrashReportConsentState::Prompting,
            "The first launch of a game with crash upload must ask the player");
        Require(hooks.applied == std::vector<bool>{ false } && hooks.uploads == 0,
            "Before the player answers, the reporter's gate must be closed and nothing may be sent");
        Require(scene.Entities().IsAlive(flow.PromptRoot()) && scene.Entities().IsAlive(flow.AcceptButton()) &&
                scene.Entities().IsAlive(flow.DeclineButton()),
            "The consent prompt must be built from scene UI");
        const kb::scene::UICanvas* canvas = scene.Components().UI().TryGet<kb::scene::UICanvas>(flow.PromptRoot());
        Require(canvas != nullptr && canvas->sortingOrder >= 30000 &&
                scene.Components().UI().Has<kb::scene::UIButton>(flow.AcceptButton()) &&
                scene.Components().UI().Has<kb::scene::UIButton>(flow.DeclineButton()),
            "The consent prompt must be a top-most canvas with two buttons");
        for (int frame = 0; frame < 5; ++frame) Frame(scene);
        Require(flow.State() == kb::scene::CrashReportConsentState::Prompting && hooks.uploads == 0 &&
                flow.StoredChoice() == kb::platform::CrashUploadConsentChoice::Undecided,
            "An unanswered prompt must stay open without storing or sending anything");
        Require(scene.UI().Focused() == flow.DeclineButton(),
            "Keyboard and controller players must start on the answer that sends nothing");

        Click(scene, flow.AcceptButton());
        Require(flow.State() == kb::scene::CrashReportConsentState::Granted &&
                flow.StoredChoice() == kb::platform::CrashUploadConsentChoice::Granted,
            "Pressing the accept button must store a yes");
        Require(hooks.applied.back() && hooks.uploads == 1, "A yes must open the reporter's gate and start the upload");
        Require(!flow.PromptRoot().IsValid() && scene.Entities().Count() == 0U,
            "Answering must remove every object of the prompt");
        for (int frame = 0; frame < 3; ++frame) Frame(scene);
        Require(hooks.uploads == 1, "The upload must start once per answer");
    }
    {
        // The next launch remembers the answer.
        kb::scene::Scene scene;
        RecordedHooks relaunch;
        kb::scene::CrashReportConsentFlow flow{ storage, relaunch.Hooks() };
        Require(flow.Begin(scene, true) == kb::scene::CrashReportConsentState::Granted && !flow.PromptRoot().IsValid() &&
                scene.Entities().Count() == 0U,
            "A stored yes must not ask again");
        Require(relaunch.applied == std::vector<bool>{ true } && relaunch.uploads == 1,
            "A stored yes must open the gate and send earlier reports at launch");
    }
}

void TestNoKeepsEverythingLocal() {
    kb::platform::UserStorage storage{ ConsentRoot("decline"), 1U << 20U };
    RecordedHooks hooks;
    {
        kb::scene::Scene scene;
        kb::scene::CrashReportConsentFlow flow{ storage, hooks.Hooks() };
        Require(flow.Begin(scene, true) == kb::scene::CrashReportConsentState::Prompting, "The first launch must ask");
        Frame(scene);
        // Keyboard: the focused decline button is submitted.
        kb::scene::SceneUIInput input;
        input.submitDown = true;
        Frame(scene, input);
        input.submitDown = false;
        Frame(scene, input);
        Require(flow.State() == kb::scene::CrashReportConsentState::Declined &&
                flow.StoredChoice() == kb::platform::CrashUploadConsentChoice::Declined,
            "Submitting the focused decline button must store a no");
        Require(hooks.uploads == 0 && std::ranges::none_of(hooks.applied, [](bool granted) { return granted; }),
            "A no must never open the reporter's gate or send anything");
    }
    {
        kb::scene::Scene scene;
        RecordedHooks relaunch;
        kb::scene::CrashReportConsentFlow flow{ storage, relaunch.Hooks() };
        Require(flow.Begin(scene, true) == kb::scene::CrashReportConsentState::Declined && scene.Entities().Count() == 0U &&
                relaunch.uploads == 0 && relaunch.applied == std::vector<bool>{ false },
            "A stored no must neither ask again nor send");

        // Changed later from a settings screen.
        Require(flow.SetConsent(scene, true) && flow.State() == kb::scene::CrashReportConsentState::Granted &&
                relaunch.uploads == 1 && relaunch.applied.back(),
            "Turning consent on later must store it and start the upload");
        Require(flow.SetConsent(scene, false) && flow.State() == kb::scene::CrashReportConsentState::Declined &&
                !relaunch.applied.back() && flow.StoredChoice() == kb::platform::CrashUploadConsentChoice::Declined,
            "Turning consent off later must store it and close the gate");
    }
}

void TestAnswerStoredElsewhereClosesPrompt() {
    const std::filesystem::path root = ConsentRoot("script");
    kb::platform::UserStorage storage{ root, 1U << 20U };
    RecordedHooks hooks;
    kb::scene::Scene scene;
    kb::scene::CrashReportConsentFlow flow{ storage, hooks.Hooks() };
    Require(flow.Begin(scene, true) == kb::scene::CrashReportConsentState::Prompting, "The first launch must ask");
    Frame(scene);
    // A settings script answered through Settings.SetCrashReportUploadConsent, which writes the same storage.
    kb::platform::UserStorage scriptStorage{ root, 1U << 20U };
    Require(kb::platform::WriteCrashUploadConsentChoice(scriptStorage, false), "The script's storage could not record a no");
    Frame(scene);
    Require(flow.State() == kb::scene::CrashReportConsentState::Declined && scene.Entities().Count() == 0U && hooks.uploads == 0,
        "An answer stored by a settings script must close the prompt and take effect");
}

void TestWithoutUploadNothingIsAsked() {
    kb::platform::UserStorage storage{ ConsentRoot("unconfigured"), 1U << 20U };
    RecordedHooks hooks;
    kb::scene::Scene scene;
    kb::scene::CrashReportConsentFlow flow{ storage, hooks.Hooks() };
    Require(flow.Begin(scene, false) == kb::scene::CrashReportConsentState::NotConfigured && scene.Entities().Count() == 0U &&
            hooks.uploads == 0 && hooks.applied == std::vector<bool>{ false },
        "A game without crash upload must not ask, and must not leave an old consent open");
    Require(kb::platform::WriteCrashUploadConsentChoice(storage, true), "Consent could not be stored");
    kb::scene::Scene relaunch;
    RecordedHooks relaunchHooks;
    kb::scene::CrashReportConsentFlow relaunchFlow{ storage, relaunchHooks.Hooks() };
    Require(relaunchFlow.Begin(relaunch, false) == kb::scene::CrashReportConsentState::NotConfigured &&
            relaunchHooks.uploads == 0 && relaunchHooks.applied == std::vector<bool>{ true },
        "Without an endpoint a stored yes is mirrored but nothing is sent");
}

} // namespace

void RunCrashReportConsentTests() {
    TestConsentChoiceStorage();
    TestFirstLaunchAsksAndYesStartsUpload();
    TestNoKeepsEverythingLocal();
    TestAnswerStoredElsewhereClosesPrompt();
    TestWithoutUploadNothingIsAsked();
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "21kb-crash-consent-tests");
}

} // namespace kb::tests
