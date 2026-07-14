# Contributing

Thank you for helping improve Caustic 3 Vita.

## Ground rules

- Keep changes narrowly scoped and explain the physical Vita behavior they
  affect.
- Preserve touch-first operation and the known-good data directory.
- Add or update tests for pure host-testable compatibility logic.
- Document Vita model, firmware, plugins, APK profile, and candidate hash for
  hardware results.
- Treat projects, presets, samples, skins, filenames, and microphone input as
  untrusted data.
- Do not weaken APK/native-library hash gates merely to accept another build.
- Do not submit APKs, VPKs, `libcaustic.so`, factory assets, unlock packages,
  third-party packs, private logs, secrets, or recordings.
- Disclose material AI assistance in the pull request.

## Checks

Run the host checks from `docs/BUILDING.md`, build the relevant Vita profile,
and follow `docs/TESTING.md` for changes that affect runtime behavior.

## Commit and pull request notes

Describe:

- the problem and root cause;
- files/paths affected;
- compatibility and security impact;
- checks performed;
- physical Vita evidence;
- rollback plan for risky loader/audio/graphics changes.
