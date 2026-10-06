# Page-Relative MIDI Mapping Refactor Requirements

## Purpose

Refactor MIDI mapping around reusable, page-relative control slots rather than
requiring a separate mapping for every concrete sequencer parameter.

A physical controller mapping identifies a numbered logical control such as
`control slot 1` or `control slot 2`. The processor-owned editing page and its
current context determine what that slot controls.

```text
MIDI channel 15 / CC21
        |
        v
logical control slot 2
        |
        v
active page binding snapshot
        |
        v
concrete parameter or parameter-step handle
        |
        v
bounded mailbox and generic dispatcher
```

This allows one controller layout to edit every compatible synth, voice,
pattern, parameter, and parameter step without enumerating hundreds of
source-to-parameter mappings.

This document extends `docs/midi-controller-mapping-requirements.md`. Existing
version 1 configuration and persistence behavior remains authoritative unless
this document explicitly changes it.

## Design summary

The refactor separates three concepts that must not be conflated:

1. **Device mapping** converts a MIDI source into one or more numbered logical
   control slots.
2. **Page binding** converts a control slot into one concrete target using the
   processor's current editing context.
3. **Target dispatch** applies the movement through the concrete parameter's
   existing editing behavior.

MIDI configuration does not need to name `cutoff`, `decay`, `pitch`, a voice,
or a pattern. Those identities remain in page profiles and the sequencer model.

For example:

```text
Controller layout
  CC20 -> control slot 1
  CC21 -> control slot 2

Pattern parameter overview
  control slot 1 -> parameter slot 1 -> Pitch Step 1
  control slot 2 -> parameter slot 2 -> Velocity Step 1

Synth parameter overview
  control slot 1 -> parameter slot 1 -> Cutoff Step 1
  control slot 2 -> parameter slot 2 -> Decay Step 1

Parameter-step editor for selected parameter X
  control slot 1 -> parameter X Step 1
  control slot 2 -> parameter X Step 2
```

The same physical mappings survive page, synth, voice, pattern, parameter, and
step-bank changes. A page change publishes a new complete binding snapshot; it
does not replace or reparse the MIDI configuration.

## Current baseline

The current implementation already provides:

- MIDI input detection and `inc-dec` decoding.
- Versioned JSON configuration and project-state persistence.
- Validation and stable-ID resolution outside the audio thread.
- Atomic publication of a fixed-capacity runtime mapping table.
- Compact target handles.
- A saturating, coalescing, lock-free target mailbox.
- A processor-owned non-audio-thread dispatcher.
- Two bundled direct mappings:
  - MIDI channel 15 / CC20 to Synth 2 Pattern 1 Pitch Step 1.
  - MIDI channel 15 / CC21 to Synth 2 Pattern 1 Velocity Step 1.

The current implementation has four deliberate MVP limitations:

- Mapping routes point directly at individual parameters.
- Targets are registered through individually named C++ constants.
- The dispatcher contains one branch per registered semantic target.
- Every incoming event scans all active mappings.

Do not extend those shortcuts to more parameters.

## Goals

The refactor must:

1. Map physical MIDI sources to numbered logical control slots.
2. Represent every editable page with a reusable page profile.
3. Represent parameter positions as numbered parameter slots rather than MIDI
   configuration names.
4. Support a deeper parameter-step context in which control slots edit the
   steps of the selected parameter.
5. Publish a complete immutable page-binding snapshot whenever editing context
   changes.
6. Resolve each MIDI event to a concrete target handle at event time.
7. Replace individually named target constants and dispatcher branches with a
   generated target catalogue and generic adapter types.
8. Index mappings directly by MIDI source rather than scanning all mappings.
9. Prevent accidental multi-target edits through validation and single-owner
   page bindings.
10. Preserve intentional fan-out through explicit multiple routes in a device
    mapping.
11. Preserve realtime safety, playback behavior, editor-closed operation,
    project portability, and all version 1 behavior.

## Non-goals

This refactor does not require:

- A mapping-management UI.
- File import or export.
- MIDI learn.
- Absolute encoders, pickup, soft takeover, NRPN, RPN, or 14-bit CC.
- Controller LED feedback.
- Direct control of REAPER or external-device parameters.
- Runtime graph topology changes.
- Mapping pattern-hit creation, deletion, or movement.
- Mapping transport, mute, selection, or other UI actions.
- Replacing existing sequencer editing APIs.

Future semantic-action targets may reuse the control-slot and page-binding
pipeline without changing its source mapping API.

## Terminology

### Physical source

A wire-level MIDI identity such as Control Change, channel 15, controller 21.
`controlId` remains optional human-readable metadata and is not part of wire
identity.

### Control slot

A one-based logical position in a controller/page layout. Control slots express
physical placement, not parameter meaning.

```text
control slot 1
control slot 2
...
control slot 64
```

JSON uses one-based slot numbers. Runtime arrays use zero-based indices.

### Parameter slot

A one-based position in a page profile's parameter layout. A parameter slot is
bound by a specific synth, voice, or pattern profile to a concrete parameter.

```text
parameter slot 1 -> profile-defined parameter
parameter slot 2 -> profile-defined parameter
```

MIDI configuration does not contain the concrete parameter name.

### Page profile

An immutable description of a reusable editor layout. It defines:

- A stable page-profile ID.
- Page kind.
- The available parameter slots.
- How control slots are interpreted for that page kind.
- The concrete parameters supplied by a particular subject profile.
- Normal edit behavior and supported step count through parameter definitions.

The UI and MIDI page-binding builder must consume the same profile information
so visual control position and hardware control position cannot drift apart.

### Editing context

Processor-owned state identifying the current page, subject, selected
parameter, and step bank. It contains stable IDs or validated handles, never UI
component pointers.

### Page-binding snapshot

A complete fixed-capacity table that resolves every control slot to zero or one
concrete target handle for the current editing context.

An invalid handle means that the slot is unbound and must do nothing.

### Concrete target

An internal descriptor for one editable value, such as a player modulation
step or a device-parameter modulation step. Concrete targets are generated from
the sequencer model and are not normally named by version 2 MIDI configuration.

## Required pipeline

```text
incoming MIDI message
    -> MIDI input adapter
    -> decoder
    -> direct source bucket lookup
    -> zero or one matching device mapping
    -> one or more logical control-slot commands
    -> current immutable page-binding snapshot
    -> concrete target commands
    -> saturating per-target mailbox
    -> processor-owned non-audio dispatcher
    -> generic target adapter
    -> existing editing API
```

Each stage remains independently testable.

## Device mapping configuration

### Version 1 compatibility

Continue accepting version 1 exactly as specified by
`midi-controller-mapping-requirements.md`.

- Version 1 target strings continue resolving to concrete handles.
- Version 1 still requires empty `when` and exactly one route.
- Version 1 source-conflict behavior remains unchanged.
- Existing project state round-trips without conversion or normalization.
- Older state with no mapping configuration still restores no active mapping.
- The current direct Pitch Step 1 and Velocity Step 1 mappings remain valid.

The generated target catalogue exists partly to preserve version 1 direct
target behavior.

### Version 2 shape

Version 2 maps MIDI sources to control slots instead of concrete parameters.

```json
{
  "version": 2,
  "mappings": [
    {
      "id": "controller-encoder-1",
      "enabled": true,
      "source": {
        "controlId": "encoder-1",
        "type": "cc",
        "channel": 15,
        "control": 20,
        "valueMode": "inc-dec"
      },
      "when": {},
      "routes": [
        {
          "controlSlot": 1,
          "operation": "increment",
          "scale": 1
        }
      ]
    },
    {
      "id": "controller-encoder-2",
      "enabled": true,
      "source": {
        "controlId": "encoder-2",
        "type": "cc",
        "channel": 15,
        "control": 21,
        "valueMode": "inc-dec"
      },
      "when": {},
      "routes": [
        {
          "controlSlot": 2,
          "operation": "increment",
          "scale": 1
        }
      ]
    }
  ]
}
```

Version 2 accepts:

- MIDI channels `1..16`.
- CC numbers `0..127`.
- `type: "cc"`.
- `valueMode: "inc-dec"`.
- `operation: "increment"`.
- Nonzero integer `scale` in `-127..127`.
- Control slots `1..64`.
- One to eight routes per mapping.

Multiple routes are the only device-configuration mechanism for intentional
fan-out. Each route names a control slot, not a concrete target.

Unknown fields outside `when` are ignored for forward compatibility. Invalid
routes disable the affected mapping. Duplicate mapping IDs disable all entries
with that ID.

The live replacement API preserves a valid submitted document exactly,
including disabled entries and unknown fields. Malformed JSON or an unsupported
top-level version leaves the previous live configuration active. Malformed or
unsupported mapping state publishes an empty configuration while allowing the
rest of valid plug-in state to restore.

### Optional controller-local context

Version 2 `when` conditions are reserved for controller-local state that can
change which logical slot a physical source represents. Editing page, subject,
selected parameter, and step bank do not belong in JSON `when`; they are
processor-owned editing context.

Supported controller-local conditions are:

- `modifiers`: up to 16 named Boolean values.
- `notes`: up to 16 `{ "note": 0..127, "held": Boolean }` entries.
- `bank`: integer `0..127`.

Example:

```json
"when": {
  "modifiers": { "shift": true },
  "bank": 2
}
```

Named conditions are interned to collision-safe compact IDs outside the audio
thread. Runtime conditions contain no strings.

An absent dimension is a wildcard. Two mappings for the same physical source
are valid only when their controller-local predicates are provably mutually
exclusive. Empty `when` overlaps every other mapping for that source.

Examples of mutual exclusion include:

```text
shift=false versus shift=true
bank=1 versus bank=2
note 60 held versus note 60 released
```

All mappings participating in an overlapping predicate group are disabled.
At runtime, zero matches emits no slot command, one match emits its routes, and
more than one match fails closed and emits nothing.

Unknown fields inside `when` disable the affected mapping. Unsupported
conditions must never be ignored in a way that broadens a mapping.

## Editing context

The processor owns editing context independently of editor component lifetime.
The exact C++ spelling may vary, but it must be equivalent to:

```cpp
enum class PageKind : std::uint8_t
{
    parameterOverview,
    parameterSteps
};

struct EditingContext
{
    StablePageProfileId profile;
    PageKind pageKind;
    StableSubjectId subject;
    std::uint16_t selectedParameterSlot;
    std::uint16_t stepBank;
    bool valid;
};
```

The context may be extended with additional page kinds without changing MIDI
source mappings.

UI code requests context changes through processor APIs. It must not publish UI
pointers, component addresses, display strings, or partially updated state.

### Editor-closed behavior

Closing the editor does not invalidate editing context. The processor retains
the last explicitly selected valid context, so hardware control continues to
operate with the editor closed.

A new plug-in instance has a deterministic default editing context matching the
initial page the editor would display. For the current product this context
must preserve the initial proof behavior:

```text
control slot 1 -> Synth 2 Pattern 1 Pitch Step 1
control slot 2 -> Synth 2 Pattern 1 Velocity Step 1
```

Editing context is persisted using stable page-profile and subject IDs. Do not
persist concrete target handles or page-binding arrays.

If restored editing context references an unknown profile, subject, parameter
slot, or step bank, publish an invalid page-binding snapshot. Do not silently
redirect hardware to a different parameter.

Older state without editing context retains version 1 direct-mapping behavior.
When version 2 mappings are present without restorable context, use the
documented deterministic default only for a genuinely new instance; restored
state fails safe with an invalid context.

## Page profiles

### Shared positional layout

Page profiles define parameters by numbered position. MIDI configuration does
not care whether parameter slot 1 is named Cutoff, Pitch, Sample Start, or
anything else.

Every compatible synth or voice profile may share the same visual and hardware
layout while binding its parameter slots to different concrete parameter
definitions.

Example internal profile data:

```text
profile: synth-editor
  parameter slot 1 -> subject parameter A
  parameter slot 2 -> subject parameter B
  parameter slot 3 -> subject parameter C
```

Display names remain parameter metadata used by the UI. Renaming a display
label must not change slot identity, persistent context, or MIDI behavior.

Page profiles and parameter definitions must have explicit stable keys. Do not
derive stable identity from localized display text or mutable container order.

### Parameter overview binding

For a `parameterOverview` page, the profile binds each applicable control slot
to the first or page-designated editable step of the corresponding parameter
slot.

The binding rule is page-profile data, not a synth-specific MIDI mapping.

For the current default pattern profile:

```text
parameter slot 1 -> pitch
parameter slot 2 -> velocity
parameter slot 3 -> gate
```

The initial overview page binds control slots 1 and 2 so the existing hardware
proof continues to edit Pitch Step 1 and Velocity Step 1.

### Parameter-step binding

For a `parameterSteps` page:

- `selectedParameterSlot` identifies parameter X.
- The page profile defines which control slots are step controls.
- `stepBank` selects the visible/editable group of steps.
- Each step control resolves to one concrete step of parameter X.

Conceptually:

```text
concreteStep = stepBank * stepsPerBank + pageStepPosition
```

The exact control-slot range and `stepsPerBank` come from the page profile. Do
not assume every controller has ten encoders or every page uses every slot.

Changing selected parameter or step bank publishes a new complete page-binding
snapshot. It does not mutate MIDI configuration.

A step beyond the parameter's supported maximum is unbound. A step beyond the
current modulation length is also unbound and must not resize the modulation
implicitly.

### Single-owner slot rule

Within one page-binding snapshot, one control slot resolves to at most one
concrete target handle.

This is the primary protection against a knob accidentally changing multiple
parameters. Page-profile construction must reject or leave unbound any slot
that receives conflicting bindings.

Intentional fan-out remains explicit in version 2 device mappings by routing a
physical source to several distinct control slots. A normal page profile does
not fan one slot out to multiple parameters.

## Page-binding snapshots

The runtime page binding is fixed-capacity and equivalent to:

```cpp
struct PageBindingSnapshot
{
    std::array<TargetHandle, 64> controlTargets;
    StablePageProfileId profile;
    PageKind pageKind;
    std::uint64_t generation;
    bool valid;
};
```

Snapshot construction occurs outside the audio thread whenever editing context
changes. Construction:

1. Validates the page profile and subject.
2. Resolves parameter slots through the subject profile.
3. Resolves step positions and step bank.
4. Resolves or obtains concrete target handles.
5. Validates the single-owner slot rule.
6. Publishes the entire snapshot atomically.

The audio thread observes either the complete previous snapshot or the complete
new snapshot. It must never observe a new page kind with old target bindings.

When processing a decoded slot command, the audio thread reads the target handle
from the snapshot and immediately accumulates movement for that concrete
handle. It does not enqueue an unresolved control slot.

Therefore, changing pages before the dispatcher drains cannot redirect pending
movement. The movement remains associated with the concrete handle selected
when the MIDI event arrived.

## Internal target catalogue

The concrete target catalogue remains necessary for page-snapshot construction,
generic dispatch, and version 1 compatibility. It is an internal model rather
than the primary version 2 configuration surface.

Replace individually named target constants with fixed descriptor variants
equivalent to:

```cpp
enum class TargetKind : std::uint8_t
{
    playerModulationStep,
    synthParameterStep,
    sampleParameterStep
};

struct PlayerModulationStepTarget
{
    StablePlayerId player;
    ModulationLane lane;
    std::uint16_t step;
};

struct SynthParameterStepTarget
{
    StableSynthParameterId parameter;
    std::uint16_t step;
};

struct SampleParameterStepTarget
{
    std::uint8_t part;
    StableSampleParameterId parameter;
    std::uint16_t step;
};
```

Descriptors may contain validated internal indices after construction. Runtime
commands still contain only compact handles. Do not store raw player pointers,
UI pointers, `std::function`, or owning pointers in realtime descriptors.

Generate target entries from the sequencer's player and parameter definitions.
Adding a player or parameter definition must not require a new target constant
or dispatcher branch.

The catalogue supports at least 8,192 handles. Handles are never serialized.

Version 1 target strings, including the two existing Synth 2 aliases, resolve
through this catalogue outside the audio thread.

## Generic target dispatch

The non-audio dispatcher drains concrete target handles and visits their
descriptor types. It contains no branch for an individual synth, voice,
pattern, parameter, or step.

### Pitch targets

- Apply logical units through the existing pitch-editing API.
- Preserve chromatic and scale-aware movement.
- Preserve MIDI-note clamping, locks, drafts, and modified state.

### Velocity and gate targets

- One logical unit changes the existing 8-bit edit value by one.
- Clamp to the target range.
- Apply through the existing modulation API.
- Preserve locks, drafts, and modified state.

### Synth and sample parameter targets

- One logical unit changes the existing page-editing value by one.
- Clamp to the parameter definition's range, including maxima over 127.
- Apply through the existing synth- or sample-lane API.
- Preserve existing draft and modified-state behavior.

An accumulated magnitude may use a target-owned batch operation only when its
result is behaviorally identical to repeated normal edits.

Parameter definitions own display names, ranges, normal increments, and special
editing policies. MIDI mappings and page slots do not duplicate those rules.

## Runtime source index

Do not scan all mappings for each MIDI event.

Compile a direct Control Change index for all 2,048 wire sources:

```text
16 channels × 128 CC numbers
```

Each source bucket contains an offset and count into an immutable flattened
array of device mappings. Incoming lookup is constant-time before evaluating
only the controller-local predicates in that bucket.

Audio-thread work per CC event depends on:

- One constant source lookup.
- The mappings in that source bucket.
- The explicit slot routes in the selected mapping.
- One page-snapshot array lookup per emitted slot.

It must not depend on the total number of mappings or the total number of
concrete parameters.

Reserve capacity for at least:

```text
Active device mappings:     1,024
Routes per mapping:             8
Control slots:                  64
Concrete target handles:     8,192
Controller conditions:          32 per mapping
CC source buckets:           2,048
```

Capacity overflow disables affected entries and returns non-realtime
diagnostics without preventing other valid mappings from installing.

## Mailbox and dispatcher scaling

Maintain one saturating signed accumulator per concrete target handle with the
existing `-127..127` limit.

Do not scan all target accumulators on every dispatcher tick. Add a fixed-size,
lock-free dirty-target bitset:

1. The audio thread updates a concrete target accumulator.
2. It atomically marks that target handle dirty.
3. The non-audio dispatcher drains dirty words.
4. It exchanges and applies only dirty target accumulators.

Races may defer an edit to the next drain but must not wrap, permanently lose
dirty state, create an unbounded retry loop, or apply movement to another
handle.

All audio-thread atomics must be asserted lock-free on supported builds. Every
audio-thread retry loop has an explicit finite bound.

## Runtime publication

Two independently changing immutable runtime packages exist:

1. **Device mapping package**: source buckets, compiled mappings, slot routes,
   and controller-local conditions.
2. **Page-binding snapshot**: concrete target handle for each logical control
   slot.

Each package is published atomically. An audio event may use the current
complete version of each package. It must never observe partial storage from
either one.

Device configuration replacement does not rebuild page profiles. Editing
context changes do not parse or replace device configuration.

Runtime handles, source offsets, page-binding arrays, dirty bits, pending
movements, generations, and interned condition IDs are not serialized.

## MIDI event order and context

Controller-local held-note state derived from incoming MIDI follows event order
inside the block:

- A note event earlier in the block may affect a later CC mapping.
- A later note event may not affect an earlier CC mapping.

Processor editing context is captured through the current complete
page-binding snapshot when each decoded slot command is resolved.

No UI component is accessed from the audio thread.

## Persistence

Persist:

- The exact validated version 1 or version 2 MIDI configuration JSON.
- Editing context using stable page-profile, subject, parameter-slot, and
  step-bank identities.

Do not persist:

- Concrete target handles.
- Page-binding snapshots.
- Source bucket offsets.
- Pending target movements.
- Dirty bits.
- Interned runtime IDs.

Increment the plug-in state schema when adding editing context. Continue
accepting all previously supported state schema versions.

Restoring state publishes complete device mapping and page-binding packages or
safe empty/invalid replacements. The audio thread never sees partial restored
state.

## Realtime constraints

The audio-thread MIDI path remains:

- Fixed-capacity.
- Bounded per event.
- Allocation-free.
- Lock-free.
- Non-blocking and free of waits.
- Free of JSON parsing, strings, file I/O, logging, UI access, and parameter
  editing.

The audio thread does not:

- Search parameter names.
- Search page profiles.
- Construct targets.
- Inspect mutable UI state.
- Scan all mappings.
- Scan all parameter targets.

JSON parsing, ambiguity analysis, source-index compilation, profile validation,
target-catalogue construction, and page-snapshot construction occur outside the
audio thread.

## Diagnostics

Extend non-realtime configuration and page-binding results with structured
diagnostics suitable for a future UI.

Each diagnostic contains:

- Stable error code.
- Severity.
- Mapping or page-profile ID when available.
- Human-readable message.
- Related IDs for conflicts.

At minimum, distinguish:

- Malformed documents.
- Unsupported versions.
- Invalid fields or ranges.
- Duplicate mapping IDs.
- Controller-context ambiguity.
- Invalid control slots.
- Unknown page profiles or subjects.
- Invalid parameter slots or step banks.
- Conflicting page-slot bindings.
- Unknown version 1 direct targets.
- Capacity exhaustion.

Do not log per MIDI event.

## Required tests

### Version 1 regression

- The existing CC20 direct pitch mapping retains exact behavior.
- The existing CC21 direct velocity mapping retains exact behavior.
- Existing version 1 JSON and plug-in state round-trip exactly.
- Older state with no mapping configuration restores no active mapping.
- Version 1 unknown targets disable only affected mappings.

### Version 2 device mappings

- CC20 resolves to control slot 1.
- CC21 resolves to control slot 2.
- `1` decodes to `+1`; `127` decodes to `-1`.
- Matching ignored `inc-dec` values remain consumed.
- One mapping may intentionally route to several distinct control slots.
- Invalid or out-of-range slots disable the affected mapping.
- Duplicate IDs and overlapping controller contexts disable all participants.
- Mutually exclusive modifier, note, and bank mappings are accepted.
- Runtime defensive ambiguity emits no slot commands.
- Exact valid JSON persists without normalization.
- Failed live replacement leaves the complete old package active.

### Page profiles and context

- The default new-instance context binds slot 1 to Synth 2 Pattern 1 Pitch
  Step 1 and slot 2 to Velocity Step 1.
- The same device mapping controls a different pattern after changing subject
  context.
- The same device mapping controls a different synth or voice profile without
  replacing JSON.
- Parameter overview slots use the subject profile's numbered parameter
  bindings, independent of display names.
- Changing a display name does not change MIDI behavior.
- Selecting parameter X and entering its step page maps step controls to
  parameter X's steps.
- Changing selected parameter rebuilds the complete binding snapshot.
- Changing step bank maps controls to the correct later steps.
- Unsupported steps are unbound and do not resize modulation.
- An invalid context produces a valid empty/unbound snapshot.
- Closing the editor retains the last context and operation.
- Opening or closing the editor does not access UI objects from the audio
  thread.
- Restored stable context rebuilds the same bindings with new runtime handles.

### Event-time resolution

- A knob event resolved on Page A still edits Page A's concrete target if the
  user switches to Page B before dispatcher drain.
- A later knob event after the switch edits Page B's target.
- An unbound control slot emits no target command.
- One page slot never edits two parameters accidentally.
- Explicit device-route fan-out edits only the targets bound to its listed
  slots.

### Generated target catalogue and generic dispatch

- Every current player pitch, velocity, and gate step is representable.
- Every current synth parameter step is representable.
- Every current sample-part parameter step is representable.
- Adding a parameter definition exposes targets without a named constant or
  dispatcher branch.
- Pitch editing preserves chromatic, scale-aware, clamping, lock, draft, and
  modified-state behavior.
- Velocity and gate editing preserve range, lock, draft, and modified state.
- Synth and sample parameter editing preserve parameter-specific ranges.
- Different handles never edit one another's targets.

### Source-index and scaling behavior

- Every channel `1..16` and CC `0..127` addresses the correct source bucket.
- Events inspect only mappings in their bucket.
- Adding 1,000 mappings for other sources does not increase mappings examined
  for the tested source.
- Page resolution performs direct control-slot lookup and does not depend on
  the number of parameters.
- Tests use deterministic inspected operation counts, not timing thresholds.

### Mailbox and realtime behavior

- Dense input saturates without integer wrap.
- Dirty-target drain visits only dirty handles.
- Concurrent accumulation and drain cannot permanently lose a dirty target.
- No allocation, mutex, wait, string comparison, JSON work, profile lookup,
  UI access, or parameter edit occurs on the audio thread.
- All realtime atomics are lock-free on supported builds.
- All audio-thread retry loops have finite tested bounds.

Run the complete existing test suite in addition to all new tests.

## Implementation sequence

Implement in this order:

1. Add stable subject, page-profile, and parameter-definition identities.
2. Introduce generic concrete target descriptors and generated catalogue.
3. Replace target-specific dispatcher branches with generic adapters while
   preserving all version 1 tests.
4. Add processor-owned editing context and page-profile models.
5. Build and atomically publish page-binding snapshots.
6. Add version 2 source-to-control-slot configuration.
7. Compile device mappings into direct CC source buckets.
8. Add controller-local context and ambiguity validation.
9. Add the dirty-target bitset and expanded fixed capacities.
10. Persist stable editing context and add diagnostics.
11. Migrate the bundled new-instance defaults to version 2 control-slot
    mappings while retaining version 1 state compatibility.
12. Run the complete build and test suite.

Do not remove working behavior before its page-relative replacement and
regression tests are in place.

## Completion criteria

The refactor is complete when:

- New MIDI configuration maps physical sources to numbered control slots, not
  concrete parameter names.
- One controller profile works across all compatible synth, voice, pattern,
  parameter-overview, and parameter-step pages.
- Page profiles define numbered parameter layout independently of MIDI.
- Deeper step editing uses selected parameter and step-bank context rather than
  parameter-specific MIDI mappings.
- The processor publishes complete immutable page-binding snapshots.
- Every MIDI movement is associated with a concrete target at event time.
- Page changes before dispatch cannot redirect pending movement.
- The processor contains no individually named target-handle constants.
- The dispatcher contains no semantic branch for an individual synth, voice,
  pattern, parameter, or step.
- Adding a subject or parameter definition does not require a new MIDI mapping
  or dispatcher branch.
- Runtime MIDI lookup does not scan all mappings or parameters.
- Accidental overlap fails closed; intentional fan-out is explicit.
- Version 1 projects and the existing hardware mappings retain exact behavior.
- New instances use version 2 slot mappings and preserve the initial CC20 pitch
  and CC21 velocity proof in the default context.
- Playback, editor-closed operation, state portability, bounded overload
  behavior, realtime safety, and the complete test suite remain intact.
