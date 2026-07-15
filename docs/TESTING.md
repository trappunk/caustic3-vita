# Physical Vita test checklist

## Installation and rollback

- Preserve the last known-good VPK and its SHA-256.
- Install the candidate through VitaShell.
- Confirm title ID `CSTC00001` and expected version.
- Keep a copy of `ux0:/data/CAUSTIC3/` before destructive tests.

## Boot and UI

- Cold boot and warm relaunch.
- First-launch factory-content extraction.
- Default skin startup and subsequent skin persistence.
- Skin selector and preset-list text under every bundled skin: Default, Flat,
  Blackbox, Frost, Darker, newskin, Julia, and Echo.
- Open every machine type and management screen.
- Check rapid transitions for intermediate-frame flashes.
- Verify LiveArea, icon, and startup artwork.

## Touch and controls

- Single touch, drags, knobs, sliders, and selectors.
- Two or more simultaneous keyboard notes.
- Triangle controller toggle and focus indicator.
- D-pad navigation and hold repeat.
- Cross grab/adjust/release on several control types.
- Start transport toggle, Select machine management, left-stick rack swipe.
- Disable controller mode while a synthetic pointer is active.

## Audio

- Low and high notes across several synths.
- 808/sub-bass material for clipping/clicks.
- Dense projects for underruns.
- Increase polyphony one voice at a time and record the first voice count that
  produces harsh digital artifacts.
- Stress feedback-heavy Modular patches, then stop transport and verify that
  every voice releases.
- Start/stop/restart transport repeatedly.
- Headphones and Vita speakers.

Known hardware result: dense polyphony and some Modular configurations can
produce harsh artifacts even with direct 44.1 kHz output. Buffer-size,
microphone, rendering-rate, CPU-affinity, and storage-backed-memory candidates
did not eliminate it. Treat this as a release limitation, not as proof of a
speaker or headphone fault.

## Microphone

- Built-in mic and compatible headset mic.
- Hardware mute enabled/disabled.
- Sampler and vocoder recording.
- Cancel, complete, suspend, resume, and exit around recording.
- Watch battery/thermal behavior during an extended idle session because the
  current build captures continuously.
- Treat microphone lifecycle changes as high-risk compatibility work: original
  Caustic kept input initialized to avoid intermittent reinitialization failure.

## Data

- Browse and load factory presets.
- Save, overwrite, rename, delete, reload, and export a disposable project.
- Load a project containing Android-style absolute sample paths.
- Confirm path-policy rejection does not break normal project files.
- Never test traversal against valuable files; use disposable sentinels only.

## Evidence

Record candidate SHA-256, Vita model/firmware, installed plugins, result, and
`ux0:/data/CAUSTIC3/debug.log`. Remove private paths or recording information
before publishing logs.
