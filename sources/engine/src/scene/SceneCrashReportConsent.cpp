#include "engine/scene/SceneCrashReportConsent.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <span>
#include <utility>

namespace kb::scene {
namespace {

// Drives the prompt. It runs after the UI update that queues a click and in the same phase, so it sees the
// click before a script system drains the event queue later in the frame.
class CrashReportConsentPromptSystem final : public SceneSystem {
public:
    explicit CrashReportConsentPromptSystem(std::weak_ptr<CrashReportConsentFlow*> flow) noexcept : flow_(std::move(flow)) {}

    void OnUpdate(SceneSystemContext& context) override {
        const std::shared_ptr<CrashReportConsentFlow*> owner = flow_.lock();
        if (owner == nullptr || *owner == nullptr) return;
        (*owner)->UpdatePrompt(context.GetScene());
    }

private:
    std::weak_ptr<CrashReportConsentFlow*> flow_;
};

[[nodiscard]] std::uint64_t FirstFontAsset(Scene& scene) {
    const kb::assets::AssetMetadata* chosen = nullptr;
    for (const kb::assets::AssetMetadata& metadata : scene.Assets().Manager().Registry().All()) {
        if (metadata.importCategory != "Font") continue;
        if (chosen == nullptr || metadata.virtualPath.generic_string() < chosen->virtualPath.generic_string()) chosen = &metadata;
    }
    return chosen == nullptr ? 0U : chosen->id.value;
}

[[nodiscard]] UIRectTransform Rect(kb::math::Vec2 anchorMin, kb::math::Vec2 anchorMax, kb::math::Vec2 offsetMin, kb::math::Vec2 offsetMax) {
    UIRectTransform rect;
    rect.anchorMin = anchorMin;
    rect.anchorMax = anchorMax;
    rect.offsetMin = offsetMin;
    rect.offsetMax = offsetMax;
    return rect;
}

SceneObject AddUIObject(Scene& scene, SceneObject parent, std::string_view name, const UIComponentSet& components) {
    SceneObject object = scene.Entities().CreateObject(SceneObjectDesc{ .name = std::string{ name }, .parent = parent });
    ApplySceneUIComponents(scene.Components().UI(), object.Entity(), components);
    return object;
}

void SetText(UIText& text, std::string_view content, std::uint64_t font, float size) {
    // Longer text than a label holds is cut at the limit rather than dropped.
    std::string_view clipped = content.substr(0U, UIText::MaxUtf8Bytes - 1U);
    while (!clipped.empty() && (static_cast<unsigned char>(clipped.back()) & 0xC0U) == 0x80U) clipped.remove_suffix(1U);
    if (!clipped.empty() && static_cast<unsigned char>(clipped.back()) >= 0xC0U) clipped.remove_suffix(1U);
    static_cast<void>(SetUITextContent(text, clipped));
    text.fontAssetId = font;
    text.fontSize = size;
    text.richText = false;
    text.color = kb::math::Color{ 1.0F, 1.0F, 1.0F, 1.0F };
}

} // namespace

CrashReportConsentFlow::CrashReportConsentFlow(kb::platform::UserStorage& storage, CrashReportConsentHooks hooks)
    : storage_(storage)
    , hooks_(std::move(hooks))
    , self_(std::make_shared<CrashReportConsentFlow*>(this)) {}

CrashReportConsentFlow::~CrashReportConsentFlow() {
    *self_ = nullptr;
    if (promptScene_ != nullptr && system_.IsValid()) {
        static_cast<void>(promptScene_->Runtime().RemoveSceneSystem(system_));
    }
}

kb::platform::CrashUploadConsentChoice CrashReportConsentFlow::StoredChoice() const {
    return kb::platform::ReadCrashUploadConsentChoice(storage_);
}

CrashReportConsentState CrashReportConsentFlow::Begin(Scene& scene, bool uploadConfigured, CrashReportConsentPromptOptions options) {
    uploadConfigured_ = uploadConfigured;
    const kb::platform::CrashUploadConsentChoice stored = StoredChoice();
    // The reporter's own gate follows the stored answer, so an old consent file cannot outlive a no or
    // stand in for a question never asked.
    const bool granted = stored == kb::platform::CrashUploadConsentChoice::Granted;
    if (hooks_.applyConsent) static_cast<void>(hooks_.applyConsent(granted));
    if (!uploadConfigured_) {
        state_ = CrashReportConsentState::NotConfigured;
        return state_;
    }
    if (stored == kb::platform::CrashUploadConsentChoice::Undecided) {
        OpenPrompt(scene, options);
        state_ = CrashReportConsentState::Prompting;
        return state_;
    }
    state_ = granted ? CrashReportConsentState::Granted : CrashReportConsentState::Declined;
    if (granted && hooks_.startUpload) hooks_.startUpload();
    return state_;
}

bool CrashReportConsentFlow::SetConsent(Scene& scene, bool granted) {
    if (!kb::platform::WriteCrashUploadConsentChoice(storage_, granted)) return false;
    ClosePrompt(scene);
    Apply(granted);
    return true;
}

void CrashReportConsentFlow::Answer(Scene& scene, bool granted) {
    if (state_ != CrashReportConsentState::Prompting) return;
    // An answer that cannot be stored would be asked again next launch; the prompt stays until it is kept.
    if (!kb::platform::WriteCrashUploadConsentChoice(storage_, granted)) return;
    ClosePrompt(scene);
    Apply(granted);
}

void CrashReportConsentFlow::UpdatePrompt(Scene& scene) {
    if (state_ != CrashReportConsentState::Prompting) return;
    for (const SceneUIEvent& event : scene.UI().Events()) {
        if (event.type == SceneUIEventType::Clicked && (event.entity == accept_ || event.entity == decline_)) {
            Answer(scene, event.entity == accept_);
            return;
        }
    }
    const kb::platform::CrashUploadConsentChoice stored = StoredChoice();
    if (stored != kb::platform::CrashUploadConsentChoice::Undecided) {
        ClosePrompt(scene);
        Apply(stored == kb::platform::CrashUploadConsentChoice::Granted);
        return;
    }
    // Focus needs the button in a laid-out UI frame, which exists from the prompt's first UI update on.
    if (focusPending_ || !scene.UI().Focused().IsValid()) {
        focusPending_ = !scene.UI().SetFocus(decline_);
    }
}

void CrashReportConsentFlow::Apply(bool granted) {
    if (hooks_.applyConsent) static_cast<void>(hooks_.applyConsent(granted));
    if (!uploadConfigured_) {
        state_ = CrashReportConsentState::NotConfigured;
        return;
    }
    state_ = granted ? CrashReportConsentState::Granted : CrashReportConsentState::Declined;
    if (granted && hooks_.startUpload) hooks_.startUpload();
}

void CrashReportConsentFlow::OpenPrompt(Scene& scene, const CrashReportConsentPromptOptions& options) {
    ClosePrompt(scene);
    const std::uint64_t font = options.fontAssetId != 0U ? options.fontAssetId : FirstFontAsset(scene);

    UIComponentSet canvas = BuildUIComponentPreset(UIComponentPreset::Canvas);
    canvas.canvas->sortingOrder = options.sortingOrder;
    const SceneObject root = AddUIObject(scene, {}, "Crash Report Consent", canvas);

    UIComponentSet backdrop = BuildUIComponentPreset(UIComponentPreset::Border);
    backdrop.rectTransform = Rect({ 0.0F, 0.0F }, { 1.0F, 1.0F }, {}, {});
    backdrop.border->backgroundColor = kb::math::Color{ 0.0F, 0.0F, 0.0F, 0.6F };
    const SceneObject shade = AddUIObject(scene, root, "Backdrop", backdrop);

    UIComponentSet panelComponents = BuildUIComponentPreset(UIComponentPreset::Border);
    panelComponents.rectTransform = Rect({ 0.5F, 0.5F }, { 0.5F, 0.5F }, { -320.0F, -150.0F }, { 320.0F, 150.0F });
    panelComponents.border->backgroundColor = kb::math::Color{ 0.12F, 0.13F, 0.15F, 1.0F };
    const SceneObject panel = AddUIObject(scene, shade, "Panel", panelComponents);

    UIComponentSet title = BuildUIComponentPreset(UIComponentPreset::Text);
    title.rectTransform = Rect({}, {}, { 24.0F, 20.0F }, { 616.0F, 60.0F });
    SetText(*title.text, options.title, font, 26.0F);
    static_cast<void>(AddUIObject(scene, panel, "Title", title));

    UIComponentSet body = BuildUIComponentPreset(UIComponentPreset::Text);
    body.rectTransform = Rect({}, {}, { 24.0F, 72.0F }, { 616.0F, 220.0F });
    SetText(*body.text, options.message, font, 17.0F);
    body.text->wrapMode = UITextWrapMode::Word;
    static_cast<void>(AddUIObject(scene, panel, "Message", body));

    UIComponentSet declineButton = BuildUIComponentPreset(UIComponentPreset::Button);
    declineButton.rectTransform = Rect({}, {}, { 24.0F, 236.0F }, { 308.0F, 280.0F });
    SetText(*declineButton.text, options.declineLabel, font, 18.0F);
    declineButton.text->horizontalAlignment = UITextHorizontalAlignment::Center;
    declineButton.text->verticalAlignment = UITextVerticalAlignment::Center;
    const SceneObject decline = AddUIObject(scene, panel, "Decline", declineButton);

    UIComponentSet acceptButton = BuildUIComponentPreset(UIComponentPreset::Button);
    acceptButton.rectTransform = Rect({}, {}, { 332.0F, 236.0F }, { 616.0F, 280.0F });
    SetText(*acceptButton.text, options.acceptLabel, font, 18.0F);
    acceptButton.text->horizontalAlignment = UITextHorizontalAlignment::Center;
    acceptButton.text->verticalAlignment = UITextVerticalAlignment::Center;
    const SceneObject accept = AddUIObject(scene, panel, "Accept", acceptButton);

    root_ = root.Entity();
    accept_ = accept.Entity();
    decline_ = decline.Entity();
    // Keyboard and controller players start on the answer that sends nothing.
    focusPending_ = true;
    promptScene_ = &scene;
    system_ = scene.Runtime().AddSceneSystem(std::make_unique<CrashReportConsentPromptSystem>(self_));
}

void CrashReportConsentFlow::ClosePrompt(Scene& scene) {
    if (root_.IsValid() && scene.Entities().IsAlive(root_)) scene.Entities().Destroy(root_);
    root_ = {};
    accept_ = {};
    decline_ = {};
    focusPending_ = false;
    // The system stays registered until the flow goes away: it may be the one calling, and it does nothing
    // once no prompt is open.
}

} // namespace kb::scene
