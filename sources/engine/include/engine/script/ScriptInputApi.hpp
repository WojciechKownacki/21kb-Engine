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
//   Input.Rebind(context:String, binding:String, key:String,
//                gamepadIndex:Int, allowConflict:Bool)
//       -> applied:Bool, conflict:String
//   Input.SaveRebindProfile(context:String, path:String)
//       -> saved:Bool, error:String
//   Input.LoadRebindProfile(context:String, path:String)
//       -> loaded:Bool, error:String
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
//   Pointer.Position()       -> x:Float, y:Float (active render-viewport pixels)
//   Pointer.Delta()          -> x:Float, y:Float (= Input.Value semantics on MouseX/MouseY)
//   Pointer.Button(button:Int) -> pressed:Bool   (0=left, 1=right, 2=middle)
//   Pointer.Scroll()         -> delta:Float (normalized wheel detents accumulated this frame)
//   Pointer.Ray(player?)     -> valid:Bool, originX/Y/Z:Float,
//                               directionX/Y/Z:Float
// Ray uses SceneRenderFeedback's renderer-published active camera for the
// selected local player and
// viewport (the single source of truth introduced by LIB-145). It reports
// valid=false before a camera frame has actually been submitted; there is no
// guessed camera or matrix fallback.
//
// Named context priority bands (LIB-118) - Input.AddMappingContext's priority
// argument is a plain int with no established convention; these functions let
// scripts reference the engine's reserved bands by name instead of hardcoding
// magic numbers, so a future console/debug-overlay/UI system (LIB-173/224-226/
// 180) reliably outranks gameplay's default (0) and each other, in the order
// DebugOverlay > Console > UI > Gameplay (see InputContextPriority.hpp):
//   Input.PriorityGameplay()    -> priority:Int (0)
//   Input.PriorityUI()          -> priority:Int (1000)
//   Input.PriorityConsole()     -> priority:Int (2000)
//   Input.PriorityDebugOverlay() -> priority:Int (3000)
// No UI tree, console, or debug overlay exists yet to actually push contexts
// at these bands - that is LIB-173/226/224-225's job. This only establishes
// the shared priority contract they will all use, and is verified against the
// real InputMappingContextStack consumption mechanism (not just declared).
//
// Focus / device presence (LIB-120):
//   Input.HasFocus()                      -> focus:Bool
//   Input.IsGamepadConnected(gamepadIndex:Int) -> connected:Bool
// Losing window focus already zeroes every key/axis/touch point (Reset() +
// the platform layer's early return), which already correctly fires
// WasReleased/action-Completed for anything that was down - proven by
// ScriptRuntimeTests.cpp's RunScriptInputFocusLossReleasesActionsTest, not new
// production behavior. HasFocus/IsGamepadConnected exist so script can tell
// *why* input went quiet (focus lost vs. genuinely idle vs. controller
// unplugged) and react, e.g. auto-pause or show a reconnect prompt.
struct ScriptInputApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
