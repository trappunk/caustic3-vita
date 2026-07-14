# Changelog

## 01.01 — 2026-07-13 (public beta candidate)

- Added centralized filesystem confinement for all imported path operations.
- Rejected traversal, unknown mounts, truncation, and packaged-data writes.
- Hardened demo ZIP parsing and extraction bounds.
- Corrected `pthread_once`, timed semaphore arithmetic, and allocation handling.
- Enabled linker section garbage collection; reduced eboot size by 6.17%.
- Switched builds to private temporary staging.
- Published security audit and AI-assistance disclosure.

## 01.00 — 2026-07-13 (known-good private hardware baseline)

- Functional Caustic 3.2.2 Vita wrapper.
- vitaGL rendering and fullscreen UI.
- Corrected audio conversion and geometry behavior.
- Front multi-touch and optional controller layer.
- Microphone input, preset loading, project saving, Vita branding, and
  first-launch welcome behavior.
