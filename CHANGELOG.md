# Changelog

## 01.01 — 2026-07-13 (stable 1.0 release)

- Added centralized filesystem confinement for all imported path operations.
- Rejected traversal, unknown mounts, truncation, and packaged-data writes.
- Hardened demo ZIP parsing and extraction bounds.
- Retained the established Vita-wrapper `pthread_once` compatibility behavior;
  corrected timed semaphore arithmetic and allocation handling.
- Kept linker section garbage collection disabled after physical Vita testing
  proved that Caustic reaches required symbols indirectly at runtime.
- Switched builds to private temporary staging.
- Published security audit and AI-assistance disclosure.
- Passed physical Vita regression testing for boot, rendering, touch, audio,
  project loading, the Vita Means Life welcome demo, and normal interaction.

## 01.00 — 2026-07-13 (known-good private hardware baseline)

- Functional Caustic 3.2.2 Vita wrapper.
- vitaGL rendering and fullscreen UI.
- Corrected audio conversion and geometry behavior.
- Front multi-touch and optional controller layer.
- Microphone input, preset loading, project saving, Vita branding, and
  first-launch welcome behavior.
