# AI assistance disclosure

## Summary

This project was developed by **trappunk** with extensive assistance from
**OpenAI Codex**. That assistance is disclosed prominently because the scope
was substantial and users should be able to evaluate the provenance of the
code and security claims.

## What Codex assisted with

- Repository and binary-import inspection
- ARMv7 wrapper/lifecycle debugging
- Import resolution and Android/Bionic compatibility functions
- vitaGL rendering and geometry troubleshooting
- Audio sample-rate, buffering, overload, and click-boundary analysis
- Vita microphone capture and input resampling
- Multi-touch and controller-generated touch logic
- Filesystem mapping, persistence, preset discovery, and saving
- APK/ZIP factory-content extraction
- Bubble/LiveArea packaging
- Build automation, reproducible staging, and VPK verification
- Documentation, test planning, cleanup, and optimization
- Repository-wide threat modeling and static security analysis

## Human direction and validation

trappunk selected the product behavior, supplied legally obtained test inputs,
performed repeated physical Vita tests, reported visual/audio/input behavior,
made release decisions, supplied original branding and demo direction, and
approved publication. Hardware observations were essential; Codex could not
independently operate the Vita during most debugging cycles.

## Security-audit disclosure

The 2026-07-13 audit was produced using the Codex Security workflow. It
included a complete maintained-file inventory, full-file static review,
candidate validation, attack-path analysis, canonical findings, coverage
receipts, and hardening guidance. It was AI-assisted and evidence-backed, but
it was not an independent third-party human audit or dynamic penetration test.

The stripped proprietary Caustic engine could only be inspected through its
imports, strings, behavior, and wrapper boundary. Its internal parsers and
server logic were not source-audited.

## Responsibility

AI assistance does not transfer responsibility to OpenAI. Project maintainers
remain responsible for review, testing, licensing, release decisions, and
responding to security reports. Contributors should review generated or
AI-assisted changes with the same care as any other code.
