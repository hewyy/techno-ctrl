# Live Pattern Sequencer MVP — macOS

This package builds the sequencer as a native macOS VST3 for REAPER. It works on both Apple Silicon and Intel Macs by building for the architecture of the Mac running the script.

The result is not a precompiled application. It is a small C++/JUCE project with a one-command script that builds, tests, locally signs, and installs the VST3.

## What the MVP does

```text
Pattern players: 10 independent drum voices by default
Pulse players:    1 periodic source
Patterns:         32-step player drafts; library length sets the initial playback end
Gate:             50 percent of the step
MIDI output:      shared channel 1, fixed note per voice, modulated velocity
CV output:        33 discrete channels; gate/pitch/control per player
Clock:            REAPER transport and tempo
```

The built-in library contains ten patterns: Basic Kick, All Steps, Backbeat, Offbeat Hats,
Tresillo, 3/5/7/9-Step Pulses, and Euclidean 5/12. Their lengths range from 3 to 16 steps, so
layering the odd-length pulses produces evolving phase relationships. Every player always shows
all 32 editable steps; loading a pattern sets the playback end to its stored length, and moving
the playback range later does not change the pattern hits. Clicking a step edits only that
player's working copy; it does not overwrite the library entry. The plugin window colors the
current playback position amber. Changes to the draft, loop range, or offset turn the selector
amber and enable a `Save *` button. Saving crops the draft to the bracketed loop; duplicate
content selects the existing library entry, while new content prompts for a name. The saved
selection activates at the end of the current playback loop. Steps outside the brackets are
not included and are discarded when that canonical saved selection activates. User-created
entries are saved to `~/Library/Hew/LivePatternSequencer/patterns.json` and are loaded by new
plugin instances at startup. Each voice has a MUTE control that silences its note hits without
stopping its sequence or playhead. The editor also provides one suppression matrix across every player. See `README.md`
for the default voice-to-note map. BD1 is the statically configured master voice in this draft.
Every other row has a one-shot Reset control: the voice keeps playing until the next start of
the master's active loop, then restarts from the beginning of its own active playback range.

Each voice also has a separate velocity-modulation lane with 1–10 rotary steps. The modulation
advances on hits rather than timeline steps, loops independently of the selected hit pattern,
and displays its own amber playhead. Values use a 0–255 editing scale and are mapped across
MIDI velocity 0–127. Modulations are selected and saved independently in
`~/Library/Hew/LivePatternSequencer/velocity-modulations.json`.

## One-time setup

Open Terminal and install Apple's command-line development tools:

```bash
xcode-select --install
```

Install Homebrew if it is not already installed, following the instructions at <https://brew.sh>.

Then install CMake and Git:

```bash
brew install cmake git
```

## Build and install

In Terminal, enter the extracted project directory and run:

```bash
chmod +x build-macos.sh
./build-macos.sh
```

The first build downloads JUCE 9.0.1, so it takes longer than later builds.

The script:

1. Detects whether the Mac is Apple Silicon or Intel.
2. Configures and builds the project in Release mode.
3. Runs the portable-core tests.
4. Installs the plugin under the current user's account.
5. Applies an ad-hoc local code signature.

The installed bundle is:

```text
~/Library/Audio/Plug-Ins/VST3/Live Pattern Sequencer MVP.vst3
```

No administrator password is required because the plugin is installed only for the current user.

## Load it in REAPER

1. Open REAPER.
2. Open **REAPER → Settings** or **Options → Preferences**.
3. Select **Plug-ins → VST**.
4. Make sure the standard macOS VST3 directory is available to REAPER.
5. Click **Re-scan**.
6. Create a track and open its FX chain.
7. Add **Live Pattern Sequencer MVP** first.
8. Add a drum machine or sampler immediately after it.
9. Make sure the drum instrument responds to MIDI channel 11 and the fixed notes listed in `README.md`; Synth 1 and Synth 2 use channels 12 and 13.
10. Press Play in REAPER.

The sequencer should follow REAPER's tempo and transport, and the following drum instrument should produce sound.

## Live logs and troubleshooting

The plugin writes asynchronous, single-line logs to:

```text
~/Library/Hew/LivePatternSequencer/sequencer.log
```

Watch them live from the project directory:

```bash
./scripts/watch-logs.sh
```

Pass an extended regular expression to filter the live stream. Every line has
stable `level=`, `component=`, and `event=` fields, so ordinary grep works well:

```bash
./scripts/watch-logs.sh 'level=(WARN|ERROR)'
grep 'component=runtime_graph' ~/Library/Hew/LivePatternSequencer/sequencer.log
grep 'event=process_overflow' ~/Library/Hew/LivePatternSequencer/sequencer.log
```

The default level is `info`. Set `LPS_LOG_LEVEL` to `debug`, `info`, `warn`,
`error`, or `off` before launching REAPER. Set `LPS_LOG_STDERR=1` to mirror the
same lines to the host process's standard error stream. Core logging uses a
bounded asynchronous queue: the audio thread never waits for disk I/O, and the
periodic `event=status` line reports `logger_dropped` if the writer falls behind.
It reports `logger_write_failures` if the file becomes unwritable; those lines
also fall back to standard error automatically.
At startup, a log of 10 MiB or larger is rotated to `sequencer.log.1`.
See [Core logging](docs/logging.md) for the host API and event reference.

## If macOS blocks something

This is source code you build locally, and the script applies an ad-hoc signature to the resulting VST3. If Terminal itself reports a permission problem with the script, run:

```bash
chmod +x build-macos.sh
```

If REAPER previously rejected an older copy of the plugin, remove the failed entry from REAPER's VST cache and run a full re-scan after rebuilding.

## Architecture boundary

The JUCE and VST3-specific code is under `src/plugin`. The replaceable sequencer implementation is under `src/core` and depends only on standard C++.

See the detailed [core architecture and integration guide](docs/core-architecture.md)
for component contracts, safety mechanisms, and the services another plugin
framework must provide.

Fixed-note `PatternPlayer` instances are normal `IPlayer` implementations. Any number can be
added to `SequencerEngine`; they use the same lifecycle, routing, and suppression matrix.
