# Runtime input rebinding

Use `InputRebindSession` in C++, or the following registered functions through
Lua `CallFunction` and Visual Graph. Sessions belong to the script host and local
player; each player can have one open profile. Activate the mapping context as
usual before displaying a settings menu.

```lua
local opened = CallFunction("Input.OpenRebindProfile", {
    context = "/Game/Input/Gameplay.21kbinputcontext",
    path = "Saved/Bindings.profile", player = 0
})
assert(opened.opened, opened.error)

-- A settings button starts capture for a stable InputKeyMapping.bindingId.
local capture = CallFunction("Input.BeginRebind", {binding = "1"})
assert(capture.started, capture.error)

-- Poll once each frame while the settings menu is open.
local result = CallFunction("Input.PollRebind", {})
-- result.state: idle, listening, conflict, applied, canceled or failed.
-- On conflict, display result.conflict (the other binding id), then either:
-- CallFunction("Input.PollRebind", {swapConflict = true})
-- CallFunction("Input.CancelRebind", {})
```

Opening a profile loads its saved overrides; a missing file starts with defaults.
Capture ignores controls held when it starts, mouse motion and wheel deltas, touch
contacts and analog values at or below 0.5 magnitude. Escape cancels capture.
Keyboard, mouse buttons and gamepad controls retain the selected gamepad slot.
A conflict leaves bindings unchanged until the user confirms a swap or cancels.
Swaps apply to simple bindings; a composite conflict requires choosing another
control. Display `result.error` if an operation fails.

Successful captures and swaps save the profile atomically. A failed save restores
the previous live overrides. `CallFunction("Input.ResetRebind", {})` returns
`reset` and `error`, restores defaults and saves an empty profile, so reset also
survives a restart. Cancel never saves. Use the same writable profile path when
opening settings after a restart. Pass the same optional `player` to every call
when configuring a secondary local player.

Keep the settings screen in a modal canvas when gameplay must not receive the
captured controls. Capture reads physical input, so it continues to work while
the modal blocks gameplay actions.
