# Core architecture and integration guide

This document is the canonical guide to the portable sequencer under
[`src/core`](../src/core). It is written for maintainers, integrators, and coding
agents. It describes the current implementation, not a proposed architecture.

If this document and the source disagree, the source and its tests are
authoritative. Files under [`files_for_reference`](../files_for_reference) use an
older interface and are not part of any build target.

## What the core is

The core is a C++17 musical event scheduler and router. A host wrapper gives it
musical time, players turn that time into protocol-neutral events, the engine
validates and routes those events, and output renderers translate them into MIDI,
CV, or another destination.

```text
Control / setup side

  catalog storage ──> PatternLibrary / VelocityModulationLibrary
                              │
  UI or state restore ──> concrete players
                              │ borrowed registrations
                              v
Real-time side

  host transport ──> TimelineBlock ──> SequencerEngine
                                            │
                    IPlayer::process() <────┤
                           │ semantic events │
                           v                 │
                    fixed event buffers      │
                           │                 │
                           v                 │
        validate -> filter -> map pitch -> PPQ-to-frame -> stable sort
                                            │
                                            v
                                IOutputRenderer::renderBlock()
                                      │                 │
                                      v                 v
                                  MIDI events       CV/audio samples
```

The core owns musical scheduling, generic synchronization, event validation,
mute/suppression policy, route fan-out, and deterministic ordering. It does not
own a plugin format, audio device, host transport, filesystem, JSON parser, GUI,
or MIDI/CV buffer type.

The shipped JUCE VST3 wrapper under [`src/plugin`](../src/plugin) is one example
of supplying those external services. It is not required by the source in
`src/core`.

Suggested reading paths:

- embedding or replacing JUCE: [Essential contracts](#essential-contracts),
  [Runtime lifecycle](#runtime-lifecycle),
  [Running core without JUCE](#running-core-without-juce), and
  [What JUCE currently supplies](#what-juce-currently-supplies);
- changing a component: [Component map](#component-map),
  [Component reference](#component-reference), and the
  [Change-impact guide](#change-impact-guide-for-agents);
- reviewing correctness: [Shared data contracts](#shared-data-contracts),
  [Safety and real-time behavior](#safety-and-real-time-behavior), and
  [Verification and tests](#verification-and-tests).

## Essential contracts

These are the rules most likely to matter when changing or embedding the core.

1. Musical blocks and player processing ranges are half-open:
   `[ppqStart, ppqEnd)`. Pattern playback windows are inclusive:
   `[playbackStart, playbackEnd]`.
2. Register players, connect routes, and configure renderers before the first
   `SequencerEngine::prepare()`. Preparation permanently freezes topology.
3. The engine borrows players and renderers. They must outlive the engine.
   `PatternLibrary` and `VelocityModulationLibrary` must outlive every
   `PatternPlayer` that refers to them.
4. A player must emit no more than 128 events per block. Event PPQ positions
   must be finite, monotonically nondecreasing within that player, and mappable
   into the current sample block.
5. Every trigger end must reuse the `TriggerId` of its trigger start. A renderer
   should key active-trigger state by `(PlayerId, RouteId, TriggerId)`.
6. `RoutedEventView` is borrowed storage. It is valid only for the duration of
   `renderBlock()` and must not be retained.
7. Call `SequencerEngine::run()` for stopped and eventless blocks too. Players
   use those calls to close gates, and renderers such as CV need them to hold or
   clear continuous output.
8. Never perform catalog insertion, filesystem work, topology mutation, or
   state serialization on the audio thread.
9. `run()` is designed to avoid allocation only after `prepare()` has reserved
   routing storage. Calling `run()` before preparation is not a supported
   real-time path.
10. The core is protocol-neutral. MIDI note/channel choices and CV channel or
    voltage choices belong to routes and renderers, not to a player.

## Terminology

| Term | Meaning |
| --- | --- |
| PPQ | Pulse position measured in quarter notes. One quarter note is `1.0` PPQ. |
| Timeline block | One host callback represented by `TimelineBlock`. |
| Player | An `IPlayer` that emits semantic events from a timeline block. |
| Cycle boundary | A player-reported loop or pulse start that can synchronize followers. It does not require an audible hit. |
| Draft | A player's editable working state, separate from immutable library content. |
| Requested state | A control-side selection or loop waiting to become active. |
| Active state | The pattern, loop, or modulation currently used by scheduling. |
| Trigger | A paired `triggerStart` and `triggerEnd` sharing one `TriggerId`. |
| Route | A connection from one `PlayerId` to one renderer, optionally with a fixed-pitch mapping. |
| Renderer | An `IOutputRenderer` that converts ordered routed events into an external representation. |

## Component map

| Component | Files | Responsibility | Important ownership or concurrency rule |
| --- | --- | --- | --- |
| Shared contracts | [`SequencerTypes.h`](../src/core/SequencerTypes.h) | Timeline, IDs, semantic events, routing DTOs, fixed event buffer, pattern/view types | Event and routed-event structs are trivially copyable. |
| Player port | [`IPlayer.h`](../src/core/IPlayer.h) | Minimal real-time source lifecycle and synchronization capabilities | Borrowed by the engine; implementation must honor event ordering and capacity. |
| UI read ports | [`IPlayerEditorModels.h`](../src/core/IPlayerEditorModels.h) | Optional pattern and modulation display snapshots | Playback snapshots are not themselves atomic. |
| Output port | [`IOutputRenderer.h`](../src/core/IOutputRenderer.h) | Block renderer lifecycle and borrowed event view | Borrowed by the engine; `false` reports a delivery failure. |
| Engine | [`SequencerEngine.h`](../src/core/SequencerEngine.h), [`SequencerEngine.cpp`](../src/core/SequencerEngine.cpp) | Topology, master/follower directives, validation, filtering, frame mapping, sorting, dispatch, recovery | Setup is single-threaded; selected runtime controls and diagnostics are atomic. |
| Pattern catalog | [`PatternLibrary.h`](../src/core/PatternLibrary.h), [`PatternLibrary.cpp`](../src/core/PatternLibrary.cpp) | Immutable canonical patterns with stable IDs and append-only publication | Startup replacement precedes readers; runtime append is single-writer. |
| Legacy velocity catalog | [`VelocityModulationLibrary.h`](../src/core/VelocityModulationLibrary.h), [`VelocityModulationLibrary.cpp`](../src/core/VelocityModulationLibrary.cpp) | Named 8-bit velocity shapes used by `PatternPlayer` and the current UI | Same append-only publication contract as `PatternLibrary`. |
| Modulation runtime | [`ModulationLane.h`](../src/core/ModulationLane.h), [`ModulationLane.cpp`](../src/core/ModulationLane.cpp) | Reusable normalized sequences, target metadata, advance/reset policy, and independent cursors | Fixed capacity and allocation-free; caller supplies synchronization. |
| Pattern player | [`PatternPlayer.h`](../src/core/PatternPlayer.h), [`PatternPlayer.cpp`](../src/core/PatternPlayer.cpp) | Editable 32-step scheduling, loop quantization, velocity modulation, master sync, editor models | Borrows both libraries; configuration crosses threads through atomics/revision guards. |
| Pulse player | [`PulsePlayer.h`](../src/core/PulsePlayer.h), [`PulsePlayer.cpp`](../src/core/PulsePlayer.cpp) | Periodic trigger source and example of generic multi-target modulation | Configuration is ordinary mutable state and must not race with processing. |

## Runtime lifecycle

### 1. Construct and configure

On a non-real-time thread:

1. Construct libraries. Optionally replace their defaults from durable storage.
2. Construct renderers and players. Keep library owners alive longer than their
   players.
3. Register each player with `SequencerEngine::registerPlayer()`.
4. Connect each desired player/renderer pair with `connect()` and retain the
   returned IDs needed by renderer-specific configuration or persistence.
5. Configure renderer routes, choose a cycle-capable master if needed, and set
   initial player state.

Registration assigns `PlayerId` values by stable slot index. Connection assigns
`RouteId` values by stable route index. The current engine has no unregister,
disconnect, reorder, or unfreeze operation.

The same player cannot be registered twice. A player may fan out to multiple
different renderers, but the same player/renderer pair cannot be connected
twice.

### 2. Prepare

Call:

```cpp
engine.prepare({ sampleRate, maximumBlockFrames });
```

Preparation:

- permanently freezes registration and connection;
- reserves routing vectors for `route count * 128` events;
- calls `prepare()` on every player;
- calls `prepare()` once on every unique renderer.

It is valid for a host to prepare again after a sample-rate or maximum-block-size
change, but topology stays frozen. The built-in players treat preparation as a
runtime reset.

### 3. Process every host block

The host adapter constructs a coherent `TimelineBlock`, binds any framework
buffers to its renderers, calls `engine.run(block)`, and unbinds the buffers.

For each call, the engine performs this sequence:

1. Clear every engine-owned player buffer and snapshot pending one-shot reset
   requests.
2. Process the master first, if configured. Accept its first cycle boundary only
   if its whole player block succeeds and the boundary is finite and inside the
   half-open block.
3. Process every follower. Capabilities determine whether it receives an
   external quantization boundary and whether a queued master reset can be
   consumed.
4. Validate each player's event sequence. Overflow or any invalid event fails
   that entire player's block.
5. Reset failed players and mark all of their connected renderers for output
   reset. A transport discontinuity also resets every renderer before new events
   are rendered.
6. Apply mute and suppression to trigger starts, resolve route pitch, convert PPQ
   positions to integer frame offsets, and fan events out through routes.
7. Sort globally by frame, event priority, then stable traversal order.
8. Batch events per renderer and call each unique renderer exactly once,
   including when its event list is empty.
9. If a renderer returns `false`, reset that output and reset every player that
   feeds it.

### 4. Reset and release

`SequencerEngine::reset()` clears pending master-reset requests, resets every
player's runtime phase, and calls `resetOutputs()` on every renderer. It retains
topology, master selection, mute state, suppression edges, and the most recently
published diagnostics.

Do not call `reset()` concurrently with `run()`. A plugin normally calls it from
its deactivation/release lifecycle after audio processing has stopped.

### 5. Destroy in dependency-safe order

A safe owning aggregate declares objects in this conceptual order:

```text
libraries -> players -> renderers -> engine
```

C++ destroys members in reverse declaration order, so the engine releases its
borrowed references first, then renderers and players, and libraries last.

## Shared data contracts

### `PrepareSpec` and `RenderSpec`

Both carry:

| Field | Meaning |
| --- | --- |
| `sampleRate` | Host sample rate. Defaults to 44,100 Hz. |
| `maximumBlockSize` | Largest callback, used for preallocation or validation by implementations. |

The current built-in players do not derive musical timing from sample rate;
timing stays in PPQ. The engine uses block tempo and sample rate when mapping PPQ
to frames.

### `TimelineBlock`

| Field | Host responsibility |
| --- | --- |
| `ppqStart` | Musical position at frame zero, in quarter notes. |
| `ppqEnd` | Exclusive musical end. For a constant-tempo block, compute it from the formula below. |
| `tempoBpm` | Positive, finite tempo used for PPQ-to-frame conversion. |
| `sampleRate` | Positive, finite callback sample rate. |
| `sampleCount` | Number of frames in the destination buffers. |
| `playing` | Whether transport-driven scheduling should run. |
| `transportDiscontinuity` | True on start, stop transition, seek, loop wrap, or another non-contiguous jump. |

For the constant-tempo model used here:

```text
ppqPerSample = tempoBpm / (60 * sampleRate)
ppqEnd       = ppqStart + sampleCount * ppqPerSample
```

Players use `ppqEnd` to decide which musical boundaries belong to the block,
while the engine uses tempo, sample rate, sample count, and `ppqStart` to derive
frame offsets. Those fields therefore must describe the same interval.

The shipped wrapper treats a missing host PPQ position as stopped. It detects a
discontinuity when play starts or stops, or when `ppqStart` differs from the
previous expected end by more than roughly four sample periods.

### Player directives, results, and capabilities

`PlayerDirectives` separates engine policy from host time:

| Field | Meaning |
| --- | --- |
| `restartAtPpq` | Rebase only this player's phase at the supplied boundary. This does not claim that host transport moved. |
| `externalCycleBoundaryPpq` | A master's first valid cycle boundary in this block, if one exists. |
| `quantizePendingTransitionsExternally` | Pending player transitions should wait for the external cycle instead of a local cycle. |

`PlayerProcessResult` reports whether the player is active, its first cycle
boundary in the block, and whether it detected event overflow.

`PlayerSyncCapabilities` lets the engine avoid sending meaningless directives:

- `providesCycleBoundaries`: eligible to be a master;
- `acceptsExternalCycleBoundaries`: can quantize its own pending changes to a
  master;
- `acceptsExternalRestart`: can consume `restartAtPpq`.

`PatternPlayer` advertises all three. `PulsePlayer` only provides boundaries, so
it can be a master but cannot currently be reset or transition-quantized by one.

### Semantic events

`SequencerEvent` represents meaning, not a wire protocol:

| Type | Relevant payload | Intended use |
| --- | --- | --- |
| `triggerStart` | PPQ, trigger ID, normalized intensity, optional musical pitch | Start a note, gate, envelope, or other discrete action. |
| `triggerEnd` | PPQ and the same trigger ID | End the exact action previously started. |
| `controlPoint` | PPQ, logical control ID, normalized value, step/linear interpolation | Change a continuous or stepped output. |

The enum values deliberately encode same-frame priority:

```text
controlPoint -> triggerEnd -> triggerStart
```

This makes new control state visible before a retrigger and ensures an old gate
ends before a new one starts at the same sample. Events with the same frame and
type retain stable route/event traversal order.

`RouteMapping::fixedPitch(semitones)` overrides a start event's intrinsic pitch.
Without a fixed mapping, an event's optional musical pitch passes through. A
trigger end intentionally carries no pitch; output adapters recover the pitch
remembered for the matching start.

`RoutedEvent` adds source player, route, resolved frame offset, stable order, and
resolved pitch. Renderers should use `frameOffset` directly rather than doing a
second PPQ conversion.

`PlayerId` and `RouteId` use the maximum 32-bit unsigned value as an invalid
sentinel; valid engine-assigned values are current vector indexes. `TriggerId`
is a player-owned 64-bit identity with no central allocator. The built-in
players increment their own counters and renderers combine the trigger ID with
source and route identity.

`PitchList` also exists in `SequencerTypes.h` but is currently unused. It is not
part of routing or the persistent player model and should not be treated as an
active architectural contract.

### Frame conversion

The engine computes:

```text
rawFrame = (event.ppqPosition - block.ppqStart)
           / (block.tempoBpm / (60 * block.sampleRate))
frame     = round(rawFrame)
```

It permits half a sample of rounding tolerance and then clamps a valid rounded
offset into `[0, sampleCount - 1]`. Non-finite time, invalid tempo/rate, or an
offset outside that tolerance fails validation. A zero-sample block maps to frame
zero, but normal renderers should expect eventless zero-sample calls.

### Fixed event storage

`SequencerEventBuffer` owns an inline array of 128 events. `push()` returns
`false` and increments a dropped count after capacity is reached. Overflow stays
set until `clear()`; the engine clears the buffer before every player call.

The engine treats either the buffer's overflow flag or
`PlayerProcessResult::eventOverflow` as a whole-player block failure. This is
intentional: silently dropping a trigger end could leave MIDI or CV active.

## Component reference

### `IPlayer`

`IPlayer` is the minimal source-side port. It deliberately contains no pattern,
MIDI, CV, persistence, or UI requirement.

| Method | Contract |
| --- | --- |
| `prepare(PrepareSpec)` | Initialize or reset runtime resources after topology is complete. Do any implementation-specific preallocation here. |
| `reset()` | Clear runtime phase and active-trigger bookkeeping without discarding persistent configuration. Output cleanup is the renderer's responsibility. |
| `process(TimelineBlock, PlayerDirectives, SequencerEventBuffer)` | Synchronously append a finite, ordered semantic event sequence and return activity, boundary, and overflow status. |
| `syncCapabilities()` | Truthfully describe the directives/results the implementation supports. |

The engine clears the supplied event buffer before every call. A player does not
need to clear it and should not assume it owns the buffer after `process()`
returns. Built-in players are called serially on the engine's render thread.

A player that changes internal trigger state after a failed `push()` must still
report overflow. The engine will reject that player's whole block, call
`reset()`, and reset connected outputs.

### `IOutputRenderer` and `RoutedEventView`

`IOutputRenderer` is the destination-side port. One instance can receive routes
from many players, and one player can connect to several renderer instances.

| Method | Contract |
| --- | --- |
| `prepare(RenderSpec)` | Preallocate destination state and clear active outputs/triggers. |
| `renderBlock(TimelineBlock, RoutedEventView)` | Consume one already ordered batch synchronously. Called once for every block, including an empty batch. Return `false` if required delivery failed. |
| `resetOutputs()` | Idempotently panic/silence the destination and clear held state. |

The view points into engine-owned vector storage and expires when the callback
returns. A renderer may copy values into its own prepared storage, but it must
not retain the view or its pointers.

The engine invokes `resetOutputs()` on transport discontinuity, explicit engine
reset, connected-player failure, and renderer failure. The method has no failure
return, so an adapter that needs guaranteed hardware cleanup should also use its
host or device lifecycle's normal panic/flush facility.

### `SequencerEngine`

`SequencerEngine` is the orchestration layer. It does not generate notes and it
does not know JUCE, MIDI channels, or audio buffers.

#### Public API groups

| API | Purpose |
| --- | --- |
| `registerPlayer`, `connect` | Build the borrowed topology before preparation. |
| `prepare`, `run`, `reset` | Drive setup, one callback, and teardown/reset. |
| `setMasterPlayer`, `masterPlayerId` | Select/query the cycle source. A non-cycle-capable player is rejected. |
| `requestResetToMaster`, `resetToMasterPending` | Queue/query one follower restart for the next valid master boundary. |
| `setPlayerMuted`, `playerMuted` | Filter trigger starts while preserving player phase and release/control traffic. |
| `setSuppression`, `suppression` | Configure directed simultaneous-trigger suppression. Self-edges and invalid IDs are ignored. |
| overflow/drop/invalid getters | Read atomic diagnostics for the most recently processed block. |
| count/ID/frozen queries | Inspect stable topology. |

#### Master and follower behavior

The master is processed first. Its boundary is accepted even if it emits no
trigger at that point. This means a silent master pattern, a rest at loop start,
or a probability-rejected pulse can still synchronize followers.

A reset request is snapshotted at block start. It is consumed exactly once only
when a valid master boundary is present. A request arriving midway through a
block waits for a later block, which makes control timing deterministic.

Followers that accept external cycle boundaries receive external-quantization
mode whenever a master exists. If this block contains no valid master boundary,
their pending externally quantized transition remains pending rather than falling
back to a local boundary.

#### Mute and suppression

Mute filters only `triggerStart`. The player's pattern and modulation continue,
and trigger ends and control points still reach routes. Unmuting therefore
preserves phase.

Suppression is a directed edge: `A -> B` means a valid start from A suppresses a
start from B at the same musical position, within `1e-9` PPQ. It only filters
starts. A muted suppressor still participates because mute is an output policy,
not a player-state pause. Failed suppressors do not participate.

If a start was muted or suppressed, its later end may still arrive at a renderer.
Renderers must safely ignore unmatched ends; both shipped renderers do.

#### Event validation and failure containment

For every player, the engine checks:

- finite PPQ positions;
- nondecreasing PPQ order;
- successful mapping into the sample block;
- buffer and player-reported overflow.

One failure invalidates the whole player's block. Its events are not routed, the
player is reset, and every renderer connected to it is reset before rendering the
remaining healthy events.

If `renderBlock()` returns `false`, the engine resets that renderer and resets
all players connected to it. Renderer delivery across several different
renderers is not transactional; an adapter should not assume another renderer's
already-rendered output can be rolled back.

### `PatternPlayer`

`PatternPlayer` combines an editable step sequencer, pattern-selection policy,
one legacy velocity modulation lane, synchronization behavior, persistent
configuration, and optional UI models.

It implements `IPlayer`, `IPatternEditorModel`, and `IModulationEditorModel`.

#### Construction and ownership

The two-argument constructor borrows a `PatternLibrary` and a
`VelocityModulationLibrary`. It activates the first valid entry from each. The
one-argument constructor uses a process-lifetime static default velocity library.

Use the two-argument constructor when catalogs, state restoration, or a shared UI
must refer to one explicit modulation library.

#### Pattern state and APIs

| API group | Behavior |
| --- | --- |
| `selectPattern` | Requests canonical content and preserves the existing offset when activation occurs. |
| `selectSavedPattern` | Requests canonical content and also resets the baked draft offset on activation. Used after saving a cropped/rebased pattern. |
| selected/active ID getters | Distinguish the control request from audio-thread activation. |
| offset left/right | Rotate the effective hit mapping without moving stored draft bits. |
| playback speed | Index `0`, `1`, `2` means `0.5x`, `1x`, `2x`. |
| playback window | Requests an inclusive range clamped to the 32-step draft. |
| `toggleStep` | Maps the visible step back through the offset and toggles that source bit. |
| save/dirty APIs | Produce a coherent cropped pattern and compare draft state with the active canonical entry. |

Every player has a 32-step draft, represented by a 32-bit hit mask. Loading a
library pattern copies its hits into the draft and initializes the playback
window to `0 .. library length - 1`; the stored length does not reduce the
editable draft capacity.

The offset is interpreted by:

```text
sourceStep = (visibleStep - offset) modulo 32
```

`patternForSave()` first applies that effective offset, then crops the requested
inclusive playback window, then rebases the result to step zero. Hits outside the
window remain part of the unsaved draft but are not saved.

#### Scheduling

At normal speed, one pattern step is `0.25` PPQ, a sixteenth note. Half speed is
`0.5` PPQ and double speed is `0.125` PPQ. The fixed gate length is 50 percent of
the current step duration.

The scheduler:

- rebases local step zero to `ppqStart` on a playing transport discontinuity;
- processes half-open ranges with a small boundary tolerance;
- tracks the last absolute playback step to prevent duplicate starts across
  small or overlapping blocks;
- closes a carried gate in chronological order before a later step;
- loops through the inclusive active playback window;
- reports the first loop boundary even when its pattern step is a rest;
- gives every accepted hit a new `TriggerId` and schedules a paired end.

While stopped or on a transport discontinuity, a pending pattern and playback
window can become active immediately. During continuous playback:

- a window change activates at the end of the old active window;
- a pattern change activates at the local active-window boundary when there is
  no external master policy;
- a follower pattern change waits for the next valid master boundary when
  external quantization is enabled;
- a manual master reset splits the current block at that boundary, closes any
  old gate, rebases phase, and schedules the follower's first window step at the
  exact boundary.

An externally synchronized pattern activation restarts pattern phase. Pattern
changes alone deliberately do not reset velocity-modulation phase. An explicit
master reset resets both.

#### Velocity modulation

The legacy library/editing API uses 8-bit values from 0 through 255 and lengths
from 1 through 10. `PatternPlayer` converts each value exactly into the generic
16-bit normalized domain using `value * 257`, then uses an intensity lane.

The lane advances on candidate hits only. Rests do not consume values. Because
mute and suppression happen later in the engine, muted or suppressed hits still
consume velocity steps. A newly selected velocity preset activates at the start
of a process block and resets its own cursor; it does not wait for a pattern or
master boundary.

When the editable length grows, new steps repeat the former final value, or 255
if there was no old value.

#### Persistent state

`PatternPlayerPersistentState` contains:

- active pattern ID;
- complete editable hit mask and offset;
- requested/active loop window;
- speed index;
- active velocity preset ID and editable velocity values.

It intentionally excludes current step, modulation cursor, pending gate,
trigger counter, queued selection timing, mute, suppression, routes, and master
choice. The wrapper must persist those other concerns separately when needed.

Restore validates referenced IDs and all ranges, publishes the configuration,
and resets runtime phase.

### `PulsePlayer`

`PulsePlayer` is both a usable periodic source and the reference proof that the
engine and modulation system are not pattern-specific.

Its constructor takes a period in PPQ and a gate ratio. A non-finite or
non-positive period falls back to `0.5` PPQ; a finite gate ratio is clamped to
`[0, 1]`.
It owns a `ModulationBank`, creates intensity lane ID 1 by default, and can carry
an optional intrinsic musical pitch.

#### Public configuration

- `publishModulationState(state)` updates lane 1.
- `addModulationLane(definition, state)` adds a nonzero, unique lane until the
  eight-lane bank is full.
- `publishModulationState(laneId, state)` replaces one existing lane's sequence.
- `setBasePitch(optionalSemitones)` controls intrinsic pitch.
- capture/restore methods persist period, gate, optional pitch, and every lane
  definition and state.

Treat these as setup/control methods. They are not atomically published and must
not run concurrently with `process()` without an external handoff.

The constructor's gate clamp and `setBasePitch()` do not sanitize NaN or
infinity. Persistent-state restore does validate those fields, but direct callers
must also supply finite configuration.

#### Per-pulse algorithm

For each candidate pulse, lanes are advanced in this order:

1. `sourceStep`
2. `time`
3. `candidateTrigger`
4. deterministic probability decision
5. `emittedTrigger`, only if the candidate survives

Lane values can replace, add to, or multiply the current intensity, pitch, gate
ratio, or probability. Control targets emit `controlPoint` events. Final
intensity, probability, and gate ratio are clamped to `[0, 1]`; pitch is not
clamped.

The probability source is deterministic from the integer pulse index, so the
same phase and configuration produce the same decision sequence. Rejected
candidates still advance candidate lanes but not emitted-trigger lanes.

Every pulse is a reported cycle boundary, including a rejected pulse. The
player currently advertises boundary production only and ignores external
directives.

### `ModulationLaneRuntime` and `ModulationBank`

The modulation layer separates a source sequence from the player-specific code
that applies it.

#### Value model

`NormalizedValue` is exact unsigned fixed point in `[0, 65535]`:

- `fromUnipolar8(x)` is exactly `x * 257`;
- `fromFloat(x)` clamps to `[0, 1]` and rounds;
- `toFloat()` returns `raw / 65535`.

`ValueMapping` linearly maps that normalized value into `[minimum, maximum]` and
labels the result with `replace`, `add`, or `multiply`. The mapping itself does
not clamp its configured endpoints or final mapped value; the consuming player
decides target-specific limits.

#### Definition

A `ModulationLaneDefinition` contains:

- stable nonzero `LaneId`;
- target: intensity, pitch, gate length, probability, or control;
- advance point: source step, candidate trigger, emitted trigger, or time;
- reset-reason bit mask;
- value mapping and combine mode;
- logical control number for control events.

`makeIntensityLaneDefinition()` creates the compatibility behavior used by
`PatternPlayer`: candidate-hit advancement and reset on source selection,
transport discontinuity, or explicit restart.

#### Runtime and bank behavior

`ModulationLaneRuntime` owns one definition, up to 32 normalized values, and an
independent next/current cursor. Publishing state clamps length to 32 and keeps a
valid cursor modulo the new length. An empty state produces no samples. Reset
only changes the cursor when the definition opted into that reason.

`ModulationBank` stores at most eight lanes inline, rejects zero or duplicate
IDs, and advances all matching lanes into a caller-owned fixed array. Lane
insertion order is combination order. Neither class allocates or synchronizes
internally.

The `time` advance point is a semantic hook, not an autonomous clock. In the
current `PulsePlayer`, it is invoked once per pulse candidate. A future player
may define a different cadence, but that cadence must be documented and tested.

### `PatternLibrary`

`PatternLibrary` owns canonical named patterns in a fixed array of 256 entries.
It begins with ten built-ins. A valid pattern has a nonzero length no greater
than 32; IDs are nonzero and currently limited to 1 through 256.

| Default ID | Name | Steps |
| ---: | --- | --- |
| 1 | Basic Kick | `x---` |
| 2 | All Steps | `xxxx` |
| 3 | Backbeat | `----x-------x---` |
| 4 | Offbeat Hats | `--x---x-` |
| 5 | Tresillo | `x--x--x-` |
| 6 | 3-Step Pulse | `x--` |
| 7 | 5-Step Pulse | `x----` |
| 8 | 7-Step Pulse | `x------` |
| 9 | 9-Step Pulse | `x--------` |
| 10 | Euclidean 5/12 | `x-x--x-x--x-` |

`replaceEntriesForStartup()` can replace this complete default catalog, so a
loaded catalog's file order and IDs become authoritative for that library
instance.

Published entries are immutable and retain stable addresses for the library's
lifetime. Lookups are linear by ID or content, which is bounded by the small
catalog capacity.

#### Publication model

Readers acquire-load the published entry count, then inspect only that immutable
prefix. A single control/UI writer may stage one new entry in the next slot and
release-store the new count after all entry fields are complete.

`addOrFind(name, pattern, beforePublish)`:

1. returns an existing entry for duplicate content, regardless of name;
2. rejects an empty name, invalid length, or full catalog;
3. normalizes all hit bits beyond meaningful length to false;
4. stages an ID, name, and content;
5. optionally calls `beforePublish`, allowing persistence or ID conflict
   resolution;
6. clears and rejects the stage if the callback returns false;
7. clears and rethrows if the callback throws;
8. revalidates callback mutations, then atomically publishes by count.

The callback is the durability seam: the current file store writes the merged
catalog before the new entry becomes visible to audio-thread readers.

`replaceEntriesForStartup()` validates the entire replacement first, including
unique IDs and unique content, then replaces and publishes it as one startup
operation. It must run before players or other concurrent readers exist.

### `VelocityModulationLibrary`

This component uses the same fixed-array, stable-address, single-writer,
append-only publication design as `PatternLibrary`. Its payload is a named
`VelocityModulation` with 1 through 10 unsigned 8-bit values. It contains two
built-ins and has a capacity of 256.

| Default ID | Name | Values |
| ---: | --- | --- |
| 1 | Steady 100 | `[201]` |
| 2 | Four-Step | `[255, 100, 225, 150]` |

Equality in both libraries compares length and the meaningful prefix only.
Unused tails are normalized on load or insertion: false for patterns and zero
for velocity values.

The velocity catalog is a compatibility/product layer. New generic modulation
work should normally use `ModulationLaneDefinition` and `ModulationLaneState`,
then decide separately whether the legacy named-preset UI or JSON schema should
also expose it.

### `IPlayerEditorModel` interfaces

The real-time `IPlayer` interface contains no mandatory pattern or velocity UI
methods. Optional display capabilities are separate:

- `IPatternEditorModel` provides a `PatternView` and a pattern playhead snapshot;
- `IModulationEditorModel` provides a modulation playhead snapshot.

`PatternView::isHit()` applies modular offset and refuses to shift beyond its
32-bit mask. `isInsidePlaybackWindow()` uses the inclusive UI window.

These are read-only views, not general cross-thread publication mechanisms. The
shipped processor reads playback snapshots on the audio thread immediately
after processing and copies the values to atomics for GUI polling.

## Worked examples

### PPQ event to sample frame

At 120 BPM and 48 kHz:

```text
ppqPerSample = 120 / (60 * 48000) = 1 / 24000
```

A 512-frame block beginning at PPQ 8 spans approximately `0.0213333` PPQ. An
event at PPQ `8.0106667` maps to approximately frame 256. The player schedules in
musical time; the engine performs this mapping once; the renderer consumes frame
256 directly.

### Pattern, gate, and velocity phase

For `Basic Kick = x---` at normal speed with window `[0, 3]`:

```text
step starts: 0.00, 0.25, 0.50, 0.75 PPQ
loop length: 1.00 PPQ
hit starts:  0.00 PPQ
hit ends:    0.125 PPQ (50% gate)
```

With velocity values `[255, 100, 225, 150]`, successive hits—not timeline
steps—consume those values. Four loops produce normalized intensities near
`[1.0, 0.392, 0.882, 0.588]`. Rests, mute, and suppression do not pause this
cursor.

### Follower pattern change and one-shot reset

Suppose player A is master and its active cycle begins at PPQ 4.0. Player B has
requested a different pattern and a one-shot reset before a block containing
that boundary.

```text
1. Engine processes A first and validates boundary 4.0.
2. B receives externalCycleBoundaryPpq = 4.0 and restartAtPpq = 4.0.
3. B processes its old phase in [blockStart, 4.0).
4. At 4.0 it activates the pending pattern, closes any held trigger, and rebases.
5. B's new playback-window start is eligible exactly at 4.0.
6. The reset request is cleared; it will not repeat next cycle.
```

If A reports no boundary in this block, B keeps playing its old phase and both
requests remain pending.

### One source, two outputs

The current plugin connects each player to both a shared MIDI renderer and a
shared CV renderer. A pattern hit has no inherent drum note. Its routes attach a
fixed semitone value, for example 36:

```text
PatternPlayer triggerStart
       ├── route M, fixed pitch 36 ──> MIDI channel 1 note 36
       └── route C, fixed pitch 36 ──> gate + 1V/oct-style pitch CV
```

The player's timing and trigger identity are generated once. Output protocols
remain adapter choices.

## Adding a player

A new discrete player only needs `IPlayer`; pattern and modulation editor models
are optional.

```cpp
class CustomPlayer final : public lps::IPlayer
{
public:
    void prepare(const lps::PrepareSpec&) noexcept override
    {
        reset();
        // Reserve or initialize every resource needed by process().
    }

    void reset() noexcept override
    {
        active_ = false;
        // Keep persistent configuration; clear only runtime phase/output state.
    }

    lps::PlayerProcessResult process(
        const lps::TimelineBlock& block,
        const lps::PlayerDirectives& directives,
        lps::SequencerEventBuffer& output) noexcept override
    {
        lps::PlayerProcessResult result;
        // The engine already cleared output.
        // Emit finite, nondecreasing events in [block.ppqStart, block.ppqEnd).
        // Pair every start/end TriggerId and honor only advertised directives.
        result.eventOverflow = output.overflowed();
        return result;
    }

    lps::PlayerSyncCapabilities syncCapabilities() const noexcept override
    {
        return {}; // Advertise only behavior actually implemented.
    }

private:
    bool active_ = false;
};
```

Before shipping a player, test:

- start, stop, seek/discontinuity, and reset with a held trigger;
- small adjacent blocks without duplicate boundaries;
- long blocks containing several events;
- exact half-open end behavior;
- event ordering and capacity-plus-one failure;
- each claimed synchronization capability;
- persistence and UI handoff, if present;
- no allocation or locks in its normal processing path.

If the player reports cycle boundaries, it must return the first valid boundary
inside the block whether or not that boundary emits a trigger. If it accepts an
external reset, it must close held outputs before rebasing at the requested PPQ.

## Adding an output renderer

An adapter for another MIDI API, CV backend, network protocol, or test sink
implements `IOutputRenderer`:

```cpp
class HostRenderer final : public lps::IOutputRenderer
{
public:
    void prepare(const lps::RenderSpec& spec) noexcept override
    {
        // Preallocate for spec.maximumBlockSize and clear active-trigger state.
    }

    bool renderBlock(
        const lps::TimelineBlock& block,
        lps::RoutedEventView events) noexcept override
    {
        // Called once per block, even when events.empty().
        // Use event.frameOffset and preserve the supplied order.
        // Return false if the destination rejects required output.
        return destinationIsHealthy_;
    }

    void resetOutputs() noexcept override
    {
        // Idempotent panic/silence: release notes, lower gates, clear held state.
    }

private:
    bool destinationIsHealthy_ = true;
};
```

Renderer requirements:

- do not retain the event view or pointers into it;
- handle empty blocks and held state;
- remember start metadata needed by a later pitchless end;
- ignore unmatched ends safely;
- make `resetOutputs()` safe before prepare, during discontinuity, and after a
  partial failure;
- return `false` if required destination capacity or data is missing;
- allocate all callback storage during setup;
- decide explicitly whether unsupported semantic event types are ignored or are
  fatal.

The JUCE MIDI and CV implementations are useful concrete references:
[`MidiBufferRenderer`](../src/plugin/MidiBufferRenderer.cpp) and
[`CvBufferRenderer`](../src/plugin/CvBufferRenderer.cpp).

## Adding modulation behavior

For another lane on a player:

1. Choose a unique nonzero `LaneId`.
2. Define target, advance point, reset mask, mapping, and combine mode.
3. Publish a nonempty state of at most 32 steps.
4. Invoke the corresponding advance point at a precisely documented musical
   moment.
5. Apply returned samples in stable lane order before emitting the affected
   event.
6. Decide which final targets clamp and when probability is evaluated.
7. Add tests for cursor independence, reset reasons, rejected candidates, and
   persistence.

Intensity and pitch can decorate a trigger start. Gate length and probability
affect whether or when events exist, so they must be applied before scheduling
the final event pair. Continuous outputs use control points and require a
renderer that knows how to hold or interpolate them.

## Running core without JUCE

### Actual dependencies

The source in `src/core` requires:

- a C++17 compiler and standard library;
- exceptions enabled for the optional `BeforePublish` callback paths, which
  clean up and rethrow callback exceptions;
- working atomics for the integer and boolean types used by the engine,
  libraries, and `PatternPlayer`;
- heap allocation during construction/setup for engine vectors and owned atomic
  cells.

It does not require RTTI, JUCE, a VST SDK, REAPER, an operating-system API,
filesystem access, JSON, networking, GUI support, an audio device, or a MIDI
library.

For strict real-time or 32-bit targets, verify that the relevant 64-bit and
`size_t` atomics are lock-free on the target, or accept/link the platform's
atomic runtime behavior.

The repository's `sequencer_core` target links no JUCE library. However, the
top-level [`CMakeLists.txt`](../CMakeLists.txt) fetches or finds JUCE before that
target is declared. A JUCE-free consumer should either make plugin construction
optional in this repository or define its own target from these implementation
files:

```cmake
add_library(sequencer_core STATIC
    src/core/ModulationLane.cpp
    src/core/PatternLibrary.cpp
    src/core/PatternPlayer.cpp
    src/core/PulsePlayer.cpp
    src/core/SequencerEngine.cpp
    src/core/VelocityModulationLibrary.cpp
)
target_include_directories(sequencer_core PUBLIC src)
target_compile_features(sequencer_core PUBLIC cxx_std_17)
```

### Minimum composition

```cpp
lps::PatternLibrary patterns;
lps::VelocityModulationLibrary modulations;
lps::PatternPlayer kick { patterns, modulations };
HostMidiRenderer midi; // Your IOutputRenderer implementation.
lps::SequencerEngine engine;

const auto playerId = engine.registerPlayer(kick);
const auto routeId = playerId
    ? engine.connect(
        *playerId, midi, lps::RouteMapping::fixedPitch(36.0f))
    : std::nullopt;
if (!playerId || !routeId)
    failInitialization();

engine.prepare({ sampleRate, maximumBlockFrames });
```

For each real-time callback:

```cpp
midi.bind(hostOutputEventQueue);

lps::TimelineBlock block;
block.ppqStart = hostPpq;
block.tempoBpm = hostTempo;
block.sampleRate = sampleRate;
block.sampleCount = frameCount;
block.ppqEnd = block.ppqStart
    + frameCount * block.tempoBpm / (60.0 * block.sampleRate);
block.playing = hostIsPlaying;
block.transportDiscontinuity =
    hostStarted || hostStopped || hostSeeked || hostLoopWrapped;

engine.run(block);
midi.unbind();
```

If a host cannot supply musical position, the current tempo-synchronized design
cannot infer it. The wrapper must either mark the block stopped or provide its
own continuous PPQ clock and discontinuity detection.

## What JUCE currently supplies

To port the product to another VST wrapper, plugin SDK, CLAP/AU framework, or
standalone application, replace the services in this table. The core APIs can
remain unchanged.

The repository currently pins JUCE 9.0.1 and builds VST3 only. JUCE configures
the target as a synth with MIDI output, no MIDI input, and no MIDI-effect
classification. `LPS_FETCH_JUCE=ON` fetches that pinned version; when disabled,
the build expects an installed JUCE CMake package.

| External service supplied by JUCE | Current implementation | Replacement contract |
| --- | --- | --- |
| VST3 target, metadata, factory, packaging | `juce_add_plugin` and `createPluginFilter()` | Framework/SDK component target, entry point, metadata, packaging, signing, and installation. |
| Processor lifecycle | `juce::AudioProcessor` | Call core setup/prepare/process/reset at the equivalent initialize/activate/process/deactivate stages. |
| Bus negotiation | `BusesProperties` and `isBusesLayoutSupported` | Declare MIDI/event output and any audio/CV buses the new host can expose. |
| Host clock | `AudioPlayHead::PositionInfo` | Supply play state, tempo, PPQ, sample count/rate, and explicit discontinuity detection. |
| MIDI destination | `juce::MidiBuffer` and `MidiMessage` | Renderer that writes timestamped note events, reports queue failure, pairs ends with remembered starts, and implements panic. |
| CV/audio destination | `juce::AudioBuffer<float>` and `FloatVectorOperations` | Renderer over planar sample buffers; ordinary `std::fill_n` or platform SIMD can replace JUCE filling. |
| Session state | `MemoryBlock`, `DynamicObject`, `var`, and `JSON` | Host state stream plus validated JSON or binary serialization. |
| Catalog persistence | `File`, `JSON`, `InterProcessLock`, and `TemporaryFile` | Per-user path resolution, schema parser, in-process and interprocess exclusion, merge, temporary write, flush, and atomic replacement. |
| GUI | JUCE components, dialogs, and timer | Capability-aware controls plus a safe audio-to-UI snapshot handoff. |
| Callback utilities | `ScopedNoDenormals`, `jassert` | Appropriate FPU mode guard and debug assertion mechanism for the target. |

### Reference renderer behavior

`MidiBufferRenderer` is constructed with a MIDI channel, clamped to 1 through
16. During each callback the wrapper binds a `juce::MidiBuffer` with
`setMidiBuffer()` and unbinds it afterward.

- A trigger start requires resolved pitch. Pitch is rounded to the nearest MIDI
  note and clamped to 0 through 127; normalized intensity is clamped to 0 through
  1 and passed as velocity.
- The renderer remembers the emitted note by player, route, and trigger. A later
  end retrieves that exact note; an unmatched end is ignored.
- Control points are intentionally ignored by this renderer.
- Missing destination, missing start pitch, active-trigger exhaustion, or host
  buffer insertion failure returns `false`.
- Reset clears trigger memory and requests channel all-notes-off at frame zero
  when a buffer is bound.

`CvBufferRenderer::configureRoute()` associates a `RouteId` with optional gate,
pitch, and control channel indexes. Configure routes before audio starts and
before binding callback buffers.

- Gate is 1 while one or more exactly tracked triggers on the route are active
  and 0 otherwise.
- Pitch uses:

  ```text
  volts = voltsAtReferencePitch
          + (semitones - referencePitchSemitones) / 12 * voltsPerOctave
  ```

- A control point is clamped to 0 through 1. Step interpolation changes at its
  frame; linear interpolation ramps from the previous control point in that
  block.
- Held gate, pitch, and control values are written through eventless blocks.
- Reset writes every configured output back to zero.

The current CV adapter gives each route one control channel and does not use
`logicalControl` to select among multiple channels. A renderer exposing several
logical controls must add that mapping itself.

### Current JUCE composition

[`LivePatternSequencerProcessor`](../src/plugin/PluginProcessor.cpp) currently:

- loads both catalogs before constructing players;
- owns ten `PatternPlayer` drum voices mapped to MIDI notes 36 through 45;
- owns one `PulsePlayer` mapped to note 46;
- registers all eleven players and connects each to both shared renderers;
- uses MIDI channel 1;
- exposes 33 discrete CV/audio outputs: gate, pitch, and control for each player;
- assigns player 0 (`BD1`) as master;
- calls `engine.prepare()` from `prepareToPlay()`;
- builds `TimelineBlock` from the host playhead in `processBlock()`;
- clears the audio buffer, binds MIDI/CV destinations, runs the engine, and
  unbinds them;
- copies player playback snapshots into atomics for the GUI.

The fixed route pitch wins over intrinsic player pitch. A port that wants pitched
players should omit the fixed mapping for those routes.

A MIDI-only port can omit the CV renderer and audio bus entirely. A pass-through
audio plugin must not copy the current wrapper's unconditional `audio.clear()`
policy. The shipped schema also assumes exactly one MIDI and one CV route per
player, so a differently routed port needs its own state validation rules.

## Persistence boundaries

There are three independent layers:

| Layer | Core support | Current outer implementation |
| --- | --- | --- |
| Shared pattern/modulation catalogs | Stable IDs, validation, startup replacement, append-before-publish callback | Two per-user, versioned JSON files. |
| Concrete player configuration | `PatternPlayerPersistentState` and `PulsePlayerPersistentState` capture/restore | Serialized inside plugin session state. |
| Engine/application graph | Public master, mute, suppression, player/route IDs; no aggregate serializer | JUCE processor state schema version 2. |

Core can run entirely from built-in catalogs with no filesystem. Durable
catalogs and session state are optional application services.

The current catalog wire formats are deliberately small and human-readable:

```json
{
  "format": "live-pattern-sequencer-pattern-library",
  "schemaVersion": 1,
  "patterns": [
    { "id": 1, "name": "Basic Kick", "steps": "x---" }
  ]
}
```

```json
{
  "format": "live-pattern-sequencer-velocity-modulation-library",
  "schemaVersion": 1,
  "modulations": [
    { "id": 1, "name": "Steady 100", "values": [201] }
  ]
}
```

The default directory is JUCE's per-user application-data location followed by
`Hew/LivePatternSequencer`. A different host may choose another location without
changing core.

The current file stores add defenses outside core:

- strict format and schema version checks;
- nonempty, bounded, unique IDs/content and payload validation;
- a 1 MiB maximum catalog size;
- a two-second in-process timed mutex and named interprocess lock;
- merge with the latest disk state to avoid losing saves from stale plugin
  instances;
- conflict-aware ID reassignment before publication;
- write to a temporary file, flush, then atomically replace the target;
- preserve malformed existing files rather than overwriting them;
- publish an entry to the live library only after persistence succeeds.

These behaviors live in
[`PatternLibraryFileStore`](../src/plugin/PatternLibraryFileStore.cpp) and
[`VelocityModulationLibraryFileStore`](../src/plugin/VelocityModulationLibraryFileStore.cpp).
Another framework needs equivalent policies only if it offers the same shared
catalog feature.

The current plugin state is JSON schema version 2 and can migrate version 1's
pattern/velocity selections. Restore validates player count/type, routes, IDs,
lane definitions and values, windows, speeds, mute, suppression, and master
before applying normal valid state. Referenced catalog IDs must already exist.

## Safety and real-time behavior

### Implemented mechanisms

| Risk | Mechanism |
| --- | --- |
| Callback allocation | Fixed player event buffers and modulation arrays; engine vector capacity reserved before topology freezes. |
| Exceptions escaping processing | Real-time ports and built-in processing methods are `noexcept`. |
| Stale player events | Engine clears its owned event buffer before every process call. |
| Event overflow | Sticky per-block overflow/drop count; whole-player block rejection; player and connected output reset. |
| Malformed timing | Finite, nondecreasing, frame-mappable PPQ validation. |
| Nondeterministic delivery | Global frame sort, explicit same-frame type priority, stable tie ordering. |
| Stuck notes/gates | Trigger identity pairing, held-output tables in renderers, trigger ends on stop/discontinuity, renderer panic/reset paths. |
| Shared CV gate interference | Current CV renderer tracks exact active triggers and per-route active counts; an unmatched end cannot lower another trigger. |
| Invalid master | `setMasterPlayer()` rejects players that do not advertise cycle boundaries. |
| Racy one-shot reset consumption | Atomic request, block-start snapshot, and one successful exchange at a validated boundary. |
| UI mute/suppression changes | Heap-stable atomic cells avoid invalidation when setup vectors grow; topology freezes before processing. |
| Torn pattern/modulation draft snapshot | Odd/even revision guards and retrying snapshot readers around groups of atomic fields. |
| Readers seeing half-written library entries | Fixed stable storage, immutable published entries, release-published/acquire-read count. |
| Bad catalog overwrite | Full validation, timed locks, latest-state merge, temporary replacement, and publish-after-durability callback. |
| Corrupt session data | Version/type/range/topology validation before normal restore. |

### Intended thread model

| Operation | Intended context | Notes |
| --- | --- | --- |
| Library startup replacement | Setup thread, before readers | Not safe after players/readers begin. |
| Library lookup | Audio or control thread | Lock-free read of an immutable published prefix. |
| Library `addOrFind` | One control/UI writer | May allocate, persist, and throw through callback; never audio-thread work. |
| Engine register/connect/prepare/master setup | Non-audio setup thread | Do not race with `run()`. Only register/connect are explicitly rejected after prepare. |
| Engine `run` | One audio/render thread | Do not call concurrently or before real-time preparation. |
| Engine `reset` | Deactivated render thread or externally synchronized lifecycle | Do not race with `run()`. |
| Mute, suppression, reset request, diagnostic reads | Control/UI against audio | Backed by atomics. |
| Pattern selection and PatternPlayer editing | Intended control/UI against audio | Uses per-field atomics and revision guards; see limitations below. |
| Pulse/lane configuration | Setup or externally synchronized control | Ordinary state; not safe to mutate concurrently with processing. |
| Playback snapshot handoff | Audio thread reads model, then publishes separate atomics | Direct GUI reads of the built-in snapshot members would race. |
| Renderer destination bind/unbind | Same audio callback thread as `run` | The bound framework buffer must remain valid through `renderBlock`. |

### Important limits and assumptions

The current design is real-time-oriented, but these points are not hidden
guarantees:

- `run()` does not enforce that `prepare()` was called. Without preparation,
  routing vectors can allocate.
- `noexcept` prevents exception propagation; it does not prove that a standard
  library implementation or target atomic is lock-free.
- `prepare()` itself is `noexcept` but reserves vectors. An allocation failure
  during preparation terminates rather than producing a recoverable status.
- `PatternPlayer` control publication is not a single immutable snapshot for the
  whole callback. Several fields are individually atomic. In particular, the
  audio path's velocity draft snapshot retries while a writer owns an odd
  revision, so it can spin. This is not strictly wait-free.
- UI-side pattern and velocity writers also spin to acquire their revision
  guards. Use one UI writer and keep edit critical sections short.
- `PulsePlayer`, `ModulationLaneRuntime`, `ModulationBank`, and their persistent
  capture/restore methods contain ordinary mutable fields and need external
  synchronization for live editing.
- Pattern and modulation playback snapshots contain ordinary fields. Use an
  audio-to-UI handoff like the current processor rather than reading them
  concurrently.
- Master selection, topology queries during mutation, player lifetime, and
  renderer lifetime are not synchronized by the engine.
- Event validation checks timing and order, but does not comprehensively validate
  every payload float or semantic combination. Producers and renderers must
  validate/clamp target-specific data.
- `NormalizedValue::fromFloat()`, direct modulation mappings, optional pulse
  pitch, and route/CV calibration should receive finite inputs. `std::clamp`
  alone does not turn NaN into a safe value, and enum-valued lane fields are not
  runtime-validated by the generic lane classes.
- The engine's public diagnostics cover player event validation/overflow, not a
  separate persisted renderer-error code.
- A shared renderer reset clears healthy players' held output on that renderer
  as well. This is the safe failure policy, not per-source isolation.
- Session-state restore is wrapper code, not an audio-thread operation. The
  current restore is strict but is not a general lock-free or transactional
  graph replacement API.
- A failed `PatternPlayer` restore can leave its already-activated pattern in
  place if later modulation activation cannot acquire its revision guard. The
  processor also applies validated players sequentially. Treat a reported
  restore failure as potentially partial and recover from a known snapshot.
- State capture is not one global atomic snapshot across players, engine flags,
  and both PatternPlayer revisions. Serialize it against graph mutations when a
  coherent project checkpoint is required.
- The current plugin-state JSON parser has no explicit input byte ceiling, even
  though the separate catalog readers are capped at 1 MiB. A replacement wrapper
  accepting less-trusted state should add an appropriate limit.

### Shipped-renderer limits

The current adapters add useful containment but do not remove host obligations:

- `MidiBufferRenderer` has 128 active-trigger slots. Exhaustion returns failure.
  It does not pre-grow the host-provided `juce::MidiBuffer`, so strict
  allocation-free behavior depends on that buffer having sufficient capacity.
- MIDI note numbers and intensities are clamped, and all-notes-off is requested
  on reset. Failure to enqueue that panic is not separately reportable.
- Standard MIDI note-off is not reference-counted by note number. Avoid mapping
  overlapping routes to the same channel/note unless the destination policy for
  overlapping voices is acceptable.
- `CvBufferRenderer` supports at most 64 configured routes, rejects duplicate
  route IDs and channel ownership, and permits a negative channel to mean
  disabled. Its calibration inputs must be finite.
- CV gate state is reference-counted per route and backed by exact trigger
  identity, so an unmatched end cannot lower another active trigger's gate.

## Verification and tests

The test executables document behavior as well as checking it:

| Test target | Main contract coverage |
| --- | --- |
| `sequencer_core_tests` | Lifecycle, topology freeze, validation, overflow, renderer failure, master capabilities, reset timing, mute, suppression, routing order. |
| `pattern_player_tests` | PPQ scheduling, gates, selection/window quantization, save semantics, offset/speed, velocity phase, discontinuity, snapshot coherence. |
| `pulse_player_tests` | Generic route use, multi-target modulation, probability, emitted-trigger semantics. |
| `modulation_lane_tests` | Exact normalization, reset/advance policy, independent lane cursors. |
| `pattern_library_tests` | Built-ins, lookup/equality, capacity, startup validation, stable publication, callback failure. |
| `velocity_modulation_library_tests` | Equivalent catalog guarantees for velocity shapes. |
| `midi_buffer_transport_tests` | MIDI renderer note mapping, velocity, panic, trigger-pitch memory, destination failure. The historical target name says transport. |
| `cv_buffer_renderer_tests` | Held gates across empty blocks, exact release frames, trigger isolation, control ramps, reset-to-zero. |
| `plugin_processor_tests` | Concrete topology, file persistence/concurrency, capabilities, CV buses, versioned state and migration. |

From a configured build directory:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

This repository may use another build directory such as `build-macos`; substitute
that name as needed. The six pure-core test executables link only
`sequencer_core`. Renderer and plugin tests also link JUCE.

For concurrency changes, add or run a ThreadSanitizer configuration. The normal
tests establish functional behavior but do not prove the absence of every race
or real-time stall.

## Change-impact guide for agents

| Change | Start with | Required nearby tests/integration review |
| --- | --- | --- |
| Timeline or event shape | `SequencerTypes.h`, `IPlayer.h`, `IOutputRenderer.h` | Engine, every player, every renderer, state schema. |
| Routing, ordering, mute, suppression, master reset | `SequencerEngine.*` | `SequencerEngineTests.cpp`, both renderer tests, processor façade. |
| Pattern timing/edit/save behavior | `PatternPlayer.*` | `PatternPlayerTests.cpp`, state capture/restore, editor assumptions. |
| Generic modulation target or policy | `ModulationLane.*`, `PulsePlayer.*` | Modulation and pulse tests; renderer/control semantics; state lane parser. |
| Legacy velocity preset behavior | `VelocityModulationLibrary.*`, `PatternPlayer.*` | Velocity library/player tests, file store, editor, state migration. |
| Catalog payload/schema | Core library plus matching `*FileStore.*` | Library and processor persistence/concurrency tests. |
| New player type | `IPlayer.h`, one existing player, processor `PlayerBundle` | Capabilities/UI, route construction, state type/version, mixed-player tests. |
| New renderer or output protocol | `IOutputRenderer.h`, `SequencerEngine.cpp` dispatch | Failure/reset, held state, empty blocks, exact frames, plugin bus declarations. |
| Host wrapper/SDK replacement | `PluginProcessor.cpp` lifecycle and this guide's JUCE table | Transport continuity, bus/event capacity, state and catalog services. |

Before completing a core change, verify all of the following:

- half-open PPQ block behavior and inclusive playback-window behavior remain
  explicit;
- setup performs every allocation needed by the real-time path;
- overflow and destination failure cannot silently lose a required trigger end;
- same-frame event priority remains deterministic;
- cycle capabilities match actual directive handling;
- persistent IDs and schema compatibility are intentionally preserved or
  migrated;
- control-to-audio and audio-to-UI ownership is documented;
- borrowed lifetimes and reset behavior remain valid;
- tests cover start, stop, discontinuity, empty block, exact boundary, failure,
  and capacity edges.
