# Security Hardening Review: Vita Caustic

## Evidence Basis

The repository scan reviewed every one of the 73 maintained files, inspected the shipped native library's import table, and validated three findings. Two low-severity findings share a missing filesystem-containment primitive. One medium-severity privacy finding results from tying microphone capture to application lifetime instead of recording lifetime. The stable 01.00 VPK remains an immutable reference artifact.

## Constraints

Vita hardware compatibility, existing Caustic project/preset behavior, low latency, and the already-working microphone flow must not regress. The proprietary Caustic engine is stripped, so the safest controls belong in the maintained wrapper. Performance and memory budgets were not measured during the scan; candidate builds require hardware regression testing.

## Opportunity Portfolio

No structural hardening portfolio is recommended. Local remediation is proportionate:

1. Introduce one filesystem-policy helper used by `fopen`, `open`, `__open_2`, `opendir`, and `stat`. Normalize Android storage paths, reject `..` escape and output truncation, allow only the Caustic data root for writable access, and allow packaged `app0:` resources read-only. Add table-driven path and flag tests.
2. Replace launch-to-exit microphone lifetime with an explicit state machine driven by the sampler/vocoder recording action. Open immediately before recording, close on complete/cancel/suspend/exit, show a capture indicator, and clear buffers. Do not ship this change until the native arm/disarm signal is identified and tested on hardware.
3. Treat non-reachable correctness defects as defense-in-depth work: add allocation checks, correct `pthread_once` state handling, harden ZIP bounds and entry-name validation, correct FalsoJNI region arithmetic, restore stricter compiler warnings, and make build staging private and reproducible.

## Recommendation Summary

Apply the shared path-policy fix first because it is source-contained and testable without changing normal UI/audio behavior. Prototype microphone gating separately behind a candidate-only switch until the true native recording-state signal is known. Bundle mechanical reliability fixes in small reviewable groups and compare each candidate against the 01.00 baseline.

## Next Decisions

- Decide whether to run the non-destructive sentinel path test on a disposable Vita directory after bridge logging is available.
- Identify the recording arm/disarm signal before changing microphone lifecycle.
- Set acceptable Vita budgets for launch time, audio underruns, input latency, RAM, and VPK size before claiming optimization gains.
