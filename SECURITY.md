# Security policy

## Supported version

Security fixes target the latest source revision. Historical experimental VPKs
and third-party repackages are not supported.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting feature when available. If it is
not enabled, open a minimal issue asking for a private contact channel without
publishing exploit details, private projects, device identifiers, or logs.

Include:

- affected commit/version;
- Vita model, firmware, and relevant plugins;
- reproducible input and steps using disposable data;
- expected versus actual behavior;
- impact and attack preconditions;
- sanitized logs or crash addresses.

Do not test against data you do not own, attempt network access to another
person's Vita, publish copyrighted APK/native binaries, or include ambient
recordings without consent.

## 2026-07-13 audit status

The repository-wide AI-assisted audit reviewed 73 maintained files and the
native import surface. Canonical artifacts are under `docs/audit/2026-07-13/`.

| Finding | Severity | Status |
|---|---:|---|
| Continuous microphone capture from launch | Medium | Accepted compatibility behavior; disclosed |
| `fopen` path escape | Low | Fixed in 01.01 path policy |
| `open`/`__open_2` path escape | Low | Fixed in 01.01 path policy |

## Important limitation

Caustic's proprietary ARM library is stripped. Its internal file parsers and
embedded network/server code were not available for source review. The wrapper
adds boundary controls, but those controls are not proof that opaque internals
are free of vulnerabilities.

## Microphone disclosure

The current full322 wrapper opens the Vita microphone at launch and continuously
passes fixed-size audio blocks to Caustic's recording callback. Caustic creator
Rej Poirier confirmed that original Caustic behaved the same way because
toggling microphone initialization could intermittently fail. Wrapper buffers
are fixed and reused, and the wrapper does not save audio files, but active
capture still has privacy, battery, and performance implications. Use the Vita
hardware mute when microphone input is not needed. Replacing this behavior with
record-state gating would require a reliable signal and hardware regression
testing; it is not currently planned as an unconditional security fix.
