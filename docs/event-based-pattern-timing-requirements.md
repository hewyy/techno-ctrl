# Event-Based Pattern Timing and Duration Requirements

## Implementation prompt

Investigate, design, and implement an event-based pattern system supporting high-resolution hit timing and per-hit note durations.

Preserve the existing realtime-safe architecture: there must be no allocation, locking, spinning, or waiting on the audio thread. Maintain the separation between pattern scheduling, Voice trigger lifetime, semantic events, and MIDI/CV rendering.

Before changing code:

1. Inspect the current pattern library, `PatternPlayer`, runtime signals, `Voice`, persistence, processor state, MIDI/CV renderers, editor grids, and relevant tests.
2. Produce a concise implementation plan.
3. Identify compatibility, realtime-capacity, and migration risks.
4. Verify that the proposed representation integrates with pattern switching, offsets, playback windows, modulation, and transport discontinuities.

Do not include pitch bend in this implementation. Pitch bend will be designed as a separate continuous-modulation feature later.

## 1. Replace fixed Boolean steps with timed hits

The current pattern contains at most 32 Boolean steps and uses a base interval of `0.25 PPQ`. Replace or evolve this representation so patterns store actual timed hits.

The conceptual model should be:

```text
Pattern
    cycleLengthTicks
    hitCount
    fixed-capacity array of PatternHit

PatternHit
    startTick
    durationTicks
```

Use this canonical timebase:

```text
960 ticks per quarter note
```

Examples:

```text
1 quarter note = 960 ticks
1/16 note      = 240 ticks
1/32 note      = 120 ticks
1/64 note      = 60 ticks
1/128 note     = 30 ticks
```

A `1/128-note` grid supplies 32 possible hit positions within one quarter-note beat.

### Timing requirements

- Store hit positions and durations as integers.
- Do not store musical timing as floating-point values.
- Convert ticks to PPQ during playback scheduling.
- Keep hits in deterministic chronological order.
- Permit only one hit at a given tick within one pattern.
- Pattern cycle length must be independent of the final hit.
- A hit must begin within the pattern cycle.
- A hit's duration may extend beyond the end of the pattern cycle.
- Reject zero-duration hits.
- Use fixed-capacity realtime-compatible storage.
- Do not dynamically allocate during playback.
- Pattern comparison and deduplication must consider cycle length, hit positions, and hit durations.

Use initial limits of:

```text
Maximum cycle length: 64 quarter notes
Maximum hit count:    256 hits per pattern
```

Define these as named constants so they can be changed after profiling.

## 2. Store base duration on every hit

Each pattern hit must carry its intended base duration.

The representation must support:

- Extremely short stabs.
- Fractional-step durations.
- A note lasting one complete step.
- Notes spanning several steps.
- Notes spanning several quarter-note beats.
- Durations extending beyond the pattern cycle boundary.

Examples:

```text
startTick = 0,   durationTicks = 30
    Very short stab

startTick = 240, durationTicks = 240
    One current-resolution step

startTick = 960, durationTicks = 2880
    Three-quarter-note-beat hold
```

`PatternPlayer` must put the hit's base duration on its logical pattern-hit signal. It must not produce MIDI or CV note-off events itself.

`Voice` must continue to:

- Resolve trigger parameters.
- Create trigger identities.
- Own active trigger lifetimes.
- Schedule matching semantic trigger-end events.

The minimum positive rendered duration must remain at least one audio sample.

## 3. Retain Gate as articulation modulation

Keep the existing Gate modulation lane.

The per-hit duration stored in the pattern is the base musical duration. Gate is a normalized articulation multiplier:

```text
effective duration = pattern duration × gate
```

Gate remains in the range `0.0–1.0`:

```text
gate 1.0 = complete stored duration
gate 0.5 = half the stored duration
gate 0.1 = short articulation
gate 0.0 = no audible trigger
```

Do not clamp the final duration to the distance between pattern hits or to one sequencer step.

The existing minimum-one-sample behavior applies after multiplication.

Where appropriate, update user-facing terminology to make the relationship clear—for example, "Gate / Articulation"—without changing its underlying modulation semantics.

## 4. Retrigger behavior

A later hit from the same `PatternPlayer` must end and retrigger its currently active note, even if the previous hit's duration has not expired.

Therefore:

- Long notes may continue across rests.
- Long notes may continue across the pattern cycle boundary.
- A later hit from the same source ends the previous trigger at the new hit position.
- The new hit then produces a new trigger start.
- Hits from different PatternPlayers retain independent trigger lifetimes.

Do not implement ties or legato in this project. They will be treated as a separate future feature because they require explicit envelope, pitch-change, MIDI, and CV semantics.

## 5. Event-based pattern playback

Replace fixed-step scanning with scheduling of timed pattern hits over each half-open processing interval:

```text
[block.ppqStart, block.ppqEnd)
```

Playback must:

- Emit every hit occurring inside the block.
- Correctly emit multiple hits inside one audio block.
- Avoid duplicate events at block boundaries.
- Correctly wrap at the pattern cycle boundary.
- Preserve deterministic ordering for simultaneous events.
- Preserve transport start, stop, reset, and discontinuity behavior.
- Preserve continuous and one-shot play modes.
- Preserve pattern selection and transition quantization.
- Preserve cycle-boundary signals.
- Preserve command and modulation propagation order.
- Continue applying same-timestamp modulation before a hit is sampled by a Voice.

Playback speed must stretch or compress the complete pattern timeline. It must not change stored hit positions or durations.

For example:

```text
0.5× playback → positions and durations take twice as much PPQ time
1.0× playback → normal timing
2.0× playback → positions and durations take half as much PPQ time
```

Review fixed signal, semantic-event, routed-event, and renderer capacities. Dense patterns and multiple players must fail safely and diagnostically if a bounded capacity is exceeded.

## 6. Offset and playback-window semantics

Convert the existing step-based offset and playback-window concepts to musical-time operations.

### Pattern offset

- Store or apply pattern offset in ticks.
- Offset should rotate hit positions within the relevant pattern cycle.
- Wrapping must be deterministic.
- Offset must not alter stored source-pattern events.

### Playback window

- Express playback start and end in ticks.
- The playback window must define an explicit cycle range.
- Hits outside the active window must not trigger.
- Define and test how a hit whose duration crosses the playback-window end behaves.
- Prefer allowing an already-started hit to retain its duration unless it is retriggered, stopped, or reset.
- Pattern transitions synchronized to cycle boundaries must continue to operate correctly.

UI controls may display musical divisions, but the engine must not depend on step indexes or a 32-bit hit mask.

## 7. Pattern editor

Replace the permanently fixed 32-cell assumption with a musical timeline editor.

The editor must support:

- Adding hits.
- Removing hits.
- Selecting hits.
- Repositioning hits.
- Editing each hit's duration.
- Editing pattern cycle length independently of its hits.
- Displaying the current playback position.
- Displaying major beats and smaller subdivisions.
- Editing closely spaced hits through zooming or an equivalent usable interaction.

### Hit visualization

Represent hits as horizontal note bars:

```text
|████                           short stab
        |████████               medium duration
                    |██████████ long duration
```

Recommended interaction:

- Click empty timeline space to add a hit.
- Click a hit to select it.
- Drag the body of a hit to change its start position.
- Drag the hit's right edge to change its duration.
- Use delete/backspace or an equivalent command to remove the selected hit.
- Apply the current snap setting when adding, moving, or resizing.
- Provide fine-nudge operations for one-tick adjustments.

Also provide a numeric duration control for exact editing. It should display useful musical values where possible, while retaining exact tick precision.

Examples:

```text
30 ticks
1/16 note
1 beat
3 beats
```

## 8. Snap and microtiming

Pattern storage resolution and editor snap must be independent.

Changing snap must never move or alter existing hits.

Provide at least these snap options:

```text
1/16
1/32
1/64
1/128
Triplet divisions
Off / one-tick editing
```

When snap is disabled:

- Positions remain integer ticks.
- Dragging may resolve to individual ticks.
- Fine-nudge commands move events by one tick.
- Do not introduce arbitrary floating-point event positions.

The editor should default to a musically useful snap rather than one-tick editing.

## 9. Meter and musical display

Use ticks per quarter note internally regardless of time signature.

Do not store positions using ambiguous "beat" units because the musical meaning of a beat varies between meters.

The UI must:

- Obtain and display the host time signature where available.
- Draw bar boundaries according to the host meter.
- Draw note-value subdivisions consistently.
- Use unambiguous snap labels such as `1/16`, `1/32`, and `1/128`.
- Optionally show dotted-quarter groupings in compound meters such as 6/8.
- Continue operating sensibly if the host does not provide a valid time signature.

Pattern timing itself must remain stable if the displayed meter changes.

## 10. Persistence and migration

Introduce a versioned pattern-catalog representation for event-based patterns.

Migrate legacy patterns as follows:

```text
old step N:
    startTick = N × 240

old pattern length:
    cycleLengthTicks = old length × 240

legacy hit base duration:
    durationTicks = 240
```

Because existing Gate modulation remains active, the existing default half-gate preserves the current effective duration:

```text
240 base ticks × 0.5 gate = 120 effective ticks
```

Migration requirements:

- Existing pattern catalogs must load safely.
- Existing processor state must load safely.
- Legacy hit positions must retain their current musical timing.
- Legacy playback windows and offsets must be translated consistently.
- New catalogs must preserve exact tick positions and durations.
- Save/load round trips must not quantize event timing.
- Pattern equality must ignore unused fixed-capacity storage.
- Reject malformed, duplicate, zero-duration, unsorted, out-of-range, and over-capacity event data.
- Do not silently discard valid legacy patterns.
- Document whether the new catalog format is readable by older application versions.

## 11. Realtime and concurrency requirements

Maintain the project's existing realtime guarantees.

Specifically:

- No allocation on the audio thread.
- No locks on the audio thread.
- No waiting or spinning on the audio thread.
- Published library records remain immutable.
- Player drafts remain safe for concurrent UI editing and audio-thread reading.
- Audio-thread snapshots must be coherent.
- Fixed-capacity overflows must be detectable.
- Overflow must cause safe output reset behavior where required.
- Do not introduce unbounded event iteration.
- Avoid rebuilding or sorting pattern data on the audio thread.

If editable event arrays require a new snapshot/publication mechanism, document and test that mechanism explicitly.

## 12. Tests

Add focused tests covering at least the following.

### Pattern data

- Valid event-based pattern creation.
- Validation of cycle length and hit count.
- Rejection of duplicate hits at one tick.
- Rejection of zero-duration hits.
- Deterministic event ordering.
- Equality and deduplication.
- Maximum cycle and hit capacity behavior.

### Migration

- Legacy Boolean-pattern migration.
- Preservation of legacy hit timing.
- Preservation of legacy cycle length.
- Translation of offsets and playback windows.
- Catalog and processor-state round trips.

### Timing

- Hits on `1/16`, `1/32`, `1/64`, and `1/128` positions.
- One-tick microtiming.
- Several hits within one former `0.25 PPQ` interval.
- Hits exactly at block boundaries.
- Hits immediately before and after cycle boundaries.
- Multiple cycles processed in a sufficiently large block.
- Playback at every supported speed.
- Transport discontinuities and resets.

### Duration and Gate

- A short stab.
- A one-step base duration.
- A three-beat base duration.
- Gate values `0.0`, `0.1`, `0.5`, and `1.0`.
- Minimum positive one-sample duration.
- Duration extending past the cycle boundary.
- Duration extending past the playback-window boundary.
- Retrigger before the previous duration expires.
- Independent overlapping triggers from different PatternPlayers.

### Output

- Matched MIDI note-on and note-off events.
- Correct MIDI note-off timing.
- Matched CV gate start and end.
- Reset behavior on stop and discontinuity.
- Safe behavior under buffer overflow.
- Deterministic same-timestamp ordering.

### UI/model

- Adding and removing hits.
- Moving and resizing hits.
- Snap changes that do not alter existing data.
- One-tick nudging.
- Cycle-length editing.
- Zoomed editing of closely spaced hits.
- Meter-aware bar display.

## 13. Documentation

Update the relevant architecture and format documentation to explain:

- The event-based pattern model.
- The 960-tick quarter-note timebase.
- Pattern cycle length.
- Hit position and duration.
- Gate as an articulation multiplier.
- Retrigger behavior.
- Playback-speed scaling.
- Tick-based offset and playback windows.
- Editor snapping versus storage resolution.
- Legacy migration.
- Realtime limits and overflow behavior.
- The fact that ties, legato, and pitch bend are deferred features.

## Explicitly deferred features

Do not include these in this implementation:

- Pitch bend.
- MPE or per-note MIDI expression.
- Ties.
- Legato transitions.
- Multiple hits at the same tick in one pattern.
- Arbitrary floating-point event positions.
- Unrelated modulation or renderer refactors.

Complete the pattern timing and duration redesign, verify it with focused tests, and keep unrelated behavior unchanged.
