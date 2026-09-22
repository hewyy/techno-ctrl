# Core architecture

## Runtime signal graph

The sequencer core is a bounded runtime signal graph. Library records describe
reusable data, players own cursors, voices assign musical meaning, and output
renderers convert resolved events to a concrete protocol.

```text
PatternLibrary                         ModulationLibrary
      |                                       |
      v                                       v
PatternPlayer -- hit/cycle --> RuntimeGraph <-- value -- ModulationPlayer
                                  |
                                  v
                                Voice
                                  |
                     resolved semantic events
                                  |
                           output bindings
                            /           \
                         MIDI            CV
```

No Pattern or Modulation record contains a voice, route, MIDI note, CV channel,
or relationship to another player.

## Data and runtime ownership

`PatternLibrary` and `ModulationLibrary` use fixed stable storage. Published
records are immutable and retain their addresses for the library lifetime. A
single control thread may append a record after persistence succeeds; realtime
readers never allocate or lock.

`Pattern` contains only a hit sequence and length. `Modulation` contains only a
fixed sequence of exact unsigned 16-bit `NormalizedValue` values and a length.
The same record can be selected by multiple players without sharing cursor
state.

`PatternPlayer` owns rhythm playback state:

- selected and active Pattern IDs;
- the editable 32-step draft, offset, inclusive window, and playback speed;
- clock- or hit-based advance, continuous or one-shot playback, and cursor;
- local, immediate, or external-cycle pattern transition policy;
- hit and cycle-boundary signal generation.

It has no velocity, pitch, gate, voice, MIDI, CV, or trigger-end state.

`ModulationPlayer` owns a selected Modulation ID, draft, cursor, advance source,
and play mode. It emits a generic normalized value. The destination parameter
is deliberately unknown to it.

`Voice` owns fixed parameter descriptors and current parameter values. A
descriptor supplies the role, sampled or continuous behavior, normalized
mapping, interpolation policy, and required-for-trigger flag. The built-in
composition uses Pitch, Intensity, and Gate descriptors plus device-specific
continuous controls. Sampled values may be scoped to a PatternPlayer, allowing
several rhythm/pitch pairs to share one Voice without overwriting one another.
A voice samples the triggering player's scoped values on a hit, creates a
voice-scoped trigger ID, and owns the matching end.

## Signals and events

Player-to-graph traffic uses `PlayerSignal`, not `SequencerEvent`:

- `patternHit` carries PatternPlayer identity, PPQ, hit identity, and nominal
  step duration;
- `patternCycleBoundary` is control-only and is emitted even if step zero is a
  rest;
- `modulationValue` carries ModulationPlayer identity, PPQ, exact normalized
  value, and source step.

After parameter application and trigger resolution, a `SequencerEvent` is a
semantic action:

- `controlPoint` identifies a continuous Voice parameter and interpolation;
- `triggerEnd` identifies the exact active trigger to release;
- `triggerStart` carries resolved pitch and intensity.

`RoutedEvent` adds Voice identity, Route ID, validated frame offset, and stable
order. There is no player-level fixed-pitch fallback.

## Bindings

`RuntimeGraphConfig` is a fixed-capacity snapshot containing:

- PatternPlayer and ModulationPlayer runtime configurations;
- `CommandBinding` from a Pattern hit or cycle port to a player command;
- `ParameterBinding` from a ModulationPlayer to a Voice parameter, optionally
  scoped to the PatternPlayer whose trigger consumes the sampled value;
- `TriggerBinding` from a PatternPlayer to a Voice;
- `OutputBinding` from a Voice trigger or continuous parameter to a registered
  output endpoint and Route ID.

Several PatternPlayers may trigger one Voice. A sampled Voice parameter may
have one modulation source per distinct PatternPlayer scope; continuous
parameters remain Voice-wide. Fan-out is allowed, including one Voice routed to
MIDI and CV at the same time.

Validation rejects missing or wrongly typed nodes, invalid endpoints or routes,
duplicate IDs and equivalent bindings, self edges, directed control cycles,
unsupported continuous outputs, colliding parameter sources, and capacity
overflow. Rejection leaves the published graph unchanged.

## Same-timestamp scheduling

`RuntimeGraph::process()` uses fixed work storage and iterative propagation.
For one timestamp it:

1. propagates player advances, external-cycle transitions, armed source
   commands, and permanent command bindings;
2. evaluates immediate player output and bounded cascades;
3. invalidates and regenerates precomputed future player signals when a command
   changes phase inside a block;
4. applies all modulation values to Voice parameters;
5. delivers Pattern hits to Voice trigger bindings;
6. orders continuous controls, old trigger ends, and new trigger starts.

Modulation is applied before a simultaneous hit is sampled. Processing remains
half-open in PPQ, and work, source-signal, semantic-event, and route buffers all
have explicit capacities. Overflow is reported as failure so the wrapper can
reset logical and physical state.

`ResetAndPlay` evaluates step zero at its command timestamp. An ordinary
same-timestamp advance is consumed. Armed one-shot commands may fire from a
pattern hit or cycle boundary; they are not permanent source special cases.

## Voice trigger lifetime

A trigger is dropped and diagnosed if any required parameter is missing. Gate
zero produces no audible resolved trigger. A positive gate ends at:

```text
hit PPQ + gate ratio * triggering PatternPlayer nominal step duration
```

The minimum positive gate is one sample in PPQ. A Voice keeps one active trigger
lifetime per PatternPlayer: a new hit retriggers that player's prior note while
different players can overlap. Renderers remember the concrete output state
using Voice, Route, and Trigger identity, so a note-off releases the note
selected by its matching start.

## Output policy and rendering

Mute and directed simultaneous-hit suppression are output-only. Logical hits
still advance modulation, fire commands, reach Voices, and preserve phase.
Only audible trigger bindings are withheld; continuous controls remain
routable. Eligibility is remembered for the trigger lifetime, so a withheld
start never produces an unmatched MIDI note-off or CV gate release.
Suppression edges are stored between PatternPlayer IDs. Voice-level editing is
a UI convenience that expands a relationship to every source/target player pair
owned by those Voices.

`MidiBufferRenderer` combines resolved pitch and intensity. Pitch is rounded and
clamped to MIDI note 0 through 127. A concrete note-on velocity is clamped to 1
through 127 so semantic intensity zero cannot become MIDI note-off. Configured
continuous routes render Voice control points as 7-bit MIDI CC messages.

`CvBufferRenderer` owns validated gate, pitch, and control channels for each
Route ID. Gate and pitch follow resolved Voice triggers; continuous controls can
step or ramp. Values hold through eventless blocks and reset to zero on stop,
discontinuity, overflow, or renderer failure.

## Runtime publication and threading

Players, Voices, and renderer endpoints are registered before `prepare()` and
retain stable lifetimes. A single control thread builds and validates a complete
fixed-size candidate. `RuntimeGraph::publish()` writes an unused slot in a
triple-buffered snapshot store and publishes its index with release ordering.
The audio thread adopts at the next block boundary and acknowledges the slot.
It never allocates, frees shared graph memory, spins, or waits.

The UI may edit library-backed player drafts through their atomic snapshot APIs.
Graph topology and fixed player runtime settings move through graph snapshots.
Registry changes still require stopping and reconstructing the processor.

## Default plug-in composition

The plug-in constructs ten drum players, one Synth 1 player, and three Synth 2
players. Each pattern player owns:

- one clock-driven PatternPlayer;
- hit-driven Pitch, Velocity-destination, and Gate ModulationPlayers;
- three parameter bindings and one trigger binding;

The three Synth 2 players share one Voice and MIDI destination. Their sampled
bindings are scoped by PatternPlayer. Sixteen additional generic modulation
players target the Volca Keys CC parameters and can advance from an associated
pattern hit or independently from the 1/16-note clock.

The focused Synth 2 page uses the same persistent Pattern and Modulation
libraries as the All Voices page. Its selection menus render previews, expose
save actions for edited drafts, and its rhythm grids expose draggable playback
start/end brackets. Each modulation lane has one Advance On selector containing
Clock and the three individual Pattern hit sources. Every rhythm and modulation
row has an independent queued reset. Rhythm rows restart at the next BD1 cycle
boundary. A hit-driven modulation restarts with its first value on the next hit from
its selected Pattern, while a clock-driven modulation restarts at the next BD1
cycle boundary even when that master step is a rest.

Pitch uses a reusable one-step modulation mapped to notes 36 through 45. Gate
uses a shared reusable one-step half-gate modulation. Velocity editing targets
the generic ModulationPlayer bound to the Voice's Intensity parameter. The UI
may call that destination “Velocity”; the underlying catalog and player remain
generic.

BD1 is the fixed cycle source for external-cycle selection, rhythm resets, and
clock-modulation resets. Hit-driven modulation resets use their selected Pattern
as the source. Host start and discontinuity reset the runtime at the block
start. Host stop resets physical outputs.

Host state writes `live-pattern-sequencer-graph-state` schema version 2 and
continues to read version 1. It
persists Pattern and Modulation references and drafts plus mute and suppression
configuration, Synth 2 parameter lanes, advance modes, and hit-source choices.
Transient cursors, queued work, and active triggers are not serialized.
Unsupported formats or versions are rejected.

## Source map

| Responsibility | Files |
|---|---|
| IDs, signals, semantic and routed events | `src/core/SequencerTypes.h` |
| Rhythm playback | `src/core/PatternPlayer.h/.cpp` |
| Generic modulation data and playback | `src/core/ModulationLibrary.h/.cpp`, `src/core/ModulationPlayer.h/.cpp` |
| Parameter meaning and trigger lifetime | `src/core/Voice.h/.cpp` |
| Validation, publication, scheduling, policy, routing | `src/core/RuntimeGraph.h/.cpp` |
| MIDI and CV protocol conversion | `src/plugin/MidiBufferRenderer.*`, `src/plugin/CvBufferRenderer.*` |
| Fixed composition, host transport, state, UI facade | `src/plugin/PluginProcessor.*` |

Focused tests mirror these boundaries in `tests/PatternPlayerTests.cpp`,
`tests/ModulationPlayerTests.cpp`, `tests/VoiceTests.cpp`,
`tests/RuntimeGraphTests.cpp`, `tests/MidiBufferTransportTests.cpp`, and
`tests/CvBufferRendererTests.cpp`.
