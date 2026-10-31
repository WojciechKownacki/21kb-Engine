#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <system_error>

namespace kb::cli {
namespace {

using kb::input::InputActionValueType;

constexpr std::string_view kDefaultContext = "/Game/Input/PlayerInput.21kbinputcontext";

[[nodiscard]] std::filesystem::path AssetFile(const std::filesystem::path& root, std::string_view virtualPath) {
    return root / "Assets" / kb::assets::Utf8Path(std::string{ virtualPath.substr(std::string_view{ "/Game/" }.size()) });
}

[[nodiscard]] std::optional<InputActionValueType> ParseValueType(std::string_view text) {
    if (text == "Bool" || text == "Button") return InputActionValueType::Bool;
    if (text == "Float" || text == "Axis1D") return InputActionValueType::Axis1D;
    if (text == "Axis2D" || text == "Vector2") return InputActionValueType::Axis2D;
    if (text == "Axis3D" || text == "Vector3") return InputActionValueType::Axis3D;
    return std::nullopt;
}

// "Space", "W=+y", "A=-x": a key, and for an axis action which axis it pushes and which way.
struct KeyBinding {
    kb::input::InputKey key = kb::input::InputKey::None;
    std::optional<std::uint8_t> axis;
    float scale = 1.0F;
};

[[nodiscard]] std::optional<KeyBinding> ParseBinding(std::string_view text, std::string& error) {
    KeyBinding binding;
    const std::size_t equals = text.find('=');
    const std::string_view keyName = text.substr(0, equals);
    binding.key = kb::input::ParseInputKey(keyName);
    if (binding.key == kb::input::InputKey::None) {
        error = "unknown key '" + std::string{ keyName } + "' (keys: A..Z, Num0..Num9, Space, Enter, Escape, Tab, LeftShift, "
            "LeftControl, ArrowUp, ArrowLeft, MouseLeft, MouseRight, MouseX, GamepadFaceBottom, GamepadLeftStickX, ...)";
        return std::nullopt;
    }
    if (equals == std::string_view::npos) return binding;
    const std::string_view direction = text.substr(equals + 1U);
    if (direction.size() != 2U || (direction[0] != '+' && direction[0] != '-') || direction[1] < 'x' || direction[1] > 'z') {
        error = "'" + std::string{ text } + "': the direction after = is +x, -x, +y, -y, +z or -z";
        return std::nullopt;
    }
    binding.axis = static_cast<std::uint8_t>(direction[1] - 'x');
    binding.scale = direction[0] == '-' ? -1.0F : 1.0F;
    return binding;
}

[[nodiscard]] std::string BindingText(const kb::input::InputMappingContextAsset& context, std::uint64_t actionId) {
    std::string text;
    for (const kb::input::InputKeyMapping& mapping : context.mappings) {
        if (mapping.actionId == actionId) text += (text.empty() ? "" : ", ") + std::string{ kb::input::ToString(mapping.key) };
    }
    for (const kb::input::InputCompositeBinding& composite : context.composites) {
        if (composite.actionId != actionId) continue;
        for (const kb::input::InputCompositeSlot& slot : composite.slots) {
            text += (text.empty() ? "" : ", ") + std::string{ kb::input::ToString(slot.key) } + '=' + (slot.scale < 0.0F ? '-' : '+') +
                static_cast<char>('x' + slot.axis);
        }
    }
    return text.empty() ? std::string{ "no keys" } : text;
}

struct ProjectInput {
    std::filesystem::path root;
    std::filesystem::path settingsFile;
    kb::project::ProjectSettings settings;
    std::string contextPath;
    kb::input::InputMappingContextAsset context;
};

[[nodiscard]] std::optional<ProjectInput> OpenProjectInput(const ArgumentList& arguments, CommandIo io) {
    ProjectInput input;
    input.root = material::ProjectRoot(arguments, "input-action", io);
    if (input.root.empty()) return std::nullopt;
    input.settingsFile = kb::project::ProjectSettingsStore::FilePath(input.root);
    kb::project::ProjectSettingsLoadResult loaded = kb::project::ProjectSettingsStore::Load(input.settingsFile);
    if (!loaded.Succeeded()) {
        io.err << "error: the project settings could not be read\n";
        return std::nullopt;
    }
    input.settings = std::move(loaded.settings);
    input.contextPath = input.settings.inputMappingContext.empty() ? std::string{ kDefaultContext } : input.settings.inputMappingContext;
    if (std::error_code error; std::filesystem::exists(AssetFile(input.root, input.contextPath), error)) {
        auto context = kb::input::ReadInputMappingContext(AssetFile(input.root, input.contextPath));
        if (!context.succeeded) {
            io.err << "error: the input context " << input.contextPath << " could not be read: " << context.error << '\n';
            return std::nullopt;
        }
        input.context = std::move(context.asset);
    }
    return input;
}

[[nodiscard]] int InputActionList(const ArgumentList& arguments, CommandIo io) {
    std::optional<ProjectInput> input = OpenProjectInput(arguments, io);
    if (!input.has_value()) return 1;
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, input->root, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "context: " << (input->settings.inputMappingContext.empty() ? std::string{ "none" } : input->contextPath) << '\n';
    std::size_t count = 0U;
    for (const kb::assets::AssetMetadata& metadata : scene.Assets().Manager().Registry().All()) {
        const std::string path = metadata.virtualPath.generic_string();
        if (!path.ends_with(kb::input::InputAssetFormat::ActionExtension) && !path.ends_with(kb::input::InputAssetFormat::AxisExtension)) continue;
        const auto action = kb::input::ReadInputAction(AssetFile(input->root, path));
        if (!action.succeeded) continue;
        io.out << action.asset.name << " (" << kb::input::ToString(action.asset.valueType) << "): " << BindingText(input->context, metadata.id.value)
               << "  " << path << '\n';
        ++count;
    }
    if (count == 0U) io.out << "no input actions; input-action new --name Fire --key Space makes one\n";
    return 0;
}

[[nodiscard]] int InputActionNew(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    const std::vector<std::string> keys = arguments.Options("--key");
    if (!name.has_value() || !kb::assets::AssetFileNameError(*name).empty()) {
        io.err << "error: input-action new requires --name <Name> (a file name: letters, digits, spaces) and --key <Key> ...\n";
        return 1;
    }
    const std::optional<InputActionValueType> type = ParseValueType(arguments.Option("--type").value_or("Bool"));
    if (!type.has_value()) {
        io.err << "error: --type is Bool, Float, Axis2D or Axis3D\n";
        return 1;
    }
    std::vector<KeyBinding> bindings;
    for (const std::string& text : keys) {
        std::string error;
        const std::optional<KeyBinding> binding = ParseBinding(text, error);
        if (!binding.has_value()) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        if (binding->axis.has_value() && *binding->axis >= kb::input::AxisCount(*type)) {
            io.err << "error: --key " << text << " pushes an axis a " << kb::input::ToString(*type) << " action does not have\n";
            return 1;
        }
        bindings.push_back(*binding);
    }
    std::optional<ProjectInput> input = OpenProjectInput(arguments, io);
    if (!input.has_value()) return 1;

    // The action asset: made once, kept (with its id) when it already exists.
    const std::string actionPath = "/Game/Input/" + *name + std::string{ kb::input::InputAssetFormat::ActionExtension };
    const std::filesystem::path actionFile = AssetFile(input->root, actionPath);
    std::error_code fileError;
    if (std::filesystem::exists(actionFile, fileError)) {
        const auto existing = kb::input::ReadInputAction(actionFile);
        if (!existing.succeeded || existing.asset.valueType != *type) {
            io.err << "error: " << actionPath << " exists as a " << (existing.succeeded ? kb::input::ToString(existing.asset.valueType) : "unreadable")
                   << " action; pass that --type or another --name\n";
            return 1;
        }
    } else {
        std::filesystem::create_directories(actionFile.parent_path(), fileError);
        kb::input::InputActionAsset action;
        action.name = *name;
        action.valueType = *type;
        if (!kb::input::WriteInputAction(actionFile, action)) {
            io.err << "error: " << actionPath << " could not be written\n";
            return 1;
        }
    }
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, input->root, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(actionPath);
    if (metadata == nullptr) {
        io.err << "error: " << actionPath << " was written but is not an asset of the project\n";
        return 1;
    }
    const std::uint64_t actionId = metadata->id.value;

    // Keys with a direction form one composite (W/A/S/D -> a 2D vector); plain keys map directly.
    kb::input::InputCompositeBinding* composite = nullptr;
    for (kb::input::InputCompositeBinding& candidate : input->context.composites) {
        if (candidate.actionId == actionId) composite = &candidate;
    }
    for (const KeyBinding& binding : bindings) {
        if (binding.axis.has_value()) {
            if (composite == nullptr) {
                input->context.composites.push_back(kb::input::InputCompositeBinding{ .actionId = actionId });
                composite = &input->context.composites.back();
            }
            const bool present = std::ranges::any_of(composite->slots, [&](const kb::input::InputCompositeSlot& slot) {
                return slot.key == binding.key && slot.axis == *binding.axis && slot.scale == binding.scale;
            });
            if (!present) composite->slots.push_back({ .key = binding.key, .axis = *binding.axis, .scale = binding.scale });
            continue;
        }
        const bool present = std::ranges::any_of(input->context.mappings, [&](const kb::input::InputKeyMapping& mapping) {
            return mapping.actionId == actionId && mapping.key == binding.key;
        });
        if (!present) input->context.mappings.push_back(kb::input::InputKeyMapping{ .actionId = actionId, .key = binding.key });
    }
    const std::filesystem::path contextFile = AssetFile(input->root, input->contextPath);
    std::filesystem::create_directories(contextFile.parent_path(), fileError);
    if (!kb::input::WriteInputMappingContext(contextFile, input->context)) {
        io.err << "error: the input context " << input->contextPath << " could not be written\n";
        return 1;
    }
    if (input->settings.inputMappingContext.empty()) {
        input->settings.inputMappingContext = input->contextPath;
        if (!kb::project::ProjectSettingsStore::Save(input->settingsFile, input->settings, error)) {
            io.err << "error: the project settings could not be saved: " << error << '\n';
            return 1;
        }
    }
    io.out << "input action " << *name << " (" << kb::input::ToString(*type) << "): " << BindingText(input->context, actionId) << " in "
           << input->contextPath << "; scripts read it with Input." << (*type == InputActionValueType::Bool ? "Pressed" : *type == InputActionValueType::Axis1D ? "Value" : *type == InputActionValueType::Axis2D ? "Vector2" : "Vector3")
           << "(\"" << *name << "\")\n";
    return 0;
}

} // namespace

int RunInputActionCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "new") return InputActionNew(arguments, io);
    if (action == "list") return InputActionList(arguments, io);
    io.err << "error: input-action expects new or list\n";
    return 1;
}

} // namespace kb::cli
