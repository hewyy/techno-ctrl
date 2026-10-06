# MIDI Control Mapping Requirements

## Goal

Build a MIDI input and mapping layer that separates physical controller input
from sequencer behavior.

Turning a knob does not directly control a parameter. It produces a MIDI input
event. The mapping layer interprets that event using configuration and current
context, then emits zero or more semantic actions.

```text
Endless encoder movement
        |
        v
REAPER MIDI input
        |
        v
detect and decode control event
        |
        v
evaluate mappings and context
        |
        v
emit zero or more semantic actions
        |
        v
registered sequencer targets
```

This separation must eventually allow one control event to update a parameter,
invoke a UI action, or change several parameters. The MVP implements only one
knob-to-parameter route.

## MVP

The MVP must:

- Receive MIDI from REAPER.
- Detect endless-encoder Control Change messages from the initial controller.
- Identify an encoder source by MIDI channel and CC number.
- Decode the configured INC/DEC format into a logical signed movement.
- Match the event against a versioned JSON mapping configuration.
- Resolve one stable sequencer parameter target.
- Apply the movement through the parameter's existing editing API.
- Persist the mapping configuration with plug-in state.
- Work during playback and when the editor is closed.

The initial proof mapping is:

```text
MIDI channel 15 / CC20 endless encoder
  -> Synth 2 -> Pattern 1 -> Pitch -> Step 1
```

The initial JSON configuration uses MIDI channel 15 and CC20. These remain
configuration values rather than hardcoded behavior in the mapping engine.
JSON MIDI channels use the human-facing one-based range `1..16`.

For this target, `+1` performs one normal pitch-edit increment and `-1`
performs one decrement. The existing chromatic or scale-aware pitch rules are
authoritative. Only Synth 2 Pattern 1 Pitch Step 1 may change.

Names such as `knob-a` are configuration metadata for people and future device
profiles. The MIDI wire identity is message type, channel, and CC number.

## Architecture

Use four distinct stages.

### 1. MIDI input adapter

The JUCE/plug-in adapter reads incoming MIDI and produces a compact control
event containing:

- Message type.
- MIDI channel.
- Control number.
- Raw value.
- Sample position or event order.

This stage knows nothing about sequencer parameters or UI controls.

### 2. Value decoder

The decoder converts the raw device message into a device-independent value.
For the MVP, the output is a signed logical increment such as `-1` or `+1`.

The MVP value mode is named `inc-dec`; it must not be represented as a generic
`relative` Boolean. Its wire decoding is:

```text
CC value 1   -> logical increment +1
CC value 127 -> logical increment -1
all other values -> ignored
```

The decoder must preserve the distinction between the raw MIDI byte and the
logical signed movement supplied to the mapping resolver.

The model must allow future decoders for absolute knobs and other relative
encoder formats without changing the mapping or target APIs. Absolute input is
not implemented in the MVP.

### 3. Mapping resolver

The resolver receives a decoded control event plus a read-only snapshot of the
current mapping context. It evaluates the JSON configuration and emits zero or
more target commands.

```text
resolve(controlEvent, context) -> targetCommands[]
```

The resolver must not contain synth-specific logic. It matches source and
conditions, applies route transforms, and identifies semantic targets.

The MVP accepts exactly one route per enabled mapping and requires an empty
`when` object. Non-empty conditions and additional routes are rejected as
unsupported in version 1. The data types must still permit those features to be
added later without changing this pipeline.

### 4. Target registry and dispatcher

Targets are registered under stable semantic IDs. A resolved command contains
a compact runtime target handle and logical value, never a raw pointer, UI
component address, or container index.

The dispatcher invokes the target's existing editing behavior. Target-specific
rules—including clamping, quantization, pitch scale rules, edit locks, draft
ownership, and modified-state tracking—remain owned by the target adapter.

A future UI target must represent a semantic action such as
`ui.focusNextPattern`, not a direct call to a UI component. No UI targets are
implemented in the MVP.

## JSON configuration

The editable configuration is a versioned JSON document. Once loaded, it is
validated, resolved into a runtime table, and embedded in plug-in state so a
REAPER project remains portable. The processor must expose a non-realtime
`replaceMappingConfiguration(jsonText)`-style API for loading the document.
File import/export and a full mapping-management UI may be added later.

A new plug-in instance starts with the example mapping below as its bundled
default configuration. Calling the replacement API with a valid document
atomically replaces that configuration. A syntactically valid version 1
document may install its valid mappings while disabling invalid entries. If the
JSON is malformed or its top-level version is unsupported, the API returns an
error and leaves the current configuration active.

Example:

```json
{
  "version": 1,
  "mappings": [
    {
      "id": "mvp-controller-knob-a",
      "enabled": true,
      "source": {
        "controlId": "knob-a",
        "type": "cc",
        "channel": 15,
        "control": 20,
        "valueMode": "inc-dec"
      },
      "when": {},
      "routes": [
        {
          "target": "synth.2.pattern.1.pitch.step.1",
          "operation": "increment",
          "scale": 1
        }
      ]
    }
  ]
}
```

`when` is intentionally present but must be empty in version 1. It reserves the
correct location for future context conditions such as held notes, modifier
state, active sequencer page, mode, or bank.

Version 1 accepts MIDI channels `1..16`, CC numbers `0..127`, `type: "cc"`,
`valueMode: "inc-dec"`, `operation: "increment"`, and a nonzero integer
`scale` in `-127..127`. Mapping IDs must be unique. Only one enabled mapping may
own a given `(type, channel, control)` source in the MVP.

Unknown fields are ignored for forward compatibility. Missing required fields,
invalid ranges, unsupported values, duplicate IDs, conflicting sources, and
unknown targets disable the affected mapping. All entries participating in a
duplicate-ID or source conflict are disabled. Invalid mappings must not prevent
other valid mappings or the rest of the plug-in state from loading.

## Future context and fan-out

The architecture must support, without redesign:

```text
Knob A + default context -> cutoff
Knob A + Shift held      -> decay
Knob B + default context -> pitch and velocity
```

Context is structured state, not one optional Boolean flag. Future context may
include Boolean modifiers, held MIDI notes, enumerated modes, active sequencer
pages, and numeric banks. All matching routes may emit commands, allowing one
control event to change multiple registered targets.

These context behaviors and multi-target mappings are not implemented in the
MVP, but the event, JSON, resolver, and runtime-table types must not assume one
global flag or exactly one route.

## Parameter boundary

Mappings may target only parameters or semantic actions registered by this
sequencer. They do not directly address REAPER parameters or external devices.

If the sequencer later controls an external device, that capability must first
be represented as a registered sequencer target. The MIDI mapping layer still
routes to that target and remains unaware of the external device.

## Realtime safety

Incoming MIDI is inspected once in `processBlock`. The audio-thread path must
be fixed-capacity, bounded, allocation-free, lock-free, and non-blocking. It
must not parse JSON, resolve strings, serialize state, access UI objects, log
per event, perform file I/O, or wait for another thread.

JSON validation and stable-ID resolution occur outside the audio thread. A
complete immutable or fixed-capacity runtime mapping table is then published
atomically.

Unsafe target edits must be accumulated in a bounded mailbox and applied by a
processor-owned non-audio-thread dispatcher. Accumulated increments must
saturate rather than wrap, and overload must never create an unbounded queue.
Movements for the same runtime target may be coalesced as a saturating signed
sum; the dispatcher applies the resulting magnitude as normal editing
increments. This intentionally represents net movement since the last drain.

The runtime design must reserve capacity for at least 32 mappings and 8 routes
per mapping even though the MVP activates only one route.

## MIDI routing

- A MIDI message matching an enabled mapping source is consumed, including a
  matching CC whose value the configured decoder ignores.
- Unmapped MIDI follows the plug-in's existing input policy.
- Existing generated MIDI output remains unchanged.
- Incoming MIDI is inspected before the plug-in prepares generated output.
- MIDI pass-through is outside the MVP.

## Feedback extension point

Controller LED feedback is not implemented in the MVP. No feedback interface,
JSON field, MIDI output, or device-specific feedback behavior is required.

A future BCR32 profile can observe resulting parameter values and active
context, convert them to absolute values, and route them through REAPER to the
controller. Adding feedback must not change control-event detection, mapping
resolution, or target dispatch.

## Persistence

Persist the validated versioned JSON configuration with plug-in state. Do not
persist runtime handles, pending events, or pending increments. Restoring older
state with no mapping configuration produces no active mapping; it does not
silently install the bundled default used by a new plug-in instance.

During state restoration, malformed JSON or an unsupported mapping version
also produces no active mapping without preventing the remaining plug-in state
from loading.

State restoration must publish either the old complete runtime table or the new
complete table; the audio thread must never observe a partially restored
mapping.

## Tests

Add focused tests covering:

- Detection of the configured channel and CC number.
- Rejection of other channels, CC numbers, and message types.
- CC value `1` decoding to `+1` and CC value `127` decoding to `-1`.
- Other CC values being ignored by the `inc-dec` decoder.
- JSON parsing, validation, versioning, and state restoration.
- Failed live replacement leaving the previous configuration active.
- Duplicate IDs and conflicting sources being disabled.
- Non-empty conditions and multiple routes being rejected in version 1.
- Stable target-ID resolution before runtime publication.
- `+1` and `-1` applying one existing pitch-edit increment in the corresponding
  direction and changing only Synth 2 Pattern 1 Pitch Step 1.
- Existing pitch clamping, scale, locking, draft, and modified-state behavior.
- Operation during playback and with the editor closed.
- Consumed controller input not leaking into generated MIDI.
- Saturating, bounded behavior under dense input.
- Coalesced input applying bounded net movement without integer wrap.
- No allocation, locks, waits, JSON work, or string lookup on the audio thread.
- Runtime data types accepting multiple routes and structured conditions even
  though those behaviors are not active in the MVP.

Run the complete existing test suite in addition to the new tests.

## Out of scope

- Context evaluation and modifier behavior.
- Multiple active routes from one control.
- UI-action targets.
- Absolute encoders, pickup, and soft takeover.
- Controller LED feedback.
- Direct control of REAPER or external-device parameters.
- MIDI pass-through.
- 14-bit CC, NRPN, and RPN.
- A global mapping-management UI.
- Changes to sequencer timing or runtime graph topology.

## Completion criteria

The MVP is complete when an endless-encoder event from the initial controller
is detected, decoded, matched through the JSON mapping layer, and applied to
the configured Synth 2 pitch parameter while preserving existing editing
rules. The implementation must remain realtime-safe, survive save and restore,
and leave clear extension points for context, fan-out, UI actions, absolute
controllers, device profiles, and feedback.
