# Timeline playback

Open a `.kbtimeline` asset in Project Files to use the Timeline panel. Select
an entity in Hierarchy and choose **Use selected** to resolve bindings against
that owner. The right viewport shows a separate copy of the scene, including
its cameras and scripts. Scrubbing samples silently; Play and Reverse dispatch
the runtime event schema. Pause stops the preview clock, and Loop controls
wrapping during playback. Reverse and silent scrubbing do not play audio.

The left table exposes duration, owner-relative bindings, transform keys,
versioned reflected properties, audio intervals, named events, animation clips,
and nested sequence intervals and binding remaps. Click a value to edit it.
**Add** creates a binding, track or marker; the property picker lists reflected
fields on the bound entity and records their stable schema. Numeric and color values use linear interpolation by
default; discrete values use Step. Select a track heading and use **Add key**
at the playhead, or select a heading/key and use **Delete**. Previous and Next
page through long documents. Invalid values or unresolved preview dependencies
leave the current document and last valid preview unchanged.

Undo and Redo operate on the asset document. Save validates and atomically
replaces the asset, then refreshes its runtime metadata; Revert reloads the saved
asset. The panel shows unsaved state and asks how to handle it when closing.
Changing the scene or entering scene Play pauses the Timeline preview. Saving
does not write preview transforms or script state into the source scene.

Lua scripts that create a Timeline by path declare it with an asset dependency,
for example `-- @asset /Game/Sequence.kbtimeline`. The cooker then retains the
Timeline and its referenced audio clips, animation clips and nested sequences.

A timeline instance owns its cursor and named entity bindings. Its asset owns
the tracks. Instances resolve the current component reflection when applying a
property sample; they do not retain component addresses or native component IDs.

## Cursor and events

`Timeline.SetPlayback(instance, rate, loop)` sets a finite, nonzero signed rate.
Positive rates move toward the asset duration; negative rates move toward zero.
The setting does not move the cursor or start a paused instance. Use
`Timeline.Play` to start or resume playback.

Without looping, playback stops at the boundary in its current direction.
With looping, elapsed time can cross multiple cycles in one update. A positive
traversal that lands exactly on the end retains that endpoint until the next
advance; a negative traversal can similarly retain zero. Pausing preserves the
cursor and playback settings.

`Timeline.Seek` samples the destination without emitting markers. This is the
scrubbing operation. `Timeline.Skip` accepts a destination on either side of the
cursor and an explicit marker policy. It does not implicitly traverse a loop.

Marker traversal excludes the departure time and includes the destination.
Forward traversal emits markers in ascending time order; reverse traversal emits
them in descending order. Markers at equal times follow asset order forward and
the reverse of that order backward. Markers at zero are not permitted by the
asset format. A reverse wrap enters the duration endpoint and emits its markers
before continuing down the new cycle. Seek never emits an endpoint marker.

The pending marker queue has a fixed capacity. If a playback advance would
exceed it, the operation reports an error and preserves the cursor and the queue
contents from before that advance. Hosts should drain pending marker events;
the scene script runtime does this when dispatching timeline marker events.

## Property tracks

Each property track names a timeline binding, component, and reflected field.
The binding stores version 1, the expected field type, and a component reflection
signature. The signature covers component and field names, field order, and
field types. It excludes native component IDs, offsets, and padding so bindings
do not depend on process registration order or platform memory layout.

A renamed component, removed field, changed field type, or changed signature
invalidates the binding. Removing the actual component from a bound entity is
also diagnosed at sampling time. Playback updates only the selected field and
notifies ECS observers of the component write.

Float and double fields and two-, three-, and four-component float vectors
support linear or step interpolation. Booleans, signed and unsigned integers,
and enums require step interpolation. Opaque byte fields are not animatable.
Keys must have strictly increasing finite times inside the asset duration and
finite values matching their field type. Sampling outside the key interval holds
the nearest endpoint value.

Timeline format version 2 adds property rows and keys. Version 1 assets remain
readable. Property rows identify their interpolation as `0` for step or `1` for
linear, and keys reference a preceding property row by its zero-based index:

```text
property "Target" 1 "ComponentName" "fieldName" FIELD_TYPE SCHEMA_SIGNATURE INTERPOLATION
property-key 0 0 VALUE
property-key 0 1 VALUE
```

Generate the signature from `TimelineComponentSchema` using the registered
reflection. It is not a user-selected identifier. Vector key values are written
as space-separated components. Save through `TimelineAssetIO` to validate and
serialize authored values with roundtrip precision.

## Audio tracks

An audio track identifies an entity binding and an `AudioClip` asset reference.
Its active interval includes the start and excludes the end. The source cursor
is the authored clip offset plus elapsed time inside that interval. Volume,
spatial playback, and mixer output bus are authored on the track. Dependency
discovery includes referenced clips; loading rejects missing references and
references to another asset type, including in runtime packages.

Forward playback owns one voice per audio track. Pause retains the voice;
resume synchronizes its source cursor. Seeking or rebinding stops the current
voice so the next playing update starts at the new position. A loop crossing
resynchronizes active voices. Releasing the instance, destroying its owner, or
leaving a clip interval stops its voices. A clip that naturally finishes before
the interval ends stays silent until a jump or re-entry.

Scrubbing and reverse playback are silent; reverse playback does not synthesize
a reversed waveform. A scene without an audio backend still evaluates its
timeline without creating voices. When a backend exists, a failed voice start
or control operation reports an error and stops the instance's remaining voices.

## Bound event tracks

Event keys target a named entity binding and carry an identifier, name and string
payload. Quoted payloads preserve quotes, backslashes and line breaks when saved
and loaded. Version 2 stores each key as:

```text
event "Receiver" 0.5 123 "Cue" "payload"
```

Key times must be greater than zero and no later than the sequence duration.
Playback and explicit emitting skips merge event keys with legacy markers by
time. Equal-time keys retain authored order, with legacy markers first; reverse
traversal reverses that order. Seek and suppressing skips emit nothing.

Bound keys dispatch `OnTimelineEvent` to the resolved object with `schemaMajor`,
`schemaMinor`, `instance`, `asset`, `marker`, `time`, `name` and `payload` fields.
Legacy markers retain `OnTimelineMarker` on the instance owner. The common bounded
queue rejects an overflowing traversal without advancing its cursor or leaving
partial events in the queue.

## Nested sequences

A sequence track references another Timeline asset, a parent interval, a source
offset and a positive rate. Each child binding must explicitly map to a parent
binding; runtime overrides on the parent propagate through all nesting levels.
For example:

```text
sequence "/Game/Child.kbtimeline" 0.25 0.75 0 2
sequence-binding 0 "Actor" "Target"
```

This plays one second of the child inside half a second of its parent. Source
intervals must fit within the referenced sequence. Expansion rejects dependency
cycles, missing remaps, more than 16 levels or more than 256 expanded segments.
Sibling references to the same source remain independent occurrences.

Seeking samples active child transforms and properties without emitting events.
Parent direction and looping also govern child traversal. Child events retain
their source asset identifier and report their time on the root timeline;
legacy markers still target the instance owner. Equal-time ordering follows
parent tracks first, then child sequences in authored depth-first order, with
the complete order reversed during reverse traversal.

Child audio is active only inside its containing interval; pause, release,
reverse playback and source reload also control child voices. Source reload
rebuilds the sequence mapping while preserving explicit root binding overrides.

## Animation clips

Animation tracks reference an `AnimationClip`, target binding, active interval,
source offset and positive rate:

```text
animation "Actor" "/Game/Motion.kbanim" 0.25 0.75 0 2
```

The source range must fit the clip. Timeline controls looping; the clip's own
loop flag does not wrap the authored interval. Seeking samples transforms,
skeletal poses, morph weights and curves immediately through the existing
animator evaluators. Skeletal targets require a valid `SkeletonBinding`.

During an active interval, Timeline owns the target animator clock. Pausing
holds its pose. Leaving the interval, rebinding or releasing the instance
restores the previous animator state; a target without a previous animator
loses its temporary evaluator. Restored bindings receive a fresh runtime
generation. Competing instances and overlapping animation tracks on one target
are rejected. Intervals exclude their end unless it is the root timeline's
final sample, allowing adjacent clips to hand over at their shared boundary.
Physics root-motion ownership must be disabled on a Timeline-driven animator.

Clip keys join the bounded Timeline event queue, following named event tracks
at equal times within a source sequence. They dispatch `OnAnimationEvent` with
the clip identifier, event identifier, source normalized time, layer `Timeline`
and state `Clip`. Forward/reverse playback, loops and emitting skips preserve
crossed keys; silent seeks and suppressing skips do not dispatch them. Clip
reload also refreshes cached event keys and source interval validation.
