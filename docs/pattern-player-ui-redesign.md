# Pattern Player UI Redesign Requirements

## 1. Purpose and scope

This document defines the first-stage redesign of pattern-player editing in the
JUCE plug-in UI. It is written against the current event-based sequencer and
supersedes the older fixed-step assumptions in the original redesign note.

The work covers:

- making **All Voices** an overview and navigation page;
- adding a dedicated, in-plug-in **Pattern Editor** for one `PatternPlayer`;
- exposing that player's rhythm plus its pitch, velocity, and gate modulation
  lanes in one workflow; and
- preserving the current live-draft, catalog, scheduling, and realtime-safety
  behavior.

The work does not redesign Synth 2 or Sample device-parameter modulation,
runtime bindings, the mute/suppression scheduler, groups, MIDI/CV routing, or
voice profiles. The existing **SYNTH 2** and **SAMPLE** pages must continue to
work.

“Full-screen” means the Pattern Editor replaces the normal content area inside
`LivePatternSequencerEditor`; it does not mean an operating-system fullscreen
window.

## 2. Current codebase model

The implementation must retain these existing boundaries.

### 2.1 Pattern-player and voice identity

The UI is indexed primarily by pattern player, not by unique `Voice`. Most
players map one-to-one to voices, but the three Synth 2 pattern players share a
single Synth 2 voice. The selected editing target must therefore be stored as a
`playerIndex`. Use `playerNameForUi()` for the editor heading and
`playerVoiceIndexForUi()` plus `voiceNameForUi()` when true voice metadata is
needed.

### 2.2 Rhythm data

`lps::Pattern` is an event-based rhythm containing:

- a cycle length at 960 ticks per quarter note;
- up to 256 chronologically ordered `PatternHit` records; and
- a start tick and duration tick count for every hit.

It is not a fixed Boolean step array. The maximum cycle is 64 quarter notes.
The current editor uses a fixed 16-quarter-note ruler and offers 1/16, 1/32,
1/64, 1/128, triplet, and one-tick snap values. The redesign must continue to
draw timed note bars and must not describe the pattern as having a fixed number
of stored steps.

### 2.3 Player state that is not library pattern data

`PatternPlayer` also owns a rotation offset, a half-open playback window
`[playbackStartTick, playbackEndTick)`, and playback speed. These values affect
the active player and are serialized in plug-in state, but
`PatternPlayer::patternForSave()` does not include them as player settings in a
pattern-library entry. The playback endpoint does define the saved pattern's
cycle length, however, and hits at or beyond that half-open endpoint are not
saved.

Consequently:

- rotating a pattern must not rewrite its source hit positions;
- editing the playback window must not mark the library pattern as modified;
- saving a pattern persists cycle length, hit positions, and hit durations;
  and
- offset, window, and speed continue to be restored through host/project state.

### 2.4 Pitch, velocity, and gate data

Pitch, velocity, and gate are independent `ModulationPlayer` instances. Their
data is stored as `lps::Modulation` entries in `ModulationLibrary`; it is not
embedded in `lps::Pattern` or `PatternLibraryEntry`.

The Pattern Editor unifies these controls visually, but it must preserve their
independent selection, draft, unsaved-state, and save behavior. Selecting a
rhythm pattern does not implicitly replace any modulation lane. Gate remains an
articulation multiplier over each hit's stored duration.

Pitch editing must continue to use the existing `PitchEditContext`, including
chromatic and scale-aware editing, root pitch class, scale selection, note-name
display, and “adjust existing values to scale” behavior.

### 2.5 Live drafts, selection, and persistence

Edits are made to the player's live draft and can affect playback without
altering immutable library records. Pattern selection is currently queued
through `RuntimeGraph::schedulePatternSelection()` and normally activates at a
future BD1/master bar boundary. The redesign must not silently change that
transition policy.

Pattern and modulation libraries are append-only and content-deduplicated.
Saving content that already exists selects the existing record instead of
creating a renamed duplicate. Plug-in state separately captures active drafts,
so leaving a page does not discard edits.

## 3. Navigation and editor state

Add a Pattern Editor page/state to `LivePatternSequencerEditor`. The editor must
track one target `playerIndex` and a return page. Opening it from All Voices sets
the return page to All Voices. A visible Back control returns to that page.

Opening and closing the Pattern Editor must not:

- stop transport;
- save, discard, or reset any draft;
- change pattern or modulation selection;
- retarget the editor except through an explicit player-navigation action; or
- rebuild or republish runtime graph topology.

If an entry point is invoked for a player that no longer supports pattern
editing, the editor must remain on the current page and perform no mutation.

The top-level transport and suppression controls may remain visible if layout
allows, but pattern-player editing takes the rest of the plug-in content area.

## 4. All Voices page

All Voices remains the overview for every configured pattern player. This is
currently 24 pattern players: ten drum players, Synth 1, three players sharing
Synth 2, and ten sample-part players.

### 4.1 Read-only rhythm overview

Each row must continue to show:

- the player display name;
- the rhythm on the shared 16-quarter-note timeline;
- current playback position and playback-window visualization;
- the compact velocity/pitch/gate preview; and
- existing mute, reset-to-master, group, scheduling, and selection state that
  is outside this redesign.

On All Voices, the rhythm timeline and playback-window handles are read-only.
Clicking a hit, empty timeline space, or a range handle must not add, remove, or
modify anything.

### 4.2 Removed and repurposed controls

Remove the per-player left/right rotation buttons from All Voices. Rotation
remains available in the Pattern Editor.

The existing pattern button and the displayed rhythm both open the Pattern
Editor for that `playerIndex`. They no longer open different editing workflows.
The compact modulation preview may also open the same Pattern Editor with the
last-used lane focused; it must not open the current floating modulation editor
from All Voices.

The pattern button must retain a visible modified marker when the rhythm draft
differs from its selected library record. If any of the three modulation lanes
is modified, the row must also expose a modulation-modified state without
mislabeling it as a rhythm modification.

## 5. Dedicated Pattern Editor

The page edits exactly one pattern player. Its header must show:

- Back;
- `playerNameForUi(playerIndex)`;
- the selected rhythm name;
- pending pattern-selection state, when applicable;
- separate unsaved indicators for Rhythm, Pitch, Velocity, and Gate; and
- Save actions described in section 7.

### 5.1 Pattern browser

The page must contain an in-page browser for `PatternLibrary` entries rather
than relying on the existing popup-only selector. Each entry shows its name,
rendered rhythm preview, selected/pending state, and origin metadata when
available.

Choosing an entry uses the existing scheduled-selection path and clearly shows
that the change is queued until its activation boundary. It must not be
presented as immediate if `waitForCycleBeforeSelection` remains enabled.

The browser must refresh after a successful save because append-only saves can
add a new entry. A save that deduplicates to existing content must select/show
the resolved existing entry.

### 5.2 Rhythm timeline

Extract or reuse the event timeline behavior currently implemented by
`SynthPageComponent::PatternRow::PatternGrid`. The Pattern Editor timeline must
support:

- add a hit by clicking empty space;
- select a hit;
- drag a selected hit to change its start tick;
- drag its right edge to change duration;
- delete/backspace to remove it;
- left/right-arrow one-tick nudge;
- an exact duration field with musical presets;
- independent snap selection;
- playback start/end handle editing;
- cycle-length editing when extending the end handle;
- a playback-position marker; and
- rotation left/right by the selected snap amount.

Changing snap must never quantize existing data. Hit edits call the existing
processor methods (`addPlayerHit`, `removePlayerHit`, `movePlayerHit`,
`resizePlayerHit`, and `setPlayerCycleLength`). Window and rotation controls use
the existing player-state APIs.

For this phase the timeline may retain the current 16-quarter-note ruler.
Patterns longer than the visible ruler must never be truncated or rewritten;
the UI must either provide horizontal navigation/zoom or clearly indicate that
additional content is off-screen.

### 5.3 Create New

Provide a Create New action that installs a new unsaved rhythm draft in the
current player. This requires a processor-level operation; it must not mutate a
published library entry.

The default proposal is an empty four-quarter-note cycle, playback window
`[0, 3840)`, zero offset, and 1x speed. Pitch, velocity, and gate selections are
left unchanged. The new draft is not added to the library until Save Rhythm is
used.

### 5.4 Modulation lanes

Show Pitch, Velocity, and Gate / Articulation on the same page. Reuse the
existing `ModulationCell` editing and processor APIs rather than adding values
to `Pattern`.

Each lane must provide:

- its own library selector/browser and Save action;
- its own modified indicator;
- length decrement/increment within `Modulation::maxLength` (32);
- value editing and current-step highlighting; and
- locked-state behavior from `playerModulationLockedForUi()`.

Pitch additionally provides the existing chromatic/scale-aware controls.

The UI must not visually imply that modulation index N corresponds to rhythm
hit N. Modulation can advance on hits and can have a different length from the
number of hits.

## 6. Originating-voice metadata

Extend `PatternLibraryEntry` with an optional display string such as
`originVoiceName`. This is catalog metadata, not a runtime binding.

When a new rhythm entry is saved:

- resolve the true voice name through `playerVoiceIndexForUi()` and
  `voiceNameForUi()`;
- store a snapshot of that name (for example, all three Synth 2 players store
  `Synth 2`, not `Synth 2 / Pattern 1`);
- display it as secondary text in the pattern browser; and
- never use it to restrict selection or routing.

Built-in and older entries may have no origin. Display `Built-in` for built-in
records and omit the field or show `Unknown` for migrated records.

Because library insertion deduplicates on rhythm content, the first saved
record owns the origin metadata. Saving equivalent content from another voice
must not overwrite that origin.

This change requires a backward-compatible pattern-catalog schema update in
`PatternLibraryFileStore`. Existing schema-2 catalogs must remain readable;
new metadata should be optional while reading.

## 7. Save and unsaved-state behavior

Unsaved state is asset-specific:

- **Rhythm modified** uses `playerPatternModifiedForUi()` and compares the live
  `Pattern` draft with the selected `PatternLibrary` record.
- **Lane modified** uses `playerModulationModifiedForUi()` for Pitch, Velocity,
  and Gate independently.
- rotation, playback window, playback speed, mute, and other player settings
  are project/session state and do not enable Save Rhythm.

Leaving the page must preserve all drafts. Project save/restore continues to
use processor state serialization. No page-navigation action automatically
writes either catalog.

Provide these explicit actions:

- **Save Rhythm** calls the existing pattern save path, extended with origin
  metadata for a new entry.
- **Save Pitch**, **Save Velocity**, and **Save Gate** use the existing
  modulation save paths.

A combined **Save All Modified** action is optional convenience. If included,
it performs the four existing independent saves and reports partial failures;
it does not create a bundled preset or an association between the saved IDs.

There is no destructive Discard action in this phase.

## 8. Implementation map

The expected implementation seams are:

- `src/plugin/PluginEditor.h/.cpp`
  - add the Pattern Editor page/component and selected `playerIndex`;
  - route All Voices click targets to it;
  - make All Voices timeline handlers non-mutating;
  - remove All Voices offset buttons; and
  - extract/reuse the focused-page timeline and modulation controls.
- `src/plugin/PluginProcessor.h/.cpp`
  - expose any missing editor-safe operation for installing a new draft;
  - pass true voice-origin metadata through pattern save; and
  - preserve the existing scheduled selection and per-lane save APIs.
- `src/core/PatternLibrary.h/.cpp`
  - add optional origin metadata to `PatternLibraryEntry` without changing
    rhythm equality or deduplication.
- `src/plugin/PatternLibraryFileStore.h/.cpp`
  - read schema 2 and the new schema;
  - write optional origin metadata; and
  - retain atomic/temp-file replacement and catalog locking behavior.
- `tests/PluginProcessorTests.cpp` and `tests/PatternLibraryTests.cpp`
  - cover origin capture, round-trip, old-catalog compatibility, deduplication,
    and the new-draft operation.

UI behavior that can be factored out of nested private classes should be moved
into reusable components before wiring the new page. Do not duplicate the
timeline edit logic in a second implementation.

## 9. Acceptance criteria

1. All 24 pattern players remain visible and identifiable on All Voices.
2. All Voices still renders rhythms, playback state, and modulation previews.
3. All Voices cannot add, remove, move, resize, rotate, or change the playback
   window of a rhythm.
4. Per-player rotation buttons are absent from All Voices.
5. Clicking a row's rhythm or pattern button opens the same Pattern Editor for
   that player.
6. Back returns to All Voices without saving or discarding anything.
7. The Pattern Editor browser lists current pattern-library records with
   previews, selected/pending state, and available origin metadata.
8. Pattern selection retains current boundary-quantized scheduling behavior.
9. The editor supports event-based hit add, select, move, resize, delete,
   one-tick nudge, exact duration, snap, cycle, playback-window, and rotation
   editing.
10. The editor exposes Pitch, Velocity, and Gate as independent modulation
    lanes and preserves scale-aware pitch editing.
11. Rhythm and each modulation lane show independent modified state.
12. Edits affect live drafts and page navigation never auto-saves them.
13. Save Rhythm persists only `Pattern` data plus catalog metadata; lane Save
    actions persist their corresponding `Modulation` data.
14. Rotation, window, and speed remain project state and do not mark the rhythm
    library entry modified.
15. Create New creates a valid unsaved player draft without publishing a
    library record.
16. A newly saved rhythm records its originating voice name; that name is
    informational and never restricts reuse.
17. Saving equivalent rhythm content preserves append-only deduplication and
    the first record's origin.
18. Existing schema-2 pattern catalogs and existing plug-in state still load.
19. Existing Synth 2 and Sample pages continue to work.
20. No UI change introduces allocation, locking, waiting, or graph rebuilding
    on the audio thread.

## 10. Questions and suspected answers

These questions affect product semantics. The suspected answers are concrete
defaults so implementation can proceed unless they are corrected.

1. **Should a saved “pattern” bundle rhythm with Pitch, Velocity, and Gate?**
   Suspected answer: **No for this phase.** Keep the existing independent
   Pattern and Modulation libraries. If selecting one item must later restore
   all four assets, introduce a separate preset/bundle type rather than putting
   modulation into `lps::Pattern`.

2. **Should playback window, rotation, and speed be stored in the pattern
   catalog?** Suspected answer: **No.** They remain per-player project state,
   consistent with `PatternPlayerPersistentState` and the current Save path.

3. **What should Create New initialize?** Suspected answer: **an empty
   four-quarter-note rhythm with a full playback window, zero offset, and 1x
   speed**, while retaining current modulation selections.

4. **Should selecting a library rhythm activate immediately inside the editor?**
   Suspected answer: **No.** Preserve the existing next-boundary scheduling and
   display a clear queued/pending state.

5. **Should origin mean voice or pattern-player label?** Suspected answer:
   **true voice name.** Store `Synth 2` for all three shared-voice players and
   use the more specific player label only in the editor heading.

6. **Should the Pattern Editor replace editing on the focused Synth 2 and
   Sample pages immediately?** Suspected answer: **No.** Keep those pages in
   this phase, but share extracted timeline/modulation components so their
   behavior does not diverge.

7. **Must the first release edit the full 64-quarter-note maximum visually?**
   Suspected answer: **Yes, without data loss, through horizontal navigation or
   zoom.** Retaining the 16-quarter-note default view is acceptable; silently
   hiding inaccessible events is not.

8. **Does this phase require user-entered pattern names?** Suspected answer:
   **No.** Preserve the current stable-ID-derived display name for unnamed
   saves. Pattern renaming can be added later without coupling it to the editor
   navigation work.
