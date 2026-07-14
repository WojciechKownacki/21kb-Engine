#include "TestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputAssetLoaders.hpp"
#include "engine/input/InputModifiers.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/input/InputTriggers.hpp"

#include <filesystem>
#include <memory>
#include <system_error>
#include <unordered_map>

namespace kb::tests {
namespace {

using namespace kb::input;

void TestModifiers() {
    ModifierRuntimeState state;

    // Negate with no flags negates every axis.
    InputModifierDesc negate{.type = InputModifierType::Negate, .params = {}};
    const InputValue negated = ApplyModifier(InputValue{.x = 1.0F, .y = -2.0F, .z = 3.0F,
                                                        .type = InputActionValueType::Axis3D},
                                             negate, 0.0F, state);
    Require(NearlyEqual(negated.x, -1.0F) && NearlyEqual(negated.y, 2.0F) && NearlyEqual(negated.z, -3.0F),
            "Negate should flip all axes by default");

    // Scalar with a default (zero) param acts as identity, set params scale.
    InputModifierDesc scalar{.type = InputModifierType::Scalar, .params = {2.0F, 0.0F, 0.0F}};
    const InputValue scaled = ApplyModifier(InputValue{.x = 3.0F, .y = 4.0F, .z = 0.0F,
                                                       .type = InputActionValueType::Axis2D},
                                            scalar, 0.0F, state);
    Require(NearlyEqual(scaled.x, 6.0F) && NearlyEqual(scaled.y, 4.0F), "Scalar should scale x, leave y identity");

    // Radial dead zone: below lower -> 0; above -> rescaled.
    InputModifierDesc dead{.type = InputModifierType::DeadZone, .params = {0.2F, 1.0F, 0.0F}};
    const InputValue small = ApplyModifier(InputValue{.x = 0.1F, .y = 0.0F, .z = 0.0F,
                                                      .type = InputActionValueType::Axis1D},
                                           dead, 0.0F, state);
    Require(NearlyEqual(small.x, 0.0F), "DeadZone should zero values under the lower threshold");

    // Swizzle YXZ swaps x and y.
    InputModifierDesc swizzle{.type = InputModifierType::SwizzleAxis,
                              .params = {static_cast<float>(InputSwizzleOrder::YXZ), 0.0F, 0.0F}};
    const InputValue swizzled = ApplyModifier(InputValue{.x = 1.0F, .y = 5.0F, .z = 0.0F,
                                                        .type = InputActionValueType::Axis2D},
                                              swizzle, 0.0F, state);
    Require(NearlyEqual(swizzled.x, 5.0F) && NearlyEqual(swizzled.y, 1.0F), "Swizzle YXZ should swap x and y");
}

void TestTriggers() {
    // Pressed fires only on the rising edge.
    InputTriggerDesc pressed{.type = InputTriggerType::Pressed, .params = {}, .chordActionId = 0U};
    TriggerRuntimeState pressedState;
    Require(EvaluateTrigger(pressed, pressedState, 1.0F, 0.016F, false) == TriggerState::Triggered,
            "Pressed should trigger on the first actuated frame");
    Require(EvaluateTrigger(pressed, pressedState, 1.0F, 0.016F, false) == TriggerState::None,
            "Pressed should not retrigger while held");

    // Hold fires after the configured time elapses.
    InputTriggerDesc hold{.type = InputTriggerType::Hold, .params = {0.5F, 0.1F, 0.0F}, .chordActionId = 0U};
    TriggerRuntimeState holdState;
    Require(EvaluateTrigger(hold, holdState, 1.0F, 0.05F, false) == TriggerState::Ongoing,
            "Hold should be ongoing before the hold time");
    Require(EvaluateTrigger(hold, holdState, 1.0F, 0.06F, false) == TriggerState::Triggered,
            "Hold should trigger once the hold time elapses");

    // Tap fires on a quick release.
    InputTriggerDesc tap{.type = InputTriggerType::Tap, .params = {0.5F, 0.2F, 0.0F}, .chordActionId = 0U};
    TriggerRuntimeState tapState;
    Require(EvaluateTrigger(tap, tapState, 1.0F, 0.05F, false) == TriggerState::Ongoing, "Tap held is ongoing");
    Require(EvaluateTrigger(tap, tapState, 0.0F, 0.05F, false) == TriggerState::Triggered,
            "Tap should trigger on a quick release");
}

std::shared_ptr<InputActionAsset> MakeAction(std::string name, InputActionValueType type, bool consume) {
    auto action = std::make_shared<InputActionAsset>();
    action->name = std::move(name);
    action->valueType = type;
    action->consumeInput = consume;
    return action;
}

void TestSubsystemAndConsume() {
    auto move = MakeAction("Move", InputActionValueType::Axis1D, true);
    auto jump = MakeAction("Jump", InputActionValueType::Bool, true);

    // High-priority context maps W -> Move (Down). Low-priority maps W -> Jump.
    auto high = std::make_shared<InputMappingContextAsset>();
    high->mappings.push_back(InputKeyMapping{.actionId = 1U, .key = InputKey::W, .modifiers = {}, .triggers = {}});
    high->mappings.push_back(InputKeyMapping{.actionId = 2U, .key = InputKey::Space, .modifiers = {}, .triggers = {}});
    auto low = std::make_shared<InputMappingContextAsset>();
    low->mappings.push_back(InputKeyMapping{.actionId = 2U, .key = InputKey::W, .modifiers = {}, .triggers = {}});

    std::unordered_map<std::uint64_t, std::shared_ptr<InputActionAsset>> actions{{1U, move}, {2U, jump}};
    std::unordered_map<std::uint64_t, std::shared_ptr<InputMappingContextAsset>> contexts{{10U, high}, {20U, low}};

    InputSubsystem subsystem;
    subsystem.SetResolvers(
        [&actions](std::uint64_t id) -> std::shared_ptr<const InputActionAsset> {
            const auto found = actions.find(id);
            return found != actions.end() ? found->second : nullptr;
        },
        [&contexts](std::uint64_t id) -> std::shared_ptr<const InputMappingContextAsset> {
            const auto found = contexts.find(id);
            return found != contexts.end() ? found->second : nullptr;
        });

    Require(subsystem.AddMappingContext(10U, 10), "High context should resolve and add");
    Require(subsystem.AddMappingContext(20U, 0), "Low context should resolve and add");

    subsystem.MutableDeviceState().SetKeyDown(InputKey::W, true);
    subsystem.Evaluate(0.016F);

    Require(subsystem.IsActionPressed("Move"), "Move should be pressed while W is down");
    Require(NearlyEqual(subsystem.GetActionValue("Move").AsAxis1D(), 1.0F), "Move value should be 1.0");
    Require(!subsystem.IsActionPressed("Jump"),
            "Jump must not fire: the higher-priority context consumes W");
    Require(subsystem.WasActionStarted("Move"), "Move should report Started on its first triggered frame");

    subsystem.MutableDeviceState().SetKeyDown(InputKey::Space, true);
    subsystem.Evaluate(0.016F);
    Require(subsystem.WasActionStarted("Jump"), "Jump should report Started on the Space press");
    subsystem.MutableDeviceState().SetKeyDown(InputKey::Space, false);
    subsystem.Evaluate(0.016F);
    Require(subsystem.WasActionReleased("Jump"), "Jump should report Released when Space goes up");
}

void TestAxisScaleAndContinuous() {
    auto move = MakeAction("Move", InputActionValueType::Axis1D, true);

    // W (+1) and S (-1) build a keyboard axis; the left stick X feeds it analog.
    auto context = std::make_shared<InputMappingContextAsset>();
    context->mappings.push_back(InputKeyMapping{.actionId = 1U, .key = InputKey::W, .scale = 1.0F});
    context->mappings.push_back(InputKeyMapping{.actionId = 1U, .key = InputKey::S, .scale = -1.0F});
    context->mappings.push_back(InputKeyMapping{.actionId = 1U, .key = InputKey::GamepadLeftStickX, .scale = 1.0F});

    std::unordered_map<std::uint64_t, std::shared_ptr<InputActionAsset>> actions{{1U, move}};
    std::unordered_map<std::uint64_t, std::shared_ptr<InputMappingContextAsset>> contexts{{10U, context}};

    InputSubsystem subsystem;
    subsystem.SetResolvers(
        [&actions](std::uint64_t id) -> std::shared_ptr<const InputActionAsset> {
            const auto found = actions.find(id);
            return found != actions.end() ? found->second : nullptr;
        },
        [&contexts](std::uint64_t id) -> std::shared_ptr<const InputMappingContextAsset> {
            const auto found = contexts.find(id);
            return found != contexts.end() ? found->second : nullptr;
        });
    Require(subsystem.AddMappingContext(10U, 0), "Axis context should resolve");

    subsystem.MutableDeviceState().SetKeyDown(InputKey::W, true);
    subsystem.Evaluate(0.016F);
    Require(NearlyEqual(subsystem.GetActionValue("Move").AsAxis1D(), 1.0F), "W with scale +1 should give axis +1");

    subsystem.MutableDeviceState().SetKeyDown(InputKey::S, true);
    subsystem.Evaluate(0.016F);
    Require(NearlyEqual(subsystem.GetActionValue("Move").AsAxis1D(), 0.0F), "Opposing W/S should cancel to 0");

    // Sub-threshold analog must still flow (axes are not trigger-gated).
    subsystem.MutableDeviceState().Reset();
    subsystem.MutableDeviceState().SetAnalog(InputKey::GamepadLeftStickX, 0.3F);
    subsystem.Evaluate(0.016F);
    Require(NearlyEqual(subsystem.GetActionValue("Move").AsAxis1D(), 0.3F), "Analog axis below 0.5 must still register its value");
}

void TestAssetRoundTrip() {
    InputActionAsset action;
    action.name = "Fire";
    action.valueType = InputActionValueType::Axis2D;
    action.consumeInput = false;
    const auto actionBytes = EncodeInputAction(action);
    const auto decodedAction = DecodeInputAction(actionBytes);
    Require(decodedAction.succeeded, "Input action should decode");
    Require(decodedAction.asset.name == "Fire" &&
                decodedAction.asset.valueType == InputActionValueType::Axis2D &&
                !decodedAction.asset.consumeInput,
            "Input action round-trip should preserve fields");

    InputMappingContextAsset context;
    InputKeyMapping mapping;
    mapping.bindingId = 99U;
    mapping.actionId = 42U;
    mapping.key = InputKey::Space;
    mapping.modifiers.push_back(InputModifierDesc{.type = InputModifierType::Negate, .params = {1.0F, 0.0F, 0.0F}});
    mapping.triggers.push_back(InputTriggerDesc{.type = InputTriggerType::Hold,
                                                .params = {0.5F, 1.0F, 0.0F},
                                                .chordActionId = 7U});
    context.mappings.push_back(std::move(mapping));

    InputCompositeBinding composite;
    composite.bindingId = 100U;
    composite.actionId = 43U;
    composite.slots.push_back(InputCompositeSlot{.key = InputKey::D, .axis = 0U, .scale = 1.0F});
    composite.slots.push_back(InputCompositeSlot{.key = InputKey::A, .axis = 0U, .scale = -1.0F});
    composite.modifiers.push_back(InputModifierDesc{.type = InputModifierType::DeadZone, .params = {0.2F, 1.0F, 0.0F}});
    context.composites.push_back(std::move(composite));

    const auto contextBytes = EncodeInputMappingContext(context);
    const auto decodedContext = DecodeInputMappingContext(contextBytes);
    Require(decodedContext.succeeded, "Mapping context should decode");
    Require(decodedContext.asset.mappings.size() == 1U, "Mapping count should round-trip");
    const InputKeyMapping& roundTripped = decodedContext.asset.mappings.front();
    Require(roundTripped.bindingId == 99U && roundTripped.actionId == 42U && roundTripped.key == InputKey::Space,
            "Mapping basics should round-trip, including the stable binding id");
    Require(roundTripped.modifiers.size() == 1U && roundTripped.triggers.size() == 1U,
            "Modifier/trigger stacks should round-trip");
    Require(roundTripped.triggers.front().type == InputTriggerType::Hold &&
                roundTripped.triggers.front().chordActionId == 7U,
            "Trigger details should round-trip");

    Require(decodedContext.asset.composites.size() == 1U, "Composite count should round-trip");
    const InputCompositeBinding& roundTrippedComposite = decodedContext.asset.composites.front();
    Require(roundTrippedComposite.bindingId == 100U && roundTrippedComposite.actionId == 43U,
            "Composite basics should round-trip");
    Require(roundTrippedComposite.slots.size() == 2U, "Composite slot count should round-trip");
    Require(roundTrippedComposite.slots[0].key == InputKey::D && roundTrippedComposite.slots[0].axis == 0U &&
                NearlyEqual(roundTrippedComposite.slots[0].scale, 1.0F),
            "Composite slot 0 should round-trip");
    Require(roundTrippedComposite.slots[1].key == InputKey::A && NearlyEqual(roundTrippedComposite.slots[1].scale, -1.0F),
            "Composite slot 1 should round-trip");
    Require(roundTrippedComposite.modifiers.size() == 1U, "Composite modifier stack should round-trip");
}

void TestCompositeBinding() {
    auto move = MakeAction("Move", InputActionValueType::Axis2D, true);
    auto jump = MakeAction("Jump", InputActionValueType::Bool, true);

    // A WASD composite feeding one Axis2D action, with a single radial DeadZone
    // applied to the *combined* vector - this is the behavior a set of four
    // independent InputKeyMappings cannot express (each would dead-zone its own
    // scalar alone, so a diagonal press would report magnitude sqrt(2) instead of
    // a normalized 1.0).
    auto lowContext = std::make_shared<InputMappingContextAsset>();
    InputCompositeBinding moveComposite;
    moveComposite.bindingId = 1U;
    moveComposite.actionId = 1U;
    moveComposite.slots.push_back(InputCompositeSlot{.key = InputKey::D, .axis = 0U, .scale = 1.0F});
    moveComposite.slots.push_back(InputCompositeSlot{.key = InputKey::A, .axis = 0U, .scale = -1.0F});
    moveComposite.slots.push_back(InputCompositeSlot{.key = InputKey::W, .axis = 1U, .scale = 1.0F});
    moveComposite.slots.push_back(InputCompositeSlot{.key = InputKey::S, .axis = 1U, .scale = -1.0F});
    moveComposite.modifiers.push_back(InputModifierDesc{.type = InputModifierType::DeadZone, .params = {0.2F, 1.0F, 0.0F}});
    lowContext->composites.push_back(std::move(moveComposite));

    // A higher-priority context claims W for Jump, so the composite must still
    // combine correctly from whichever of its slot keys remain unclaimed.
    auto highContext = std::make_shared<InputMappingContextAsset>();
    highContext->mappings.push_back(InputKeyMapping{.bindingId = 2U, .actionId = 2U, .key = InputKey::W});

    std::unordered_map<std::uint64_t, std::shared_ptr<InputActionAsset>> actions{{1U, move}, {2U, jump}};
    std::unordered_map<std::uint64_t, std::shared_ptr<InputMappingContextAsset>> contexts{
        {10U, highContext}, {20U, lowContext}};

    InputSubsystem subsystem;
    subsystem.SetResolvers(
        [&actions](std::uint64_t id) -> std::shared_ptr<const InputActionAsset> {
            const auto found = actions.find(id);
            return found != actions.end() ? found->second : nullptr;
        },
        [&contexts](std::uint64_t id) -> std::shared_ptr<const InputMappingContextAsset> {
            const auto found = contexts.find(id);
            return found != contexts.end() ? found->second : nullptr;
        });
    Require(subsystem.AddMappingContext(10U, 10), "High-priority Jump context should resolve");
    Require(subsystem.AddMappingContext(20U, 0), "Low-priority Move composite context should resolve");

    // D alone: full-magnitude axial press should pass the dead zone at scale 1.
    subsystem.MutableDeviceState().SetKeyDown(InputKey::D, true);
    subsystem.Evaluate(0.016F);
    const InputValue dOnly = subsystem.GetActionValue("Move");
    Require(NearlyEqual(dOnly.x, 1.0F) && NearlyEqual(dOnly.y, 0.0F), "D alone should give composite value (1, 0)");

    // W+D diagonal: the combined-vector dead zone must normalize the magnitude to
    // 1.0 (0.7071, 0.7071), proving the modifier ran once on the resultant vector.
    subsystem.MutableDeviceState().SetKeyDown(InputKey::W, true);
    subsystem.Evaluate(0.016F);
    const InputValue diagonal = subsystem.GetActionValue("Move");
    Require(NearlyEqual(diagonal.Magnitude(), 1.0F),
            "Diagonal W+D should normalize to magnitude 1.0 via the composite's shared dead zone");
    Require(diagonal.x > 0.0F && diagonal.y == 0.0F,
            "W's contribution must be excluded: the high-priority context already consumed W for Jump");
    Require(subsystem.IsActionPressed("Jump"), "Jump should still fire from the higher-priority context's W mapping");

    subsystem.MutableDeviceState().Reset();
    subsystem.Evaluate(0.016F);
    subsystem.MutableDeviceState().SetKeyDown(InputKey::A, true);
    subsystem.MutableDeviceState().SetKeyDown(InputKey::S, true);
    subsystem.Evaluate(0.016F);
    const InputValue negDiagonal = subsystem.GetActionValue("Move");
    Require(NearlyEqual(negDiagonal.Magnitude(), 1.0F), "A+S diagonal should also normalize to magnitude 1.0");
    Require(negDiagonal.x < 0.0F && negDiagonal.y < 0.0F, "A+S should give a negative x/y composite direction");
}

void TestBindingIdStableAcrossRebind() {
    // Proves the asset-level "rebinding" contract LIB-113 defines: a binding is
    // addressed by its stable bindingId, not by the physical key it currently
    // points to, so a rebind operation (find-by-id, then mutate .key) works even
    // though the whole point of rebinding is to change that key. The live runtime
    // rebind API with conflict validation and settings persistence is LIB-119; this
    // only proves the asset shape supports it.
    auto move = MakeAction("Move", InputActionValueType::Axis1D, true);
    auto context = std::make_shared<InputMappingContextAsset>();
    context->mappings.push_back(InputKeyMapping{.bindingId = 7U, .actionId = 1U, .key = InputKey::W});

    std::unordered_map<std::uint64_t, std::shared_ptr<InputActionAsset>> actions{{1U, move}};
    std::unordered_map<std::uint64_t, std::shared_ptr<InputMappingContextAsset>> contexts{{10U, context}};

    InputSubsystem subsystem;
    subsystem.SetResolvers(
        [&actions](std::uint64_t id) -> std::shared_ptr<const InputActionAsset> {
            const auto found = actions.find(id);
            return found != actions.end() ? found->second : nullptr;
        },
        [&contexts](std::uint64_t id) -> std::shared_ptr<const InputMappingContextAsset> {
            const auto found = contexts.find(id);
            return found != contexts.end() ? found->second : nullptr;
        });
    Require(subsystem.AddMappingContext(10U, 0), "Context should resolve before rebind");

    subsystem.MutableDeviceState().SetKeyDown(InputKey::W, true);
    subsystem.Evaluate(0.016F);
    Require(subsystem.IsActionPressed("Move"), "Move should fire on W before rebinding");

    // Find the binding by its stable id (not by key) and rebind it to S.
    InputKeyMapping* rebound = nullptr;
    for (InputKeyMapping& candidate : context->mappings) {
        if (candidate.bindingId == 7U) {
            rebound = &candidate;
        }
    }
    Require(rebound != nullptr, "Binding should be locatable by its stable id");
    rebound->key = InputKey::S;
    Require(subsystem.AddMappingContext(10U, 0), "Re-adding the context should re-resolve the rebound mapping");

    subsystem.MutableDeviceState().Reset();
    subsystem.Evaluate(0.016F);
    subsystem.MutableDeviceState().SetKeyDown(InputKey::W, true);
    subsystem.Evaluate(0.016F);
    Require(!subsystem.IsActionPressed("Move"), "Move must no longer fire on the old key W after rebinding");

    subsystem.MutableDeviceState().Reset();
    subsystem.Evaluate(0.016F);
    subsystem.MutableDeviceState().SetKeyDown(InputKey::S, true);
    subsystem.Evaluate(0.016F);
    Require(subsystem.IsActionPressed("Move"), "Move must fire on the new key S after rebinding");
    Require(rebound->bindingId == 7U, "The binding id must remain unchanged across the rebind");
}

void TestAssetDiscoveryAndResolve() {
    // Mirrors how the editor creates an input asset: resolve a mounted folder to a
    // physical path, write the asset there, then rediscover it through the manager.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_input_asset_discovery_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Input asset discovery test root could not be prepared");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<InputActionAssetLoader>()), "Action loader registration failed");
    Require(manager.RegisterLoader(std::make_unique<InputMappingContextAssetLoader>()), "Context loader registration failed");
    Require(manager.Mounts().Mount("Game", root), "Game mount failed");

    // A bare mount root ("/Game") is not resolvable; resolving a probe file inside
    // the folder and taking its parent yields the folder's physical path (this is
    // exactly what the editor's CreateInput*Asset does).
    const std::optional<std::filesystem::path> probe = manager.Mounts().Resolve(std::filesystem::path{"/Game"} / "probe");
    Require(probe.has_value(), "Resolving a file under the mount root must succeed");
    const std::filesystem::path physicalFolder = probe->parent_path();

    InputActionAsset action;
    action.name = "Jump";
    action.valueType = InputActionValueType::Bool;
    const std::filesystem::path actionPath = physicalFolder / ("Jump" + std::string{InputAssetFormat::ActionExtension});
    Require(WriteInputAction(actionPath, action), "Writing the input action asset should succeed");

    const std::size_t discovered = manager.DiscoverMountedAssets();
    Require(discovered >= 1U, "Discovery should pick up the newly written input action asset");

    const std::optional<std::filesystem::path> virtualPath = manager.Mounts().ToVirtual(actionPath);
    Require(virtualPath.has_value(), "Created asset should map back to a virtual path");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*virtualPath);
    Require(metadata != nullptr && metadata->type == "InputAction",
            "Discovered asset should be registered with type InputAction");

    const kb::assets::AssetHandle<InputActionAsset> handle = manager.Load<InputActionAsset>(metadata->id);
    Require(handle.IsLoaded() && handle->name == "Jump", "Loading the discovered input action should round-trip");

    std::filesystem::remove_all(root, error);
}

} // namespace

void RunInputTests() {
    TestModifiers();
    TestTriggers();
    TestSubsystemAndConsume();
    TestAxisScaleAndContinuous();
    TestAssetRoundTrip();
    TestAssetDiscoveryAndResolve();
    TestCompositeBinding();
    TestBindingIdStableAcrossRebind();
}

} // namespace kb::tests
