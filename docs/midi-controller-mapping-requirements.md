# MIDI Controller Mapping Requirements

## Implementation prompt

Design and implement a general MIDI controller-mapping layer, then expose one
initial mapping target:

```text
Synth 2 -> Pattern 1 -> Pitch modulation -> Step 1
```

The initial control source is an endless rotary encoder that transmits relative
MIDI CC messages. The MIDI channel and CC number must be learned from incoming
MIDI; neither may be hardcoded.

The mapping architecture must allow absolute knobs, additional parameters, and
button-driven actions such as Play, Pause, Mute, and Reset to be added later
without replacing the mapping model. Do not implement those future controls in
this work.

Preserve the realtime-safe architecture. Incoming host MIDI is presented to the
plug-in on the audio thread, but the audio thread must not allocate, lock, spin,
wait, serialize state, touch UI objects, or perform unbounded work.

Before changing code:

1. Inspect the current plug-in MIDI declarations, `processBlock`, Synth 2 pitch
   accessors, modulation draft editing, state persistence, editor refresh path,
   and processor tests.
2. Produce a concise implementation plan identifying the audio-thread and
   non-audio-thread responsibilities.
3. Confirm the relative CC encoding used by the target controller, or support an
   explicit user-selectable encoding from the required set below.
4. Account for the uncommitted PatternPlayer and processor work already present
   in the worktree. Do not overwrite or reformat unrelated changes.

## 1. Initial feature scope

The first release must allow the user to:

1. Open the focused Synth 2 page.
2. Start MIDI Learn for Pattern 1, Pitch modulation, Step 1.
3. Turn an endless rotary encoder.
4. Capture the encoder's MIDI channel and CC number.
5. Turn clockwise to raise the stored pitch.
6. Turn counterclockwise to lower the stored pitch.
7. Relearn or clear the mapping.
8. Save the host project and restore the mapping later.

The learned controller edits the existing pitch modulation draft. It must behave
like an edit made with the existing on-screen pitch control; it is not a separate
live pitch offset or performance override.

Only this destination is exposed for learning in the initial UI:

```text
Synth 2 pattern slot: 0 (displayed as Pattern 1)
Modulation lane:      Pitch
Modulation step:      0 (displayed as Step 1)
```

Other Synth 2 patterns, modulation steps, and parameters must remain unchanged.

## 2. Separation from the sequencer core

MIDI controller mapping belongs outside the sequencer core.

The core must not know about:

- MIDI channels or CC numbers.
- MIDI Learn state.
- Controller manufacturers or models.
- Relative encoder protocols.
- UI component identities.
- Persisted controller assignments.

Use a controller-mapping layer with a thin JUCE MIDI adapter in the plug-in
layer. The mapping layer may invoke processor-level target adapters, which in
turn use the existing pitch-editing API and sequencer models.

Do not modify PatternPlayer timing or runtime graph topology to implement this
feature.

## 3. Stable control targets

A mapping destination must use a stable semantic identifier rather than a
current `players_` vector index, raw pointer, or UI component address.

The initial target is conceptually:

```text
synth2.pattern1.pitch.step1
```

The exact stored spelling may differ, but it must be versioned, stable across
sessions, and independent of runtime container ordering. At dispatch time, the
processor resolves it through the existing Synth 2 pattern/lane accessors.

The general target description must be capable of expressing:

- A stable identifier.
- A user-facing name.
- A target category.
- Its accepted input shape.
- Its execution policy.

The only enabled target category in this work is a relative continuous
parameter edit. The model must remain extensible to these future categories:

- Absolute continuous parameter.
- Trigger action.
- Toggle action.
- Press/release gate action.
- Realtime or quantized command.

Do not implement future action targets as placeholders that alter behavior.
They only need to be representable without redesigning the initial mapping
record.

## 4. MIDI source and mapping model

Each persisted mapping must contain at least:

- Enabled state.
- MIDI message kind.
- MIDI channel.
- CC number.
- Value interpretation/encoder protocol.
- Stable target identifier.

Initial validation limits are:

```text
MIDI channel: 1 through 16
CC number:    0 through 127
```

MIDI Learn captures an exact channel and CC number. Omni-channel mappings are
not required initially, but the representation must not make them impossible to
add later.

Use a fixed-capacity or immutable published runtime mapping table. Runtime MIDI
matching must not search strings, allocate, or traverse dynamically growing
containers. Resolve stable identifiers to compact runtime handles before a
mapping becomes active on the audio thread.

Choose a named mapping-capacity limit of at least 32 assignments. Only one
target is exposed initially, but the realtime path must remain bounded when ten
or more controls are moving together in future releases.

## 5. Relative encoder decoding

The controller protocol is part of the mapping. Do not assume that every CC
value is an absolute position and do not infer a protocol continuously while a
mapping is active.

Support an explicit relative mode matching the target hardware. Prefer
supporting these common modes if the controller has not yet been fixed:

### 1/127 mode

```text
CC value 1   -> delta +1
CC value 127 -> delta -1
```

Values not defined by this mode must be ignored safely.

### Two's-complement mode

Decode the CC byte into a signed relative delta according to the controller's
documented two's-complement convention. Preserve accelerated magnitudes rather
than reducing every event to its sign.

### Binary-offset/signed mode

Decode values around the protocol's neutral point into signed relative deltas.
The exact neutral value and direction must be explicit and covered by tests; do
not guess them from the control's recent movement.

If only one protocol is implemented in the initial change, it must be named
explicitly in the mapping data and documentation. It must not be represented as
a generic `relative` Boolean.

The decoder output presented to the target layer is a signed logical delta. The
pitch target must not understand raw MIDI encoding.

Reserve a distinct value-interpretation type for future absolute 7-bit CC
input. Absolute knob behavior, pickup, and soft takeover are out of scope.

## 6. Pitch-edit semantics

Each positive logical delta advances the target pitch by one editing increment.
Each negative logical delta moves it down by one editing increment.

Pitch edits must reuse the existing pitch behavior:

- Chromatic mode moves by semitone.
- Scale-aware mode moves to the next or previous permitted scale note.
- A magnitude greater than one applies that many editing increments.
- Pitch remains bounded to MIDI notes 0 through 127.
- Further movement at a boundary leaves the boundary value unchanged.
- The selected modulation-library record remains immutable; the mapping edits
  the player's draft.
- The normal modified/unsaved indicator must update.
- If the pitch lane is locked against editing, controller input must not modify
  it.

The target is always Pattern 1 Pitch Step 1 for this implementation. It must not
follow the currently playing modulation step and must not move to another step
as playback advances.

## 7. MIDI Learn behavior

The initial pitch control must expose these actions:

- MIDI Learn Encoder.
- Relearn, when a mapping already exists.
- Clear MIDI Mapping.
- Select or display the active relative encoder mode.

While Learn is armed:

1. The next eligible incoming CC captures its channel and controller number.
2. Non-CC MIDI messages do not complete learning.
3. The learned source replaces any previous mapping for this target.
4. Learning ends after a successful capture or explicit cancellation.
5. The editor shows the captured channel, CC number, and encoder mode.

MIDI Learn state is transient and must not be restored as armed when a project
is reopened.

Learning may require UI notification, but the audio thread must only publish a
small fixed-size capture result. It must not modify menus, labels, or persistent
state directly.

## 8. Realtime event flow

Incoming MIDI supplied to `processBlock` must be handled in one bounded pass.

The audio-thread path is limited to:

1. Recognizing eligible MIDI CC messages.
2. Performing constant-time or bounded mapping lookup.
3. Decoding the configured relative format.
4. Accumulating the signed delta in a fixed-capacity mailbox.
5. Publishing a fixed-size Learn capture when Learn is armed.
6. Applying the defined input-consumption policy.

The audio thread must not:

- Allocate or free memory.
- Acquire a mutex.
- Spin or wait for a revision writer.
- Serialize or parse JSON.
- Call editor or UI component methods.
- Perform file I/O.
- Log once per controller event.
- Rebuild or publish runtime graph topology.
- Invoke any existing draft-writing path that can spin against a concurrent
  writer.

Relative movement must be coalesced with a bounded signed accumulator:

```text
pendingDelta += decodedDelta
```

The accumulator must use defined saturation or overflow handling. It must not
wrap. A burst of messages therefore becomes one bounded target update rather
than filling an unbounded queue.

A processor-owned non-audio-thread dispatcher drains pending controller edits
and invokes the existing pitch-edit behavior. It must:

- Continue operating when the editor is closed.
- Avoid depending on a particular editor instance.
- Coalesce repaint or notification work.
- Remain safe during processor destruction and state restoration.
- Define what happens if input arrives faster than dispatch; the latest bounded
  accumulated movement must remain valid.

The implementation may use the application's message thread or another
processor-owned control path, but it must not create an unbounded thread or one
thread per mapping.

## 9. MIDI input and output behavior

Enable host MIDI input for the plug-in and report that the processor accepts
MIDI.

The initial routing policy is:

- A learned CC used by the mapping is consumed.
- It is not copied into generated MIDI output.
- Unmapped incoming MIDI is discarded, preserving the plug-in's existing role
  as a MIDI/CV sequence generator rather than adding MIDI-through behavior.
- Existing generated note, CC, and CV output remains unchanged.

Do not clear incoming MIDI before it has been inspected. After input handling,
ensure the outgoing buffer cannot retain consumed controller messages.

MIDI pass-through and configurable filtering are separate future features.

## 10. Persistence and compatibility

Persist controller mappings with the plug-in state. Evolve the current state
schema compatibly; older state versions must load with no controller mapping.

Persist at least:

- Mapping enabled state.
- MIDI channel.
- CC number.
- Named encoder protocol.
- Stable target identifier.

Do not persist:

- Armed Learn state.
- Pending encoder deltas.
- Pending UI notifications.
- Runtime target handles.

On restoration:

- Validate every field before activating a mapping.
- Resolve the stable target identifier through the target registry.
- Ignore unknown future target identifiers safely.
- Reject unsupported encoder modes without rejecting the rest of the plug-in
  state.
- Do not partially activate a malformed mapping.

State restoration and mapping publication must not race the audio thread or
expose a partially updated mapping.

## 11. UI requirements

Limit the initial UI change to the existing control for Synth 2 Pattern 1 Pitch
Step 1.

The UI must make these states distinguishable:

- No mapping.
- Waiting for MIDI Learn input.
- Learned and active.
- Learned but unsupported/invalid after state restoration, if surfaced rather
  than discarded.

Display enough information to diagnose the assignment, for example:

```text
MIDI Ch 1 / CC 21 / Relative 1-127
```

Do not add a global mapping-management page in this work. Do not add Learn
controls to every parameter yet.

The existing pitch display, scale editing, modulation selection, save behavior,
and modified-state indication must continue to work.

## 12. Conflict and failure policy

For the initial implementation:

- One source maps to at most one active target.
- The initial target has at most one active source.
- Relearning atomically replaces the old assignment.
- Clearing atomically disables the assignment.
- Invalid MIDI channels, CC numbers, target identifiers, or encoder modes are
  rejected.
- Unsupported incoming messages are ignored.
- Saturated pending deltas fail safely without integer wrap.
- A missing or shortened modulation step causes the edit to be ignored safely.
- Controller input received during state restoration must observe either the old
  complete mapping or the new complete mapping, never partial state.

## 13. Tests

Add focused tests for the generic mapping layer and processor integration.

### Decoder and mapping tests

- MIDI Learn captures the correct channel and CC number.
- Non-CC messages do not complete learning.
- The configured relative protocol produces correct positive and negative
  deltas.
- Accelerated relative values preserve their magnitude where supported.
- Invalid values are ignored or rejected according to the selected protocol.
- Mapping lookup distinguishes MIDI channels and CC numbers.
- Relearn replaces the prior mapping.
- Clear disables the mapping.
- Delta accumulation saturates and never wraps.

### Pitch integration tests

- Clockwise movement raises Synth 2 Pattern 1 Pitch Step 1.
- Counterclockwise movement lowers it.
- A multi-increment delta applies multiple edits.
- Chromatic mode moves by semitone.
- Scale-aware mode moves through permitted scale notes.
- Notes remain clamped to 0 through 127.
- Pattern 2, Pattern 3, and other modulation steps do not change.
- A locked pitch lane does not change.
- The selected library record is not mutated.
- The draft reports unsaved changes after a successful controller edit.
- The mapping operates while playback is running.
- The mapping operates while the editor is closed.

### MIDI-buffer and persistence tests

- The learned incoming CC does not appear in outgoing MIDI.
- Unmapped incoming MIDI follows the defined discard policy.
- Existing generated MIDI output remains correct.
- A saved mapping restores with the correct source, protocol, and target.
- Older plug-in state loads with no active mapping.
- Malformed or unknown mappings do not prevent the remaining state from loading.

### Realtime verification

- The input path performs no heap allocation.
- The input path takes no locks and performs no waits or spins.
- Dense controller input remains bounded.
- Ten simultaneously active mappings are supported by the internal runtime path,
  even though only one target is exposed by the initial UI.

Run the complete existing test suite in addition to the new tests.

## 14. Explicitly out of scope

Do not include any of the following in this implementation:

- Absolute knob control.
- Pickup or soft takeover.
- Learn controls for additional parameters or modulation steps.
- MIDI-controlled Play, Pause, Mute, Reset, or pattern selection.
- Incoming MIDI pass-through.
- Multiple controllers for one target.
- One controller driving multiple targets.
- 14-bit CC, NRPN, or RPN.
- MIDI feedback to rings, LEDs, or motorized controls.
- Per-controller acceleration curves or sensitivity settings.
- Fine/coarse modifier buttons.
- Live pitch override, hold, or drift-back behavior.
- Recording controller gestures into a modulation pattern.
- Controller templates or a global mapping-management screen.
- Changes to PatternPlayer timing, pattern storage, or runtime graph topology.

## 15. Completion criteria

The work is complete when:

- The plug-in accepts host-routed MIDI input.
- Synth 2 Pattern 1 Pitch Step 1 can learn a relative MIDI CC source.
- No MIDI channel or CC number is hardcoded.
- The encoder mode is explicit and persisted.
- Clockwise and counterclockwise movement produce correct pitch edits.
- Existing scale-aware rules, locking, draft ownership, and modified-state
  behavior are preserved.
- The mapping works during playback and with the editor closed.
- The mapping survives save and restore.
- Learned input is not leaked to MIDI output.
- The audio-thread work is fixed-capacity, bounded, allocation-free, lock-free,
  and non-spinning.
- The mapping and target models can add absolute controls and button actions
  without a state-format or architecture replacement.
- Existing and new tests pass.

## 16. Required hardware detail before final integration

Record the target controller's model and the raw values produced by:

```text
one slow clockwise detent
one slow counterclockwise detent
several fast clockwise turns
several fast counterclockwise turns
```

Use those observations or the manufacturer's MIDI documentation to select and
test the initial relative protocol. Do not ship a guessed encoder convention.
