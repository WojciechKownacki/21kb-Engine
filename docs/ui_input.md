# Modal UI input

Set a screen-space Canvas's `modal` property and place it above the underlying
canvases with `sortingOrder`. While the modal is visible, lower canvases remain
drawn but cannot receive pointer hits or navigation. A Canvas Group controls its
visibility; a fully hidden or transparent dialog releases the modal layer.

The modal policy is global: gameplay actions for every local user are blocked,
including keyboard, mouse, touch and controllers. UI consumes the unmodified
physical snapshot before gameplay input is evaluated. Script `Pointer` button,
delta and scroll queries and `Input.TouchCount/TouchPoint` read the filtered
gameplay snapshot. Pointer coordinates, device connection and window focus remain
available. Use UI events for modal controls.

Opening a modal cancels active gameplay actions and resets their trigger state;
it does not synthesize a release that fires a Tap or Released action. Closing it
keeps keys, buttons and touches held inside the modal suppressed until released.
Controller axes must return within 0.2 of neutral before gameplay resumes. Mouse
deltas and wheel input resume on the next unblocked frame.

Each nested modal remembers the focus it covered. Closing it restores the previous
dialog, then the underlying HUD. Each controller's focus stays on its assigned
canvas. If the previous control was deleted or disabled, the next available
control receives focus. A dialog without controls for a player still blocks that
player's gameplay. Pointer captures and drags on covered controls are canceled.

CLI authoring uses the same Canvas properties as the Inspector:

```sh
kb_cli component set --project Sample --entity Dialog --type Canvas --set modal=true --set sortingOrder=10
```

`kb_game_ui_modal_package` authors and reloads a scene through CLI, replays input
through CLI simulation, cooks it and checks the same focus and gameplay assertions
in the packaged native runtime. Native input and scene tests cover cancellation,
held-input release and nested focus. The editor headless catalog scenario checks
modal authoring, focus, Inspector rendering and undo/redo.
