# Architecture

## Native module loading

The wrapper extracts the APK's ARMv7 `libcaustic.so`, verifies its expected
SHA-256, packages it under `app0:/lib/`, maps the ELF at a fixed Vita address,
applies relocations, resolves imports against wrapper/native functions, flushes
caches, and runs constructors.

The binary remains Caustic's original Android code. This repository does not
contain it and does not translate it ahead of time.

## Minimal Android environment

FalsoJNI and the wrapper provide the subset of JNI and Android lifecycle calls
used by Caustic. Startup calls configure OS version, capabilities, storage,
microphone availability, screen size, audio latency, APK path, root path, and
device identity before creating machines and entering the renderer.

## Graphics

OpenGL ES imports resolve to vitaGL. vitaShaRK and `libshacccg.suprx` provide
runtime shader translation. Compatibility hooks account for Caustic's Android
shader/geometry assumptions. Rendering targets 960×544, and the producer waits
for VBlank so intermediate menu/rack transition states are not over-queued.

## Audio output

Caustic produces 256-frame stereo blocks at 44.1 kHz. The Vita BGM port accepts
that rate directly, so the wrapper submits fixed-size blocks to `SceAudioOut`
without output resampling. This avoids resampling coloration and reduces work
on the real-time audio thread.

Physical-hardware stress testing found that dense polyphony and certain
feedback-heavy Modular configurations can still generate harsh digital
artifacts. Output-buffer, sample-rate, microphone, rendering-rate, CPU-affinity,
and file-backed-memory experiments did not remove the problem. Evidence points
to the stripped native DSP path or machine-specific compatibility behavior,
which cannot be fully audited without Caustic's source.

## Microphone input

The Vita voice input port captures 768-frame, 48 kHz mono blocks. A continuous
phase resampler converts them to Caustic's 256-frame, 44.1 kHz mono callback.
The input thread starts with the app rather than only when Caustic arms
recording. Rej Poirier confirmed this matches original Caustic's strategy:
keeping input initialized avoided intermittent failures caused by toggling the
microphone on and off. Buffers are fixed-size and reused; the wrapper does not
write captured samples to disk.

## Input

Front-touch coordinates map from the Vita's touch range to the 960×544 logical
surface. Hardware touch IDs occupy native slots; synthetic controller pointers
use reserved IDs. Physical controls operate by emitting the same touch begin,
move, and end calls Caustic receives on Android.

## Filesystem

Android `/sdcard/` and `/storage/emulated/0/` paths map beneath
`ux0:/data/CAUSTIC3/`. A shared policy validates every imported file operation:

- parent traversal is rejected;
- unknown Vita mounts and unrecognized absolute paths are rejected;
- writable access is confined to the Caustic data root;
- `app0:` is read-only;
- path truncation fails closed.

## Content extraction

The full322 profile extracts `assets/demo/` from the user's packaged APK on
first launch. The ZIP reader validates central/local records, entry offsets,
sizes, names, CRCs, output containment, and path length. Existing matching
files are skipped.

## Trust boundary

Projects, presets, skins, samples, filenames, and ambient microphone audio are
treated as untrusted. The installed VPK, Vita kernel/plugins, and hash-pinned
APK/native binary are treated as trusted administrative inputs. Proprietary
Caustic parsers remain opaque and are explicitly outside source coverage.
