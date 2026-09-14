# Runtime signal graph refactor: implementation handoff

## Purpose and authority

This document scopes the refactor described in *Sequencer Core Architecture
Requirements*. It is an implementation handoff, not a description of the
current code.

When this handoff conflicts with the existing architecture documentation or
the earlier architecture audit, use this handoff. The explicit decisions made
after review also override the source requirements where noted, especially the
mute and suppression behavior.

The implementing agent should change the code, tests, plugin composition, and
affected documentation. It should not preserve obsolete velocity-specific,
`PulsePlayer`, or legacy-state abstractions merely to reduce the diff.

## Required outcome

The core must become a runtime-configurable signal graph with these boundaries:

```text
PatternLibrary                         ModulationLibrary
      │                                       │
      ▼                                       ▼
PatternPlayer ── Hit ───────┐       ModulationPlayer ── Value
      │                     │                 │
      │ cycle/control       ▼                 ▼
      └──────────────> runtime graph ──────> Voice
                                                │
                                   resolved semantic events
                                                │
                                                ▼
                              output bindings and renderers
                                      ├── MIDI
                                      ├── CV
                                      └── future outputs
```

The responsibilities are strict:

- `PatternPlayer` determines when a hit occurs. It owns no modulation,
  velocity, pitch, gate, voice, MIDI, or CV state.
- `ModulationPlayer` reads a reusable generic modulation sequence and emits
  generic normalized values. It does not know the destination parameter or
  output protocol.
- `Voice` gives values meaning, stores current parameter state, samples the
  required parameters on a trigger, and owns semantic trigger lifetime.
- Bindings connect sources to commands, advances, voice triggers, voice
  parameters, and output routes.
- Renderers convert resolved voice events into MIDI, CV, or another concrete
  protocol.
- Pattern and modulation library records remain immutable reusable definitions;
  runtime cursor and playback state live in players.

## Confirmed product decisions

### Compatibility and persistence

- Do not create a `Project` core type.
- No compatibility with legacy velocity-specific catalogs, plugin-state
  schemas, or representations is required.
- The plugin wrapper must continue to serialize its current composition as
  host state, but
  that state is a graph/configuration snapshot rather than a core project
  aggregate.
- Use one clean new schema and reject unsupported
  versions instead of carrying the current v1/v2 migration code.
- Persist configuration and stable references, not transient cursors, active
  triggers, or queued runtime events.

### Existing behavior to preserve

Except where this document explicitly removes or replaces a feature, preserve
the current behavior covered by the passing test suite:

- half-open PPQ processing intervals;
- sample-accurate event placement;
- 32-step pattern drafts, offsets, inclusive playback windows, and the current
  three playback speeds;
- requested-versus-active pattern selection and cycle-quantized activation;
- deterministic same-frame output ordering;
- independent runtime state for players that reference the same library record;
- trigger identity and balanced start/end rendering;
- output reset on stop, discontinuity, failure, and overflow;
- globally ordered renderer batches and eventless CV blocks;
- pattern and modulation catalog validation, deduplication, stable immutable
  entries, and publish-after-persistence behavior;
- realtime processing without allocation, locks, blocking, or unbounded work.

The repository is clean at handoff time and all nine current CTest targets pass.

### Removed and deferred behavior

- Remove `PulsePlayer`. A periodic pulse is represented by a `PatternPlayer`
  playing a one-step hit pattern with a clock advance source. `Continuous` and
  `OneShot` are playback modes, not separate player types.
- Remove `VelocityModulation`, `VelocityModulationId`,
  `VelocityModulationLibrary`, the velocity-specific file store, and embedded
  modulation lanes/banks.
- Phase one supports at most one active trigger binding per voice.
- Phase one supports at most one active modulation binding per voice parameter.
- Multiple-source arithmetic and priority policies such as add, subtract, sum,
  multiply, and override are future work. The binding representation must be
  extendable without putting combination behavior into `Modulation`.
- A logic-level disable is future work. Reserve a clear stage at which a future
  `signalEnabled`/`logicMuted` policy can discard hits before all internal
  bindings while allowing player phase to continue.
- A full graph editor is not part of this change. The first plugin integration
  uses a fixed composition and retains only the current practical editing
  surface.

## Core data model

### Stable identifiers

Use distinct strong IDs rather than one ambiguous ID space:

```text
PatternId
ModulationId
PatternPlayerId
ModulationPlayerId
VoiceId
VoiceParameterId
TriggerBindingId
ParameterBindingId
CommandBindingId
OutputBindingId / RouteId
```

A tagged `PlayerRef` may identify a pattern or modulation player when a command
target can be either type. Do not rely on vector indices as externally stable
identity, even if the first implementation uses fixed arrays internally.

### Pattern records

Keep `Pattern` rhythm-only:

```cpp
struct Pattern
{
    std::array<bool, maxLength> hits;
    std::size_t length;
};
```

Do not add voice IDs, routes, MIDI notes, modulation IDs, playback mode, advance
sources, or player relationships to a pattern record. Player-specific drafts,
offsets, windows, selected IDs, and clock settings remain outside the library.

### Modulation records

Replace the velocity library with a generic library. A modulation contains only
a fixed-capacity sequence of exact normalized values and its length:

```cpp
struct Modulation
{
    static constexpr std::size_t maxLength = 32;
    std::array<NormalizedValue, maxLength> values;
    std::uint8_t length;
};
```

Retain the existing unsigned 16-bit `NormalizedValue` representation, including
deterministic conversion helpers. It is generic, exact, trivially copyable, and
suitable for realtime snapshots. It must reject or safely clamp non-finite
floating-point input at non-realtime API boundaries.

The record must not contain target, mapping, advance policy, reset policy,
interpolation, MIDI CC, CV channel, or voice information. Those belong to the
player, voice parameter, or output binding.

The generic `ModulationLibrary` should retain the useful `PatternLibrary`-style
behavior already implemented by the velocity library: fixed stable storage,
immutable published entries, content deduplication, validation, startup
replacement, and a single-writer append path.

There is no legacy import requirement for `velocity-modulations.json`. Use a
new generic catalog name and format such as `modulations.json` and
`live-pattern-sequencer-modulation-library`.

## Player contracts and behavior

### Internal source signals

Do not use resolved `SequencerEvent` as the raw player-to-graph contract. Add a
small trivially-copyable internal signal representation, or equivalent typed
fixed buffers, for at least:

```text
PatternHit
PatternCycleBoundary
ModulationValue
```

A hit needs its timestamp, source `PatternPlayerId`, stable hit/trigger identity,
and the triggering pattern player's nominal step duration. The nominal duration
is used to interpret the voice Gate ratio even when that pattern player advances
from another player's hits.

A modulation value needs its timestamp, source `ModulationPlayerId`, exact
normalized value, and source step for diagnostics/UI.

The cycle boundary is a control signal, not an audible hit and not a resolved
output event. It is required to preserve current reset-to-master and
master-cycle-quantized pattern-selection behavior when the master pattern begins
with a rest.

### Shared playback semantics

Both player types need these concepts, without requiring an inheritance
hierarchy beyond whatever minimal realtime interface is useful:

```cpp
enum class PlayMode { Continuous, OneShot };
enum class PlayerCommand { Play, Stop, Reset, ResetAndPlay };

struct ClockAdvance
{
    double stepLengthPpq;
};

struct PatternHitAdvance
{
    PatternPlayerId source;
};

using AdvanceSource = tagged_union<ClockAdvance, PatternHitAdvance>;
```

Preserve the current PatternPlayer speed choices by mapping them to clock step
lengths derived from the existing base step of `0.25` PPQ.

Command semantics are:

- `Play`: resume from the current position without resetting. It does not
  synthesize a step immediately unless the player is in the reset-before-step-0
  state.
- `Stop`: stop advancement without changing the current position.
- `Reset`: stop and move to the state immediately before step 0.
- `ResetAndPlay`: move immediately before step 0, enter playing state, and
  evaluate step 0 at the command timestamp, even when already playing.

Duplicate same-timestamp commands are coalesced. If conflicting commands arrive
at one timestamp, resolve them deterministically with `Stop` as the safe winner,
then `ResetAndPlay`, then `Reset`, then `Play`. A `ResetAndPlay` consumes an
ordinary advance delivered to the same player at the same timestamp; it must not
emit step 0 and step 1 together.

For `OneShot`, every sequence position counts, including rests in a pattern.
After the final position has been evaluated, the player stops. `Play` at the
completed end does nothing until `Reset` or `ResetAndPlay` establishes a valid
position again.

The host transport remains the outer timing authority for the shipped plugin:

- host start or a transport discontinuity issues timestamped `ResetAndPlay` to
  the default players at the block start;
- a continuing host block advances only players whose state is playing;
- host stop stops processing and resets physical outputs;
- player commands control participation while the host transport is active.

### PatternPlayer

Refactor `PatternPlayer` so it:

- references one `PatternId`;
- owns pattern selection/draft state, playback window, offset, speed/clock
  configuration, play mode, advance source, cursor, and playback state;
- advances on its clock or actual source hits;
- emits a hit only when the evaluated pattern step is active;
- emits cycle-boundary control signals regardless of whether the boundary step
  is a hit;
- supports runtime commands and immediate step-0 evaluation;
- contains no modulation editor model, velocity library reference, modulation
  cursor, intensity calculation, gate scheduling, MIDI note, or output route.

Pattern changes should retain the current local-cycle and external-cycle
quantization behavior. Represent external synchronization through an explicit
cycle-boundary control relationship rather than the current special-case master
fields embedded in engine directives.

### ModulationPlayer

Add `ModulationPlayer` with:

- one selected `ModulationId` and a read-only `ModulationLibrary` reference;
- an independent cursor and playback state;
- `Continuous` and `OneShot` modes;
- `Clock` and `PatternPlayerHit(source)` advance sources;
- the four player commands;
- coherent requested-versus-active selection publication;
- a generic editor/status snapshot containing selected ID, current step, and
  playing state;
- a generic value signal for every evaluated step.

`ResetAndPlay` emits modulation step 0 at its timestamp. A hit-driven modulation
player emits its next value at the source-hit timestamp. It does not know which
voice parameter will consume the value.

In the default composition, the Pitch, Velocity, and Gate modulation players
for a voice all advance from the same PatternPlayer hit that triggers the voice.
This guarantees that their current values are established before the voice
samples them.

## Voice model

### Voice parameters

A voice owns a fixed-capacity collection of parameter descriptors and current
states. Use stable parameter IDs so built-in and future properties share one
mechanism.

Each descriptor must include at least:

```text
VoiceParameterId
semantic role or key
SampledOnTrigger / Continuous behavior
mapping from normalized modulation to the voice's semantic unit
interpolation policy where relevant
whether the parameter is required to resolve a trigger
```

Meaning and mapping belong here, not in the modulation record or player.

Initial built-in roles are:

- Pitch: `SampledOnTrigger`, normalized input mapped to the supported pitch
  range, initially MIDI-compatible 0 through 127 semitones.
- Velocity/Intensity: `SampledOnTrigger`, normalized input mapped to 0 through
  1.
- Gate: `SampledOnTrigger`, normalized input interpreted as 0 through 1 times
  the triggering PatternPlayer's nominal step duration.
- Generic continuous control: `Continuous`, normalized input retained as a
  resolved control value with an interpolation policy.

There are no implicit voice parameter defaults. Every required initial value is
provided by a bound `ModulationPlayer`, including constant values represented as
one-step modulation sequences.

If a trigger arrives before every required parameter has a valid current value,
the voice must not emit a malformed trigger. Drop that resolved trigger safely
and expose a bounded diagnostic counter/status. Do not silently invent a pitch,
velocity, or gate value.

### Parameter updates

When a bound modulation value arrives:

- map the normalized value with the destination voice parameter descriptor;
- update that parameter's current value;
- for `SampledOnTrigger`, emit nothing yet;
- for `Continuous`, emit a resolved control event immediately, even if the
  voice has not been triggered.

Phase one validates that there is no more than one active binding for a
`(VoiceId, VoiceParameterId)` pair. Keep bindings as records rather than storing
a direct source pointer in the parameter so a future combine policy can allow
multiple sources without changing modulation data.

### Trigger resolution and gate ownership

Phase one validates at most one active trigger source per voice. Keep
`TriggerBinding` as a separate table so this restriction can later be relaxed.

On a valid trigger, the voice:

1. samples its current Pitch, Velocity, and Gate values;
2. creates a voice-scoped trigger identity;
3. emits a resolved `triggerStart` at the hit timestamp;
4. schedules the matching `triggerEnd` at
   `hitPpq + gateRatio * nominalStepLengthPpq`.

A Gate value of zero produces no audible resolved trigger. For a positive gate,
ensure the resolved end is at least one sample after the start so timestamp
rounding cannot sort the matching end before its own start. An end belonging to
an older active trigger still precedes a replacement start at the same frame.

Repeated triggers are monophonic for phase one. If a voice is already active,
emit its end before the replacement start at the new timestamp. This also
defines the exact-boundary case when a normal end and new hit coincide.

## Runtime bindings and graph

### Binding records

Provide fixed-capacity runtime records equivalent to:

```cpp
struct TriggerBinding
{
    PatternPlayerId source;
    VoiceId destination;
};

struct ParameterBinding
{
    ModulationPlayerId source;
    VoiceId voice;
    VoiceParameterId parameter;
};

struct CommandBinding
{
    ControlSource source; // PatternPlayer Hit or CycleBoundary port
    PlayerRef destination;
    PlayerCommand command;
};
```

`ControlSource` must distinguish a PatternPlayer's Hit and CycleBoundary ports;
they are not interchangeable. Permanent hit-to-command bindings implement the
new runtime relationships. Preserve the current one-shot "reset to master"
operation as an armed command delivered at the next selected CycleBoundary,
not as a permanent binding that would restart the follower every master cycle.

Likewise, preserve cycle-quantized pattern activation with an explicit
transition policy such as `localCycle`, `immediate`, or
`externalCycle(PatternPlayerId)`. The plugin may retain the user-facing idea of
a selected master, but the core should resolve it into these explicit control
sources and policies rather than broadcasting master directives to every
player.

An advance source may be stored directly in the destination player's published
configuration or normalized into an equivalent advance-binding table. Do not
maintain both as independently mutable truths.

An output binding links a resolved voice signal to an already registered output
endpoint/renderer configuration. It must support:

- one voice routed to MIDI and CV simultaneously;
- trigger start/end routing;
- sampled pitch and velocity combination in the MIDI renderer;
- gate and pitch routing in the CV renderer;
- a continuous voice parameter routed independently to MIDI CC, CV, or a future
  internal destination;
- renderer-specific channel, CC, voltage, range, and calibration configuration
  outside Pattern, Modulation, and their players.

### Same-timestamp evaluation order

The current process-each-player-then-sort design is insufficient because a hit
can synchronously advance or restart another player. Implement a bounded,
deterministic event scheduler or an equivalent topologically ordered evaluation.

At one timestamp, the observable order is:

1. apply/coalesce player commands and advances;
2. evaluate any resulting player steps, including immediate step 0 from
   `ResetAndPlay`;
3. propagate resulting command/advance cascades through the acyclic control
   graph;
4. apply all resulting modulation values to voice parameter state;
5. deliver pattern hits to voice trigger bindings;
6. resolve voice trigger ends before starts;
7. route resolved continuous controls and trigger events to renderers using
   stable binding order as the final tie-breaker.

This order is required: if one hit advances a modulation player and triggers a
voice, the voice samples the newly advanced value.

`ResetAndPlay` may emit a step-0 hit/value that cascades at the same timestamp.
The scheduler must support that behavior without recursion. Use fixed-capacity
work storage and iterative processing.

### Graph validation

Validate a complete candidate graph before publication. At minimum reject:

- missing or wrongly typed source/destination IDs;
- self-advance or self-command edges;
- every directed cycle in the advance/command graph;
- duplicate binding IDs;
- duplicate equivalent bindings where they create ambiguous delivery;
- more than one trigger binding targeting a voice in phase one;
- more than one parameter binding targeting one voice parameter in phase one;
- output bindings that reference unregistered endpoints or unsupported signals;
- graphs whose node/edge counts exceed prepared fixed capacities.

Reject all control cycles rather than attempting bounded feedback. Fan-out and
same-timestamp cascades are bounded by graph capacities, but queue overflow must
still be detected and handled with the same fail-safe philosophy as event-buffer
overflow.

### Runtime publication

Player/voice/renderer registries and their object lifetimes are fixed before
`prepare()`. Runtime editing covers:

- player advance sources;
- command bindings;
- trigger bindings;
- parameter bindings;
- output bindings/routes;
- play mode and other fixed-size player/voice configuration.

The control thread builds and validates a complete fixed-capacity snapshot,
then publishes it atomically for adoption at an audio block boundary. The audio
thread must observe either the old graph or the new graph, never a mixture.

Do not allocate or free a shared graph through the audio thread. A double/triple
buffered snapshot with generation/ownership acknowledgement, or another bounded
reclamation scheme, is preferable to `shared_ptr` destruction on the callback.
Do not spin waiting for a writer.

Hot publication does not authorize adding or destroying players, voices, or
renderers during playback. Those registry changes require stopping and
re-preparing the engine.

## Mute, suppression, and future logical disable

This section intentionally overrides the source requirement that a suppressed
hit should not cause downstream advancement.

Mute and suppression are output-only policies:

- the PatternPlayer still advances and emits its logical hit;
- the hit still advances ModulationPlayers;
- the hit still fires command bindings and can restart another player;
- the hit still reaches its Voice and updates logical voice state;
- player and modulation phase continue unchanged;
- the resolved audible trigger is withheld from MIDI Note On and CV gate/trigger
  routes;
- continuous parameter/control routing is not muted unless a future explicit
  route policy says otherwise.

Preserve directed simultaneous-hit suppression: if A suppresses B and both hit
at the same timestamp, B's logical graph activity still occurs, but B's resolved
audible trigger is marked ineligible for trigger outputs.

The output-suppression decision must follow the trigger identity through its
lifetime. Never deliver an end for a start that was withheld, and never let a
withheld trigger release another active MIDI note or CV gate.

Do not implement the future logical-disable feature as another spelling of
mute. Its reserved behavior is different: phase continues, but the hit is
discarded before command, advance, parameter, and voice-trigger graph delivery.
`Stop` is also different because it freezes player advancement.

## Resolved events and output routing

Keep `SequencerEvent` as a representation of resolved semantic actions, not raw
library data or graph wiring. It should continue to be fixed-size and trivially
copyable and should represent at least:

```text
triggerStart: trigger ID, resolved pitch, resolved intensity
triggerEnd: matching trigger ID
controlPoint: voice parameter ID, normalized/resolved value, interpolation
```

The routed envelope should identify the source `VoiceId` rather than the source
player and include the selected route/output binding ID plus the validated frame
offset and stable order. Do not put arrays of modulation values, `ModulationId`,
or binding pointers in events.

Remove fixed-pitch mapping from the player-to-renderer route. Pitch now comes
from the voice's bound Pitch modulation. MIDI Note On remains assembled in the
MIDI renderer by combining the resolved pitch and intensity carried by the
voice trigger. Trigger End must use the exact MIDI note remembered for the
matching start.

Semantic intensity zero does not cancel a logical trigger. When an otherwise
valid audible trigger is rendered to MIDI, avoid encoding it as velocity zero
(which MIDI receivers commonly treat as Note Off); clamp the concrete Note On
velocity to at least 1. Gate zero, mute, and suppression are the mechanisms that
withhold the audible start.

Adapt CV routing so a voice can independently route gate, pitch, and continuous
parameters. Preserve exact trigger identity, held values across eventless
blocks, interpolation, channel ownership validation, and reset-to-zero behavior.

Maintain deterministic final ordering: controls, trigger ends, then trigger
starts at the same frame, followed by stable graph/binding order.

## Default plugin composition

The first integration remains intentionally fixed rather than adding a graph
editor.

For each of the existing ten drum rows, construct:

- one `PatternPlayer` using the row's existing selected pattern and clock
  behavior;
- one `Voice`;
- one Pitch `ModulationPlayer` referencing a one-step generic modulation whose
  normalized value maps to the row's current MIDI note (36 through 45);
- one Velocity `ModulationPlayer` referencing the selected generic modulation;
- one Gate `ModulationPlayer` referencing a one-step generic modulation with
  value `0.5`;
- three parameter bindings into that Voice;
- one trigger binding from the PatternPlayer to the Voice;
- hit-based advance sources for all three ModulationPlayers;
- MIDI and CV output bindings for the Voice.

The one-step pitch and gate records are ordinary reusable modulation records,
not hidden voice defaults. Different ModulationPlayers may reference the same
record while retaining independent runtime state.

Represent the two current velocity shapes generically using exact normalized
values equivalent to the current 8-bit data:

```text
201
255, 100, 225, 150
```

The core names and catalog must not call these velocity records. The existing UI
may still label the currently edited destination as Velocity because that is a
voice-parameter meaning supplied by its binding.

Remove the additional Pulse row and its routes. The plugin's default CV layout
therefore needs channels only for the ten voices unless another fixed route
requires more.

Retain the current pattern, velocity-destination editing, mute, suppression,
master/reset, MIDI, and CV user functionality by redirecting the processor/UI
facade to the appropriate PatternPlayer, ModulationPlayer, Voice, or graph API.
Pitch and Gate constant sequences need not receive new editor controls in this
phase.

## Source-level change map

### Add

- `src/core/ModulationLibrary.h/.cpp`
- `src/core/ModulationPlayer.h/.cpp`
- `src/core/Voice.h/.cpp`
- a runtime graph/snapshot implementation, preferably isolated in
  `RuntimeGraph.h/.cpp` or equivalently clear files
- focused tests for the new library, player, voice, graph validation, graph
  publication, and cross-node scheduling
- a generic modulation catalog store in the plugin layer

### Refactor

- `SequencerTypes.h`: split raw source signals from resolved voice events; add
  strong IDs, play/command/advance types, voice parameter types, and updated
  routed-event source identity.
- `IPlayer.h`: make the realtime contract operate on player source signals and
  commands/configuration without voice or renderer knowledge.
- `IPlayerEditorModels.h`: PatternPlayer exposes only pattern status;
  ModulationPlayer exposes generic modulation status.
- `PatternPlayer.h/.cpp`: remove all velocity/modulation/gate/event-combination
  code while preserving rhythm timing and editor behavior.
- `SequencerEngine.h/.cpp`: replace direct player-to-renderer routing and
  special-case master directives with graph evaluation, voice resolution,
  validated hot snapshots, and voice-to-output routing.
- `IOutputRenderer.h`: retain block lifecycle but update routed event identity
  and signal selection as required.
- `MidiBufferRenderer.h/.cpp`: consume resolved Voice triggers and continuous
  MIDI-control output bindings.
- `CvBufferRenderer.h/.cpp`: consume voice gate/pitch/control bindings while
  preserving held-state and reset behavior.
- `PluginProcessor.h/.cpp`: own heterogeneous PatternPlayers,
  ModulationPlayers, Voices, graph snapshots, and new routes; build the fixed
  default composition; publish UI snapshots from the correct objects.
- `PluginEditor.h/.cpp`: keep the existing practical surface but make its
  velocity panel edit the ModulationPlayer bound to the selected Voice's
  Velocity parameter.
- `CMakeLists.txt`: replace removed sources/tests and register all new targets.
- `docs/core-architecture.md` and `docs/pattern-to-hardware-midi.md`: rewrite
  flows and terminology after behavior is passing.

### Remove or replace

- `src/core/VelocityModulationLibrary.h/.cpp`
- `src/core/ModulationLane.h/.cpp`
- `src/core/PulsePlayer.h/.cpp`
- `src/plugin/VelocityModulationLibraryFileStore.h/.cpp`
- the matching velocity-library, modulation-lane, and pulse-player tests
- velocity fields from `PatternPlayerPersistentState`
- `PatternPlayer`'s `IModulationEditorModel` implementation
- `RouteMapping::fixedPitch` and direct player-to-renderer connections
- engine master/reset fields whose behavior is superseded by explicit
  cycle-control bindings
- legacy plugin-state migration code and tests; replace them with the clean new
  host-state schema and its round-trip tests

Reuse small validated pieces such as `NormalizedValue`, trigger identity,
fixed-buffer mechanics, PPQ-to-frame conversion, renderer lifecycle, and the
append-only library implementation where they still match the new ownership
boundaries.

## Implementation sequence

### Phase 0: characterization and scaffolding

- Keep the current suite green before contract changes.
- Add focused characterization tests for pattern timing, cycle boundaries,
  selection quantization, speed changes, mute/suppression logical phase, exact
  MIDI frames, CV holds, and output cleanup.
- Introduce strong IDs and fixed-capacity helper storage without changing
  behavior.

### Phase 1: generic modulation data

- Add `Modulation` and `ModulationLibrary`.
- Replace the velocity-specific catalog implementation and tests with generic
  equivalents.
- Add the new generic file store and format; no legacy import path.
- Move the reusable `NormalizedValue` type to a neutral header if appropriate.

### Phase 2: split player responsibilities

- Strip modulation and gate scheduling out of PatternPlayer so it emits only
  hits and cycle signals.
- Add ModulationPlayer with both advance sources, both play modes, all commands,
  independent state, and generic editor snapshots.
- Delete PulsePlayer and ModulationBank/Lane after their useful behavior has an
  explicit destination in the new architecture.
- Keep direct player unit tests independent of Voice and JUCE.

### Phase 3: Voice resolution

- Add voice parameter descriptors/state and parameter binding application.
- Implement required-value validation, continuous updates, sampled trigger
  combination, gate scheduling, retrigger ordering, and diagnostics.
- Keep Voice tests independent of output protocols.

### Phase 4: static graph scheduler

- Introduce binding records, validation, topological ordering, and a bounded
  same-timestamp work scheduler.
- Replace special engine master/follower wiring with cycle-control bindings.
- Prove command/advance cascades, new-value-before-trigger ordering, cycle
  rejection, deterministic fan-out, and overflow containment.
- Initially build the graph before prepare; do not combine this step with hot
  publication until static behavior is correct.

### Phase 5: voice-to-output routing

- Change routed identity from Player to Voice.
- Adapt MIDI and CV renderers and route configurations.
- Apply mute/suppression only at audible trigger routing after all logical graph
  work.
- Preserve exact sample position, same-frame priority, trigger pairing, held
  CV, empty-block rendering, and failure/reset behavior.

### Phase 6: runtime graph publication

- Add message-thread candidate mutation APIs and whole-graph validation.
- Publish fixed-capacity immutable snapshots at block boundaries.
- Add concurrency tests proving each block observes one coherent graph
  generation and that the audio path never waits for or destroys control-thread
  storage.

### Phase 7: plugin and UI integration

- Build the ten-voice fixed composition and its Pitch, Velocity, and Gate
  ModulationPlayers.
- Repoint existing UI operations to the correct new objects.
- Remove the Pulse row and velocity-specific storage types.
- Retain simultaneous MIDI and CV routing.
- Replace host state with the clean graph/configuration schema and add
  new-format round-trip tests only.

### Phase 8: cleanup and documentation

- Delete temporary adapters and obsolete types.
- Audit core headers for JUCE independence.
- Run allocation/realtime checks and sanitizers where available.
- Rewrite the architecture and hardware-MIDI walkthroughs to show
  PatternPlayer -> Voice and ModulationPlayer -> Voice flows.
- Ensure all public contracts document ownership, thread, capacity, and failure
  behavior.

## Required tests and acceptance criteria

### Libraries

- Generic modulations validate length and exact normalized values.
- Equal content deduplicates regardless of name and ignores unused tail storage.
- Multiple ModulationPlayers referencing one record have independent cursors.
- Pattern records remain rhythm-only and existing pattern library behavior
  remains intact.

### Player behavior

- PatternPlayer compiles and operates without any modulation type or API.
- Clock advance preserves current pattern timing, speed, window, offset, and
  half-open block behavior.
- Hit advance advances exactly once per actual logical source hit, not on a rest
  or nonexistent candidate.
- Output mute and suppression do not prevent hit-driven advancement.
- Both player types implement Continuous and OneShot, including rest/final-step
  behavior.
- Play, Stop, Reset, and ResetAndPlay satisfy the defined cursor semantics.
- ResetAndPlay while already playing re-emits step 0 at the command timestamp.
- A coincident ResetAndPlay and advance produces only step 0.
- Pattern cycle boundaries are available even when the boundary step is a rest.

### Graph behavior

- A hit can restart one player and independently advance another.
- ResetAndPlay step-0 output can cascade through more than one acyclic graph
  level at the same timestamp.
- Every self-edge and directed control cycle is rejected before publication.
- Invalid, duplicate, over-capacity, and phase-one multiple-source bindings are
  rejected without changing the active graph.
- Graph replacement is atomic at a block boundary.
- Stable results do not depend on registration vector iteration accidents.
- Queue/event overflow is observable and cannot leave an output active.

### Voice behavior

- A modulation sequence can be reused as Pitch on one Voice and another
  parameter on another Voice without changing the sequence.
- Modulation meaning and mapping are absent from ModulationPlayer.
- A voice trigger samples the newly advanced Pitch, Velocity, and Gate values at
  the same timestamp.
- A continuous parameter emits without a voice trigger.
- A missing required parameter drops the resolved trigger and increments a
  bounded diagnostic.
- Gate `0.5` releases at half of the triggering player's nominal step duration.
- A retrigger emits the prior end before the new start.

### Mute and suppression

- Muted PatternPlayer hits still advance bound ModulationPlayers and fire
  command bindings.
- A muted or suppressed hit produces no MIDI Note On and no CV gate rise.
- Continuous controls remain routable while triggers are muted.
- Directed simultaneous suppression does not change either player's phase.
- A withheld start never emits an unmatched physical end.
- Unmuting resumes output at the player's current phase.

### Rendering and integration

- The default first drum Voice resolves pitch 36 from a one-step Modulation and
  produces the same MIDI note/sample position as today.
- Existing velocity shapes map across the MIDI range exactly as their generic
  normalized equivalents.
- MIDI Note Off uses the note remembered for its matching Voice trigger.
- One Voice can drive MIDI and CV simultaneously.
- CV gate/pitch/control hold across eventless blocks and reset safely.
- Player or graph event failure resets affected physical outputs without
  silently losing a release.
- The default plugin exposes ten PatternPlayer/Voice rows and no Pulse row.
- Existing pattern and Velocity-destination editing remains usable through the
  new ownership model.
- New-format catalog and host-state round trips pass; no legacy migration test
  is required.

### Realtime constraints

- No heap allocation, dynamic container growth, locks, blocking, filesystem
  access, or recursive graph traversal occurs in `processBlock()` or engine
  processing after prepare.
- All realtime event/signal buffers, graph work queues, voice state, and route
  batches have explicit capacities and overflow diagnostics.
- Control publication never makes the audio thread spin.
- Same input graph, timeline, and commands produce byte-for-byte deterministic
  ordered event results.

## Definition of done

The refactor is complete only when:

- PatternPlayer has no modulation or output knowledge;
- ModulationPlayer and Voice exist as independently tested core concepts;
- pitch, velocity, and gate in the default plugin all come through generic
  ModulationPlayer bindings, including one-step constants;
- command, advance, trigger, parameter, and output relationships are explicit
  validated records;
- runtime graph edits publish coherently at block boundaries;
- mute/suppression preserve logical graph activity while withholding audible
  trigger output;
- MIDI and CV are both driven from resolved Voice events;
- PulsePlayer and velocity-specific core/storage abstractions are gone;
- all replacement tests pass and obsolete expectations are removed;
- architecture and end-to-end documentation describe the implemented graph,
  not the pre-refactor direct-player routing.
