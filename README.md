# Live Pattern Sequencer — one-day MVP

For the native macOS build and installation workflow, start with `README-MACOS.md` and `build-macos.sh`.

This project is a deliberately narrow end-to-end proof:

- REAPER supplies transport position and tempo.
- Independent C++ pattern players drive a configurable number of drum voices.
- Ten built-in patterns cover basic drum figures and odd-length 3/5/7/9-step pulses.
- Every drum voice emits its own fixed MIDI note on MIDI channel 1, with velocity driven by an independently selected modulation.
- The UI edits a 32-step working copy for each player, marks changes to its draft, loop range, or offset, and can save the active loop into a persistent per-user library. A library pattern's stored length sets the player's initial playback end without limiting the editable grid.
- Each player also has an editable 1–10 step velocity modulation shown as rotary knobs. It advances only when the pattern produces a hit, loops independently of the hit pattern, and has its own amber playhead. Modulations can be selected and saved without changing any hit pattern.
- Each voice has a MUTE control that silences its note hits while its sequence and visible playhead continue advancing.
- One statically configured voice is the master (BD1 by default). Each other voice can queue a one-shot Reset that restarts its active loop at the next start of the master's active loop.
- The plugin is placed before a software synth on the same REAPER track.

## Default drum voices

The bundled configuration creates ten regular `PatternPlayer` instances. They all use MIDI
channel 1 and are distinguished by these fixed drum-machine note values:

| Voice | Note |
| --- | ---: |
| BD1 | 36 |
| BD2 | 37 |
| Machine | 38 |
| Snare | 39 |
| Clap | 40 |
| Rimshot | 41 |
| OH | 42 |
| CH | 43 |
| Crash | 44 |
| Ride | 45 |

The voice list is centralized in `PluginProcessor.cpp`. The processor, editor, sequencer
engine, routing, and suppression matrix use runtime-sized collections, so the list can be
expanded or reduced without changing those systems.

## Architecture boundary

`src/core` contains no JUCE, VST3, REAPER, or operating-system types. `IPlayer` and
`ITransport` are the boundaries between the sequencer core and the plugin wrapper.
`PatternLibrary` and `VelocityModulationLibrary` own separate immutable, append-only
catalog entries; each `PatternPlayer`
copies the selected hits into its own unsaved 32-step draft before playback or editing. Saving
crops and rebases the selected loop, including any pattern offset. Exact content duplicates
select the existing entry without asking for a name. New content asks for a name and is added
to the shared per-user JSON catalog. Edits outside the brackets are marked as draft
changes but are excluded from the saved pattern. The saved or matching library entry follows
the same timing as other pattern selections: while playing, follower voices wait for the next
start of the master voice's loop, and the master waits for its own loop start. While stopped,
selections apply immediately. Activation reloads canonical content and discards excluded edits.
User entries are restored from the catalog whenever a new plugin instance starts. Catalog
persistence is independent of the DAW's plugin-state persistence, which is not implemented
in this MVP.

Velocity modulation uses an 8-bit 0–255 editing scale, which is normalized to MIDI's 0–127
velocity range at output. For example, `[255, 100, 225, 150]` produces MIDI velocities
approximately `[127, 50, 112, 75]`. Rests do not consume modulation steps; muting or
suppression does not pause the modulation because the underlying player continues running.

## Pattern catalog

On first startup, the plugin creates `patterns.json` containing the ten default patterns. On
later startups, the complete library is read from that file before any players are created.
Saving a new pattern validates and atomically replaces the catalog before publishing the new
entry to the running sequencer. Multiple plugin instances merge their latest on-disk entries
under both in-process and inter-process locks. Already-running instances see their own saves
immediately and pick up other instances' new entries the next time they are opened.

The per-user catalog location is:

- macOS: `~/Library/Hew/LivePatternSequencer/patterns.json`
- Windows: `%APPDATA%\Hew\LivePatternSequencer\patterns.json`
- Linux: `${XDG_CONFIG_HOME:-~/.config}/Hew/LivePatternSequencer/patterns.json`

Velocity modulations use a separate `velocity-modulations.json` file in the same directory.
It follows the same validation, locking, merging, and atomic-write rules as `patterns.json`.
Each entry stores a contiguous `values` array containing 1–10 integers from 0 through 255:

```json
{
  "format": "live-pattern-sequencer-velocity-modulation-library",
  "schemaVersion": 1,
  "modulations": [
    { "id": 2, "name": "Four-Step", "values": [255, 100, 225, 150] }
  ]
}
```

The versioned JSON is human-readable. Each entry has a stable numeric `id`, a `name`, and a
`steps` string in which `x` is a hit and `-` is a rest:

```json
{
  "format": "live-pattern-sequencer-pattern-library",
  "schemaVersion": 1,
  "patterns": [
    { "id": 1, "name": "Basic Kick", "steps": "x---" }
  ]
}
```

Edit the catalog only while the plugin is not running. It accepts 1–256 unique patterns,
each 1–32 steps long. If an existing catalog is malformed or uses an unsupported schema, the
plugin leaves it untouched, starts with its default library, and refuses new disk-backed saves
until the file is corrected or moved aside and the plugin is reopened. The editor displays the
catalog path and recovery action when this happens.

```text
REAPER clock
    -> PluginProcessor
    -> SequencerEngine
    -> PatternPlayer(s)
    -> SequencerEventBuffer
    -> shared MidiBufferTransport
    -> JUCE MidiBuffer (one channel, distinct notes)
    -> software synth
```

Every fixed-note pattern source is still a normal `IPlayer`, so it participates in the same
routing lifecycle and suppression matrix as any other player implementation. Master reset
alignment follows the master's playback range and speed rather than its MIDI hits, so a silent
master loop start is still a valid synchronization point. The target keeps playing until that
boundary and then restarts sample-accurately from the beginning of its own active range.

## Windows prerequisites

Install:

1. Visual Studio 2022 with **Desktop development with C++**.
2. CMake 3.22 or newer.
3. Git.
4. REAPER.

The first CMake configuration downloads JUCE 9.0.1 from its official repository. Review JUCE's current licensing terms before distributing a binary. This does not matter for privately testing the MVP, but it matters before release.

## Build

Open PowerShell in this directory and run:

```powershell
.\build-windows.ps1
```

Or run the commands manually:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The plugin will be produced at approximately:

```text
build\LivePatternSequencer_artefacts\Release\VST3\Live Pattern Sequencer MVP.vst3
```

Copy the entire `.vst3` bundle to:

```text
C:\Program Files\Common Files\VST3\
```

If Windows requires administrator permission, run the copy from an elevated PowerShell or Explorer window.

## REAPER setup

1. Start REAPER.
2. Open **Options → Preferences → Plug-ins → VST**.
3. Confirm `C:\Program Files\Common Files\VST3` is scanned.
4. Run **Re-scan**.
5. Create one track.
6. Open the track's FX chain.
7. Add **Live Pattern Sequencer MVP** first.
8. Add a software synth immediately after it.
9. Configure the drum machine or sampler to receive MIDI channel 1 and map the notes in the table above.
10. Press Play in REAPER.

The sequencer UI should advance with REAPER and the drum instrument should receive the patterns.

## Fast troubleshooting

### The UI advances but there is no sound

- Confirm the sequencer is before the drum instrument in the FX chain.
- Confirm the drum instrument accepts MIDI channel 1.
- Confirm the drum voice notes in the table above match the receiving instrument's note map.

### REAPER cannot find the plugin

- Confirm the complete `.vst3` bundle was copied, not just a file inside it.
- Confirm the VST3 directory is in REAPER's scan path.
- Clear the VST cache and re-scan from REAPER's VST preferences.

### CMake cannot download JUCE

Clone JUCE separately and configure with an installed JUCE CMake package, or temporarily retry on a network without GitHub restrictions. The project supports `-DLPS_FETCH_JUCE=OFF` when JUCE is already discoverable by CMake.

## Next core-development step

Move the default voice map and shared MIDI channel into persisted plugin state so they can be
edited without recompiling. The existing fixed-note player and dynamic engine routing can stay
in place.
