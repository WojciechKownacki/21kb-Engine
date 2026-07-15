#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers the runtime input API as script functions on the host. Because the
// host mirrors every registered function into both the Lua function table
// (CallFunction) and a Visual Graph CallNative node, a single registration here
// exposes input to all three scripting backends from one source of truth.
//
// Every function below also takes an optional trailing `player:Int` pin (LIB-115).
// Omitted or <= 0 means the primary/default local user - the exact single-player
// behavior every function had before this pin existed. player=N queries/mutates
// local user N's own independent InputSubsystem instead (see LocalUserId).
//
// Registered functions (all take/return well-typed pins):
//   Input.IsPressed(action:String)        -> pressed:Bool
//   Input.WasPressed(action:String)       -> pressed:Bool
//   Input.WasReleased(action:String)      -> released:Bool
//   Input.Value(action:String)            -> value:Float        (axis1D / bool as 0/1)
//   Input.Vector2(action:String)          -> x:Float, y:Float
//   Input.Vector3(action:String)          -> x:Float, y:Float, z:Float
//   Input.AddMappingContext(context:String, priority:Int) -> added:Bool
//   Input.RemoveMappingContext(context:String)            -> removed:Bool
//
// Canonical typed-value + trigger-event names (additive aliases over the above,
// same underlying InputSubsystem queries - kept as separate registrations rather
// than renames so existing scripts/graphs using the names above keep working):
//   Input.ActionBool(action:String)  -> value:Bool   (raw/modified value != 0;
//                                       differs from Held below when a trigger's
//                                       threshold/deadzone hasn't fired yet even
//                                       though the value itself is non-zero)
//   Input.ActionFloat(action:String) -> value:Float  (= Input.Value)
//   Input.Action2D(action:String)    -> x:Float, y:Float (= Input.Vector2)
//   Input.Pressed(action:String)     -> pressed:Bool  (rising edge, first frame only)
//   Input.Released(action:String)    -> released:Bool (falling edge, first frame only)
//   Input.Held(action:String)        -> held:Bool     (trigger state this frame; = Input.IsPressed)
//
// Legacy aliases kept for existing scripts/graphs:
//   IsActionPressed(action:String)        -> pressed:Bool
//   GetActionValue(action:String)         -> value:Float        (axis1D / bool as 0/1)
//   GetActionValueXY(action:String)       -> x:Float, y:Float   (axis2D)
//   WasActionStarted(action:String)       -> started:Bool
//   WasActionTriggered(action:String)     -> triggered:Bool
//   WasActionCompleted(action:String)     -> completed:Bool
//   AddMappingContext(context:String, priority:Int)  -> added:Bool
//   RemoveMappingContext(context:String)             -> removed:Bool
// (context is the decimal string form of the mapping-context asset id.)
//
// Pointer (LIB-117) - the mouse is a singular physical device (unlike
// gamepads), shared by every local user, so these take NO player pin and
// always read the primary local user's device state:
//   Pointer.Position()       -> x:Float, y:Float (absolute, host window client pixels)
//   Pointer.Delta()          -> x:Float, y:Float (= Input.Value semantics on MouseX/MouseY)
//   Pointer.Button(button:Int) -> pressed:Bool   (0=left, 1=right, 2=middle)
//
// Pointer.Scroll and Pointer.Ray are deliberately NOT implemented yet:
//   - Scroll needs real wheel delta, which is message-driven (WM_MOUSEWHEEL),
//     not pollable like GetAsyncKeyState/GetCursorPos/XInputGetState. Wiring it
//     requires adding a side effect to the editor's existing, currently
//     zero-test-coverage EditorMouseWheelRouter (sources/editor/src/app/
//     EditorMouseWheelRouter.cpp) *and* respecting play-vs-edit-mode routing
//     (over the scene viewport, wheel already drives edit-camera zoom) -
//     deferred with the same reasoning as LIB-116's touch WM_TOUCH gap; see
//     others/_temp.md's POWRÓT list.
//   - Ray needs a screen-space-to-world unproject through the active camera's
//     view/projection matrices. Those matrices are assembled today only in
//     kb::render (RenderSceneCameraBuilder), not at the kb::scene/kb::script
//     layer this API lives in, and CameraComponent has no viewport/priority
//     fields yet - both are explicitly LIB-135's and LIB-145's scope
//     ("Camera z pose/projection/viewport/priority" and "screen/world
//     conversions, ray z kamery"), so implementing it here would either
//     duplicate that work or require a new kb::scene -> kb::render dependency.
struct ScriptInputApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
