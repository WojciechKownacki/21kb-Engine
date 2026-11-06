#include "WebTextClipboard.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        using namespace kb::scene;
        kb::input::InputDeviceState committed;
        const std::string longCommit = std::string(63U, 'x') + "e\xCC\x81\xF0\x9F\x98\x80";
        Require(kb::game::AppendWebCommittedText(committed, longCommit) && committed.TextInput().size() == 66U &&
            committed.TextInput()[64U] == U'\u0301' && committed.TextInput()[65U] == U'\U0001F600',
            "Long IME transactions must preserve combining marks and supplementary characters");
        committed.ClearTextInput();
        Require(kb::game::AppendWebCommittedText(committed,
            std::string(kb::input::InputDeviceState::kMaxTextInputCodePoints - 1U, 'x')),
            "Text capacity fixture failed");
        Require(!kb::game::AppendWebCommittedText(committed, "e\xCC\x81") &&
            committed.TextInput().size() == kb::input::InputDeviceState::kMaxTextInputCodePoints - 1U,
            "A full input buffer must reject an entire grapheme without appending its base");
        Require(!kb::game::AppendWebCommittedText(committed, "\xED\xA0\x80"),
            "Malformed committed text must not enter the input snapshot");
        Scene scene{SceneMode::PrefabPrivate};
        const auto canvas = scene.Entities().CreateObject();
        ApplySceneUIComponents(scene.Components().UI(), canvas.Entity(), BuildUIComponentPreset(UIComponentPreset::Canvas));
        const auto field = scene.Entities().CreateObject(SceneObjectDesc{.parent = canvas});
        auto components = BuildUIComponentPreset(UIComponentPreset::InputField);
        components.rectTransform->offsetMax = {200.0F, 80.0F};
        components.inputField->multiline = true;
        ApplySceneUIComponents(scene.Components().UI(), field.Entity(), components);
        auto ui = scene.UI();
        const auto tick = [&] { static_cast<void>(ui.Update(640.0F, 480.0F, {}, .016F)); };
        const auto content = [&] { return UITextContent(*scene.Components().UI().TryGet<UIText>(field.Entity())); };
        const auto paste = [&](std::string_view text) { return kb::game::QueueWebClipboardText(ui, field.Entity(), text); };
        const auto copy = [&] { return kb::game::ReadWebClipboardSelection(ui, field.Entity()); };
        const auto selectAll = [&] {
            auto state = ui.TextInputState();
            Require(state.has_value(), "Focused field snapshot missing");
            state->selectionAnchor = 0U;
            state->caret = state->text.size();
            Require(ui.QueueTextInputState(*state), "Selection rejected");
            tick();
        };
        tick();
        Require(ui.SetFocus(field.Entity()), "Field focus failed");
        const std::string text = "A\xC5\xBC\xF0\x9F\x98\x80\n";
        Require(paste(text), "Unicode paste rejected");
        tick();
        Require(content() == text, "Multiline Unicode paste changed bytes");
        selectAll();
        Require(copy() == text, "Selected Unicode failed round-trip");
        Require(!paste("\xED\xA0\x80") && !paste(std::string("a\0b", 3U)) &&
            !paste(std::string(16385U, 'x')), "Malformed or oversized input accepted");
        tick();
        Require(content() == text, "Invalid paste mutated text");
        Require(paste(""), "Cut rejected");
        tick();
        Require(content().empty(), "Cut did not remove selection");
        Require(paste(std::string(1022U, 'x') + "e\xCC\x81"), "Byte-limit paste rejected");
        tick();
        Require(content() == std::string(1022U, 'x'), "Storage limits must reject the whole trailing grapheme");
        selectAll();
        Require(paste(""), "Byte-limit fixture cleanup failed");
        tick();
        auto config = *scene.Components().UI().TryGet<UIInputField>(field.Entity());
        config.characterLimit = 1U;
        scene.Components().UI().Set(field.Entity(), config);
        Require(paste("e\xCC\x81" "x"), "Limited paste rejected");
        tick();
        Require(content() == "e\xCC\x81", "Paste split a grapheme or exceeded the limit");
        selectAll();
        config.readOnly = true;
        scene.Components().UI().Set(field.Entity(), config);
        Require(copy() == "e\xCC\x81" && !paste(""), "Read-only copy/cut contract failed");
        for (const auto type : {UIInputContentType::Password, UIInputContentType::Pin}) {
            config.contentType = type;
            scene.Components().UI().Set(field.Entity(), config);
            Require(!copy(), "Protected field exposed its selection");
        }
        config.contentType = UIInputContentType::Standard;
        config.readOnly = false;
        scene.Components().UI().Set(field.Entity(), config);
        Require(paste("pending"), "Pending paste rejected");
        ui.ClearFocus();
        tick();
        Require(content() == "e\xCC\x81" && !paste("stale") && !copy(), "Focus loss accepted a stale transfer");
        std::cout << "Browser clipboard native field contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
