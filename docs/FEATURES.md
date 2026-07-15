# Feature and compatibility matrix

## Confirmed on physical Vita hardware

| Area | Status | Notes |
|---|---|---|
| Application boot/lifecycle | Working | ARMv7 module loads and reaches the Caustic UI. |
| Rendering | Working | GLES2 translated through vitaGL on a 960×544 logical surface. |
| UI transition pacing | Working | Explicit VBlank producer pacing reduces intermediate-frame flashes. |
| Audio output | Working with stress limitation | Direct 44.1 kHz Vita BGM output; dense polyphony can produce harsh artifacts. |
| Synth and BeatBox playback | Working with stress limitation | Machine-specific compatibility fixes included; some feedback-heavy Modular patches remain problematic. |
| Preset discovery/loading | Working | APK factory content is extracted into the Vita data root. |
| Additional presets | Working in enhanced build | Curated presets beyond the vanilla factory library can be installed without replacing factory content. |
| Bundled skins | Working | Default, Flat, Blackbox, Frost, Darker, newskin, Julia, and Echo were normalized and checked on physical hardware. |
| Save/load | Working | Projects save beneath `ux0:/data/CAUSTIC3/`. |
| Front touch | Working | Direct touch mapping at Vita resolution. |
| Multi-touch | Working | Multiple independent front-panel pointers support chords. |
| Microphone | Working, compatibility behavior | Built-in/headset input works; capture runs continuously, matching original Caustic's initialization strategy. |
| Physical controls | Experimental/working | Optional touch-synthesis layer toggled with Triangle. |
| Left-stick rack scrolling | Experimental/working | Continuous synthetic swipe on the rack rail. |
| Bubble/LiveArea | Working | Custom project-created Vita assets included. |

## Controller features

- Physical controls are off by default, preserving normal touch behavior.
- Triangle toggles controller mode and releases active synthetic pointers.
- D-pad focus supports press-and-hold repeat.
- Cross grabs a control; D-pad movement then produces fine touch dragging.
- Start synthesizes transport play/stop taps.
- Select opens machine management and positions focus near the first slot.
- The left stick produces re-anchored vertical swipes for long rack movement.
- Physical multi-touch and controller-generated pointers occupy separate IDs.

## Data and packaging

- Supported APKs are selected by exact SHA-256.
- The embedded ARMv7 library is independently hash-checked.
- Demo/factory content is extracted once using a CRC-checking ZIP reader.
- Existing user files are preserved when optional bundles are installed.
- The enhanced Vita build can layer curated non-vanilla presets over the
  original factory library through the optional extras path.
- The filesystem bridge maps Android storage roots to the Vita data root.
- Release 01.01 rejects traversal, unknown mounts, truncation, and writes to
  packaged `app0:` content.

## Unsupported or incomplete

- USB MIDI input
- Android billing, Play Store, or unlocker workflows
- Android “Get More” integration
- Guaranteed compatibility with unrecognized APK revisions
- Independent audit of proprietary Caustic parser/server internals
- On-demand microphone lifecycle (not planned without a reliable record-state signal)
- Automatic compatibility for arbitrary user-added Android skins
- Artifact-free playback of every dense-polyphony or feedback-heavy Modular project
