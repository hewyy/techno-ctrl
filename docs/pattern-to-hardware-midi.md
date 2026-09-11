# End-to-end example: from one pattern hit to a hardware synth

This walkthrough follows one concrete BD1 hit through the shipped JUCE VST3,
from a user's edit to an audible hardware-synth response. It expands the
[core architecture guide](core-architecture.md) with exact values and source
links. The same boundaries apply to another plugin format or host adapter.

The most important boundary is this:

> `src/core` schedules and routes a protocol-neutral trigger. The JUCE wrapper
> writes MIDI into the host's callback buffer. REAPER then forwards that MIDI to
> a physical output. Neither the core nor `MidiBufferRenderer` opens a hardware
> MIDI device.

MIDI also carries no audio. The synthesizer's audio output needs a separate path
to speakers, a mixer, or an audio interface.

## Concrete result

The example deliberately uses the fresh instance defaults:

| Setting | Value |
| --- | --- |
| Player | `BD1`, player ID 0 and the default master |
| Hit pattern | `Basic Kick = x---` |
| Active playback window | steps `[0, 3]`, inclusive |
| Playback speed | `1x` |
| Velocity modulation | `Steady 100 = [201]` |
| MIDI route | fixed note 36 |
| MIDI renderer | channel 1 |
| Host tempo | 120 BPM |
| Sample rate | 48,000 Hz |
| Callback size | 512 frames |
| Play position | PPQ 8.0 |

With BD1 unmuted and unsuppressed, pressing Play produces a channel-1 note-on
for note 36 at MIDI velocity 100. At normal speed, the note ends 62.5 ms later
and the four-step pattern repeats after 500 ms.

```text
control path
  user step/velocity edits
    -> PatternPlayer draft atomics

real-time event path
  REAPER playhead
    -> TimelineBlock
    -> PatternPlayer::process()
    -> SequencerEvent triggerStart/triggerEnd
    -> SequencerEngine validation, filtering, routing, and frame mapping
    -> RoutedEvent
    -> MidiBufferRenderer
    -> juce::MidiBuffer
    -> REAPER track MIDI hardware output
    -> OS MIDI driver
    -> USB-MIDI or DIN-MIDI interface
    -> hardware synth MIDI input

separate audio path
  hardware synth audio output
    -> mixer, speakers, or audio-interface input
    -> optional monitored/recorded REAPER audio track
```

## 1. Wire the hardware and host

Before pressing Play:

1. Connect the computer directly to the synth over USB-MIDI, or connect a MIDI
   interface's **OUT** port to the synth's **IN** port with a DIN cable.
2. Set the synth to receive MIDI channel 1 or Omni.
3. Make sure MIDI note number 36 triggers the intended sound. Use the number,
   because manufacturers disagree on whether note 36 is named C1 or C2. A
   melodic synth will play its ordinary patch at note 36; `BD1` does not make
   that note a kick by itself.
4. Connect the synth's audio outputs to speakers or a mixer. To hear or record
   it through REAPER, connect those outputs to an audio interface and monitor a
   separate audio-input track.
5. In REAPER's **Options/Preferences -> Audio -> MIDI Devices**, enable the
   interface or synth under MIDI outputs.
6. Add **Live Pattern Sequencer MVP** to a track. A downstream software
   instrument is not required for this hardware example.
7. Open or right-click that track's **ROUTE** control. Select the required
   **MIDI Hardware Output** and preserve the source channel or map it to channel
   1. REAPER documents direct external-synth output in its
   [User Guide](https://www.reaper.fm/userguide.php).

The current note map and channel are compiled into the wrapper rather than
editable in the UI. If the synth expects another note or channel, remap in
REAPER/the synth, or change `defaultDrumVoices` and `drumMidiChannel` in
[`PluginProcessor.cpp`](../src/plugin/PluginProcessor.cpp) and
[`PluginProcessor.h`](../src/plugin/PluginProcessor.h), then rebuild.

Track record-arm and a MIDI item are not required just to run this
transport-driven plugin. REAPER must, however, keep processing the track's FX.

## 2. Define the hit and velocity in the shipped UI

Do this while stopped for the least ambiguous first run:

1. In the `BD1` row, select **Basic Kick**.
2. Set the playback range brackets to steps 1 through 4. In core indexes that is
   the inclusive window `[0, 3]`.
3. Ensure the first cell is lit and the next three are dark: `x---`. Clicking a
   cell toggles it, so clicking the already-lit first cell would remove the hit.
4. Select **Steady 100** in the velocity-modulation selector.
5. Keep its lane length at one and set the one visible knob to `201` on the
   editor's 0-through-255 scale.
6. Make sure BD1 is not muted and no other row suppresses it in the suppression
   matrix. Mute other rows if the goal is to hear this hit in isolation.

The fresh instance already has exactly these BD1 defaults. The explicit steps
above show which state matters.

The UI call path is:

```text
click pattern cell
  -> LivePatternSequencerEditor::contentMouseDown()
  -> LivePatternSequencerProcessor::togglePlayerStep(playerIndex, step)
  -> PatternPlayer::toggleStep(visibleStep)
  -> editableHitMask_ bit is XOR-toggled

move velocity knob
  -> Slider::onValueChange
  -> setPlayerVelocityModulationValue(playerIndex, laneStep, uint8 value)
  -> PatternPlayer::setVelocityModulationValue(step, value)
  -> editableVelocityValues_[step]
```

See [`PluginEditor.cpp`](../src/plugin/PluginEditor.cpp),
[`PluginProcessor.cpp`](../src/plugin/PluginProcessor.cpp), and
[`PatternPlayer.cpp`](../src/core/PatternPlayer.cpp).

These operations change the player's draft. Saving is not required to make it
play; processing sees the coherent draft on a subsequent callback. Pattern and
velocity saves are independent:

- pattern save takes the inclusive bracketed loop, applies the offset, crops it,
  and rebases the saved result to step zero;
- velocity save captures the active 1-through-10-value lane;
- equivalent content selects the existing catalog entry; new content requires a
  name and is persisted before publication;
- while playing, a new pattern selection waits for an appropriate cycle
  boundary. A newly selected velocity preset activates at a process-block
  boundary and resets its own cursor.

The editor's visible step is transformed by the pattern offset. With the offset
used here, zero, visible step 0 is source bit 0. In general the source bit is:

```text
sourceStep = (visibleStep - patternOffset) mod 32
```

## 3. Equivalent construction through the C++ APIs

The two catalog records are ordinary data:

```cpp
lps::Pattern hitPattern;
hitPattern.length = 4;
hitPattern.hits[0] = true;  // x---

lps::VelocityModulation velocity;
velocity.length = 1;
velocity.values[0] = 201;
```

This setup-only example shows the ownership and routing calls. It uses the
JUCE-specific reference renderer, but the libraries, player, and engine are
portable core objects.

```cpp
lps::PatternLibrary patterns;
lps::VelocityModulationLibrary velocities;

lps::Pattern hitPattern;
hitPattern.length = 4;
hitPattern.hits[0] = true;

lps::VelocityModulation velocity;
velocity.length = 1;
velocity.values[0] = 201;

const auto patternResult = patterns.addOrFind("Hardware Kick", hitPattern);
const auto velocityResult = velocities.addOrFind("Hardware Velocity", velocity);
if (patternResult.entry == nullptr || velocityResult.entry == nullptr)
    throw std::runtime_error("invalid pattern or velocity definition");

lps::PatternPlayer bd1 { patterns, velocities };
bd1.selectPattern(patternResult.entry->id);
bd1.selectVelocityModulation(velocityResult.entry->id);

lps::MidiBufferRenderer midiOut { 1 }; // wrapper adapter, not src/core
lps::SequencerEngine engine;

const auto playerId = engine.registerPlayer(bd1);
if (!playerId)
    throw std::runtime_error("player registration failed");

const auto routeId = engine.connect(
    *playerId,
    midiOut,
    lps::RouteMapping::fixedPitch(36.0f));
if (!routeId || !engine.setMasterPlayer(*playerId))
    throw std::runtime_error("route or master setup failed");

engine.prepare({ 48'000.0, 512 });
```

Because `x---` and `[201]` equal the shipped defaults, both `addOrFind()` calls
resolve to the existing **Basic Kick** and **Steady 100** entries instead of
publishing duplicates. A genuinely new value combination produces a new
in-memory entry. Durable storage is an outer-layer responsibility; the shipped
processor supplies persistence callbacks to `addOrFind()`.

All catalog insertion, player registration, route creation, master selection,
and renderer-specific configuration belongs on a setup/control thread. The
libraries, player, and renderer are borrowed and must outlive `engine`.
`engine.prepare()` reserves routing capacity, prepares every component, and
permanently freezes topology.

The shipped processor performs the same composition at a larger scale:

- it loads both catalogs before constructing players that borrow them;
- it creates ten `PatternPlayer` voices and one `PulsePlayer`;
- BD1 is registered first, receives player ID 0, and becomes master;
- BD1's MIDI route uses `RouteMapping::fixedPitch(36.0f)`;
- all players share one `MidiBufferRenderer` constructed for channel 1;
- the VST3 target declares MIDI output in [`CMakeLists.txt`](../CMakeLists.txt).

## 4. Press Play: how the core receives a clock

`PatternPlayer` neither owns a clock nor polls one. The user presses Play in
REAPER, and REAPER calls the plugin's audio callback. JUCE exposes the playhead;
[`LivePatternSequencerProcessor::processBlock()`](../src/plugin/PluginProcessor.cpp)
copies it into a portable `TimelineBlock`:

```text
TimelineBlock.playing    <- PositionInfo::getIsPlaying()
TimelineBlock.tempoBpm   <- PositionInfo::getBpm()
TimelineBlock.ppqStart   <- PositionInfo::getPpqPosition()
TimelineBlock.sampleRate <- AudioProcessor::getSampleRate()
TimelineBlock.sampleCount<- audio.getNumSamples()

ppqPerSample = tempoBpm / (60 * sampleRate)
ppqEnd       = ppqStart + sampleCount * ppqPerSample
```

For the first example callback:

```text
ppqStart      = 8.0
tempoBpm      = 120
sampleRate    = 48000
sampleCount   = 512
ppqPerSample  = 120 / (60 * 48000) = 1 / 24000
ppqEnd        = 8.0 + 512 / 24000 = 8.021333333...
playing       = true
discontinuity = true
```

The wrapper marks this callback as a discontinuity because its previous
`wasPlaying_` value was false. It also marks a stop or an unexpected PPQ jump,
such as a seek or loop wrap, as a discontinuity. A missing host PPQ forces
`playing` to false because the scheduler cannot safely infer musical position.

The wrapper then binds the host-owned `juce::MidiBuffer`, calls
`engine.run(block)`, and unbinds the pointer. Unbinding does not erase messages
already written to the host buffer.

### Host transport is not MIDI Clock

The "clock" above is REAPER's callback timeline: playing state, tempo, and PPQ.
The plugin does not emit MIDI Clock (`F8`), MIDI Start (`FA`), MIDI Continue
(`FB`), MIDI Stop (`FC`), or Song Position Pointer. A synth needs none of those
messages merely to respond to the note-on in this example.

If a hardware arpeggiator, sequencer, or tempo-synchronized effect also needs
clock, configure REAPER to send clock separately. If external hardware is the
master and REAPER follows it, that synchronization also happens outside this
plugin; the core still receives a `TimelineBlock` from REAPER.

## 5. What `PatternPlayer` does in the first callback

BD1 is the master, so `SequencerEngine::run()` processes it before follower
players. `PatternPlayer::process()` then performs this sequence:

1. Clear its fixed-capacity `SequencerEventBuffer`.
2. Activate a pending velocity selection if necessary, then copy a coherent
   velocity-draft snapshot into `velocityLane_`.
3. Because `transportDiscontinuity` is true, close any prior held trigger, clear
   its pending gate end and last-triggered-step state, and reset the velocity
   cursor.
4. Rebase local step zero to `block.ppqStart`, so
   `playbackOriginPpq_ = 8.0`. Starting at PPQ 8.37 would likewise make PPQ 8.37
   local step zero; the first hit is not forced to a global bar line.
5. Activate any eligible pending pattern/window state.
6. Calculate the normal-speed step length:

   ```text
   stepLengthPpq = baseStepLengthPpq / speedMultiplier
                 = 0.25 / 1
                 = 0.25 PPQ
   ```

7. Evaluate the half-open callback interval `[8.0, 8.021333333...)`. The first
   candidate step is local step 0 at PPQ 8.0.
8. Map that playback step into pattern step 0 and call
   `PatternView::isHit(0)`. Bit 0 is set, so this is a hit.
9. Report PPQ 8.0 as the first cycle boundary. This synchronization boundary
   exists because the loop starts, independently of whether the step is
   audible.
10. Advance the velocity lane at its `candidateTrigger` advance point. Rests do
    not advance it. A one-value lane wraps to the same value on every hit.

The core's velocity normalization is exact:

```text
8-bit editor value       = 201
NormalizedValue.raw      = 201 * 257 = 51657
normalized intensity     = 51657 / 65535
                         = 201 / 255
                         ~= 0.788235294
```

On a fresh instance the player allocates `TriggerId {1}` and appends this
protocol-neutral event:

```text
SequencerEvent
  ppqPosition       = 8.0
  triggerId         = 1
  type              = triggerStart
  normalizedValue   = 201 / 255
  hasMusicalPitch   = false
```

The player intentionally supplies no drum note. Pitch is a route/output concern.
Trigger IDs continue increasing across transport resets so later trigger ends
cannot be mistaken for old starts.

The fixed 50-percent gate ends halfway through the 0.25-PPQ step:

```text
offPpq = 8.0 + 0.25 * 0.5 = 8.125
```

PPQ 8.125 is outside the first callback, so the player stores it as
`pendingTriggerOffPpq_` rather than emitting an out-of-block event.

Relevant implementation: [`PatternPlayer.h`](../src/core/PatternPlayer.h),
[`PatternPlayer.cpp`](../src/core/PatternPlayer.cpp),
[`ModulationLane.h`](../src/core/ModulationLane.h), and
[`ModulationLane.cpp`](../src/core/ModulationLane.cpp).

## 6. What `SequencerEngine` does with the start

The engine receives the player's semantic event and:

1. validates that PPQ 8.0 is finite, nondecreasing relative to earlier events
   from this player, and mappable into this callback;
2. rejects the whole player's block and resets its outputs if event validation
   or the 128-event buffer fails;
3. filters trigger starts if BD1 is muted or simultaneously suppressed;
4. fans the surviving event out to every BD1 route;
5. applies the MIDI route's fixed pitch, producing mapped pitch 36;
6. converts musical PPQ to a callback-relative sample frame;
7. stably sorts the combined routed events, with an end before a start when both
   land at the same frame;
8. batches the result by renderer and calls `renderBlock()` once per renderer.

For the first hit, the frame calculation is:

```text
rawOffset = (eventPpq - blockPpqStart) / ppqPerSample
          = (8.0 - 8.0) / (1 / 24000)
          = 0

frameOffset = round(rawOffset) = 0
```

The resulting MIDI-side boundary object is conceptually:

```text
RoutedEvent
  event             = the triggerStart above
  sourcePlayerId    = 0
  routeId           = BD1's MIDI route ID
  frameOffset       = 0
  mappedPitch       = 36.0 semitones
  hasMappedPitch    = true
```

The same semantic hit is also routed to the plugin's CV renderer. That fan-out
does not cause `PatternPlayer` to schedule the hit twice.

See [`SequencerEngine.cpp`](../src/core/SequencerEngine.cpp) and
[`SequencerTypes.h`](../src/core/SequencerTypes.h).

## 7. What the JUCE MIDI adapter writes

Because this is a transport discontinuity, the engine first calls
`MidiBufferRenderer::resetOutputs()`. With a callback buffer bound, the renderer
clears its active-trigger table and queues channel-1 All Notes Off at frame 0.
It then handles the routed start:

1. round mapped pitch 36.0 and clamp it to the MIDI range 0-through-127;
2. reserve an active-trigger slot keyed by
   `(PlayerId 0, RouteId, TriggerId 1)` and remember note 36;
3. clamp intensity `201 / 255` to 0-through-1;
4. construct `juce::MidiMessage::noteOn(1, 36, 201.0f / 255.0f)`;
5. ask `juce::MidiBuffer::addEvent()` to place it at frame 0.

JUCE maps the normalized intensity to a seven-bit velocity by rounding:

```text
round((201 / 255) * 127) = round(100.10588...) = 100
```

Logically, the MIDI 1.0 output is:

```text
frame 0  B0 7B 00   channel 1, All Notes Off (CC 123), safety reset
frame 0  90 24 64   channel 1, note-on 36, velocity 100
```

VST3 and the host may represent these as event objects internally rather than
literal byte packets. The byte form above is what a MIDI 1.0 hardware path
conceptually transmits.

A velocity-lane value of zero deserves special attention: it maps to MIDI
note-on velocity zero, which receivers conventionally interpret as note-off.
The sequencer still scheduled and advanced through that hit, but the synth may
produce no attack.

See [`MidiBufferRenderer.cpp`](../src/plugin/MidiBufferRenderer.cpp) and its
[`MIDI renderer tests`](../tests/MidiBufferTransportTests.cpp).

## 8. REAPER, the cable, and the audible sound

When the callback returns, the MIDI event remains in the host-owned output
buffer. REAPER's track hardware-output route sends it through the selected OS
MIDI device and USB/DIN connection.

The hardware synth receives channel 1, note 36, velocity 100. If its receive
channel and note map match, it allocates or triggers a voice. How velocity
affects amplitude, filter, sample selection, or another parameter belongs to the
synth patch. The synth then produces an analog or digital audio signal on its
own audio outputs.

The engine's frame offset is sample-accurate inside the host callback. Physical
delivery still adds latency or jitter from the DAW, OS, USB/DIN interface, and
synth. That is beyond the core's scheduling boundary.

## 9. The gate end and next loop

At 120 BPM and 48 kHz:

```text
0.125 PPQ = 3000 samples = 62.5 ms
```

With 512-frame callbacks, sample 3000 falls in zero-based callback 5:

```text
callback 5 begins at global sample 2560
block ppqStart = 8.0 + 2560 / 24000 = 8.106666667
event offset   = (8.125 - 8.106666667) * 24000 = 440
```

In that callback, `PatternPlayer` emits:

```text
triggerEnd(PPQ 8.125, TriggerId 1)
```

The engine maps it to frame 440. `MidiBufferRenderer` uses the full
player/route/trigger key to recover the note used for the start; it does not
trust a newly computed pitch for the end. It frees the active slot and writes:

```text
frame 440  80 24 00   channel 1, note-off 36, release velocity 0
```

For a one-shot drum voice, the synth may let its sample finish regardless of
note-off. A sustained patch normally enters its release stage.

The remaining pattern steps at PPQ 8.25, 8.5, and 8.75 are rests. The velocity
cursor remains on its last hit through all three. The four-step loop is one PPQ,
or 500 ms at 120 BPM, so the next hit occurs at PPQ 9.0:

```text
first hit: PPQ 8.0, global sample 0, callback 0 frame 0
next hit:  PPQ 9.0, global sample 24000, callback 46 frame 448
```

The one-value lane wraps, so that next note-on also has velocity 100.

## 10. Stop, seek, mute, and failure behavior

### Stop or seek while a note is held

`PatternPlayer` closes its logical trigger at the discontinuity position and
rebases if playback continues. The engine resets the MIDI renderer before
rendering events for that block. With a destination bound, this clears trigger
memory and inserts channel All Notes Off at frame 0. A logical end that follows
the reset is unmatched and safely ignored.

On a normal REAPER stop transition, hardware therefore normally receives the
All Notes Off safety message rather than a redundant individual note-off. On a
seek during playback, the reset is followed by the new local step-zero hit if
that step is active.

### Mute or suppression

Mute and simultaneous-hit suppression filter trigger starts in the engine,
after `PatternPlayer` has scheduled them. Pattern phase and velocity phase keep
advancing. The later end reaches the renderer but is unmatched because no start
was rendered, so the renderer ignores it.

### Invalid player output or capacity overflow

Each player has room for 128 semantic events per callback. The engine checks
finite PPQ, monotonic per-player ordering, and block membership. Overflow or any
invalid event fails that player's entire callback, resets the player, and resets
renderers fed by it instead of risking a partial stuck note.

### MIDI renderer failure

A missing bound destination, missing start pitch, exhausted 128-slot active
trigger table, or host-buffer insertion failure makes `renderBlock()` return
false. The engine resets the shared renderer and every player connected to it.

All Notes Off is necessarily best-effort. If the cable or device disappears,
the synth cannot receive cleanup. Use the synth or REAPER panic control, or reset
the hardware, if a physical failure leaves a note sounding. A replacement
adapter that requires guaranteed shutdown cleanup should panic and flush while
its destination is still available; `releaseResources()` may run after the
per-callback JUCE buffer has been unbound.

## 11. Porting this path away from JUCE or VST3

The portable path stops at `IOutputRenderer`. A different plugin wrapper needs
two adapters.

### Host-clock adapter

For every processing callback, construct a `TimelineBlock` containing:

- finite `ppqStart` and `ppqEnd` for a half-open interval;
- positive tempo and sample rate;
- the callback's sample count;
- current playing state;
- an explicit discontinuity flag for start, stop, seek, loop wrap, or another
  non-contiguous timeline transition.

Do not call the core from a GUI timer as its musical clock. The audio/event
processing callback supplies both the time interval and the output deadline.

### MIDI-output adapter

Implement `IOutputRenderer` so it:

- accepts block-relative `RoutedEvent::frameOffset` values;
- maps trigger starts to the target API's note-on representation;
- remembers each emitted note by player, route, and trigger identity;
- maps the matching end to note-off for that remembered note;
- clamps or rejects values according to an explicit policy;
- returns `false` when delivery cannot be guaranteed;
- implements `resetOutputs()` with the target's panic/zero-output operation;
- allocates or reserves everything before real-time processing.

A plugin adapter normally writes to its host's event queue and lets the host own
the physical port, just as the JUCE VST3 does. A standalone application may open
a MIDI device itself; in that case its renderer also owns device selection,
timestamp conversion, output-thread scheduling, hot-plug behavior, and shutdown
flushes. None of those responsibilities should be moved into `PatternPlayer`.

The minimal callback shape is:

```cpp
void process(const HostProcessContext& context)
{
    lps::TimelineBlock block;
    block.ppqStart = context.ppqStart;
    block.ppqEnd = context.ppqEnd;
    block.tempoBpm = context.tempoBpm;
    block.sampleRate = context.sampleRate;
    block.sampleCount = context.frameCount;
    block.playing = context.playing;
    block.transportDiscontinuity = detectDiscontinuity(context);

    midiRenderer.bind(context.midiOutput);
    engine.run(block);
    midiRenderer.unbind();
}
```

Treat this as structural pseudocode: binding names and error reporting belong
to the chosen SDK. Call `engine.prepare()` before processing, continue calling
`run()` for stopped and eventless blocks, and call `engine.reset()` at an
appropriate lifecycle boundary while cleanup can still reach the output.

## Boundary-by-boundary troubleshooting

| Symptom | First boundary to inspect |
| --- | --- |
| UI playhead never advances | REAPER is not playing/processing the FX, or its playhead did not provide PPQ. |
| UI advances but a MIDI monitor after the plugin sees no start | Check BD1 mute, suppression, hit bit, playback window, velocity zero, and engine diagnostics. |
| Track MIDI exists but the synth receives nothing | Enable the REAPER MIDI output, select the track's MIDI Hardware Output, and verify cable/device selection and channel mapping. |
| Synth MIDI indicator flashes but there is no sound | Check note 36 mapping, patch/voice allocation, synth volume, and the separate audio connection. |
| Sound is late | Distinguish core frame placement from DAW buffering, hardware-output, interface, and synth latency. |
| A note sticks after disconnect | Reconnect and send panic/All Notes Off, or reset the synth; cleanup cannot cross a missing physical link. |

## Source and test index for agents

| Concern | Authoritative implementation | Focused tests |
| --- | --- | --- |
| Pattern and velocity data | [`PatternLibrary.cpp`](../src/core/PatternLibrary.cpp), [`VelocityModulationLibrary.cpp`](../src/core/VelocityModulationLibrary.cpp) | [`PatternLibraryTests.cpp`](../tests/PatternLibraryTests.cpp), [`VelocityModulationLibraryTests.cpp`](../tests/VelocityModulationLibraryTests.cpp) |
| Draft edit publication and scheduling | [`PatternPlayer.h`](../src/core/PatternPlayer.h), [`PatternPlayer.cpp`](../src/core/PatternPlayer.cpp) | [`PatternPlayerTests.cpp`](../tests/PatternPlayerTests.cpp) |
| Normalization and lane phase | [`ModulationLane.h`](../src/core/ModulationLane.h), [`ModulationLane.cpp`](../src/core/ModulationLane.cpp) | [`ModulationLaneTests.cpp`](../tests/ModulationLaneTests.cpp) |
| Validation, route mapping, and PPQ/frame conversion | [`SequencerEngine.h`](../src/core/SequencerEngine.h), [`SequencerEngine.cpp`](../src/core/SequencerEngine.cpp) | [`SequencerEngineTests.cpp`](../tests/SequencerEngineTests.cpp) |
| JUCE clock and composition | [`PluginProcessor.h`](../src/plugin/PluginProcessor.h), [`PluginProcessor.cpp`](../src/plugin/PluginProcessor.cpp) | [`PluginProcessorTests.cpp`](../tests/PluginProcessorTests.cpp) |
| JUCE MIDI encoding and panic | [`MidiBufferRenderer.h`](../src/plugin/MidiBufferRenderer.h), [`MidiBufferRenderer.cpp`](../src/plugin/MidiBufferRenderer.cpp) | [`MidiBufferTransportTests.cpp`](../tests/MidiBufferTransportTests.cpp) |
| VST3 MIDI-output declaration | [`CMakeLists.txt`](../CMakeLists.txt) | Build and host smoke test |

When changing this path, preserve the separation between editable musical data,
semantic scheduling, route policy, protocol encoding, host hardware routing, and
the synth's independent audio return.
