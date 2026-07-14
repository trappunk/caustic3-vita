# Security Review: Vita Caustic

## Scope

Repository-wide review of all 73 maintained source/configuration files plus shipped native import inspection.

- Scan mode: repository
- Target kind: git_worktree
- Target ID: local-workspace-sha256:e1b6ffbcdc3cf4c0c882b5e994b87f52641ae9116fd5c17d5dbcf4954e4f09e7
- Revision: 69c14c7dc438
- Snapshot digest: codex-security-snapshot/v1:sha256:2ec6fdeae02e8778bb88b23162adcd7c1bac9c6f654b27105af75eb3e51fc235
- Inventory strategy: repository
- Included paths: .
- Excluded paths: .deps/\*\*, .toolchain/\*\*, build\*/\*\*, \*\*/\*.vpk, \*\*/\*.apk, \*\*/\*.so, \*\*/\*.a, backups/\*\*, recovery/\*\*
- Runtime or test status: native-v2-ready
- Artifacts reviewed: build-full322/Caustic3Vita-01.00-final.vpk, embedded libcaustic.so import table, local Caustic project path corpus

Limitations and exclusions:
- Sub-agent concurrency was unavailable by execution policy.
- The stripped proprietary libcaustic.so parser/server internals are outside source coverage.
- No destructive or cross-title filesystem proof was attempted on Vita hardware.
- Excluded .deps/\*\* and .toolchain/\*\*: Generated dependency/toolchain material is outside maintained-source review.
- Excluded build\*/\*\*, \*.vpk, \*.apk, \*.so, \*.a: Generated binaries were excluded from source review; the shipped native import table and release hashes were inspected separately.
- Excluded backups/\*\*, recovery/\*\*, copied assets: Historical/generated copies were excluded from source discovery and assigned to the separate clean-sweep review.

### Scan Summary

| Field | Value |
| --- | --- |
| Reportable findings | 3 |
| Severity mix | medium: 1, low: 2 |
| Confidence mix | high: 1, medium: 2 |
| Coverage | partial |
| Validation mode | parent-only exhaustive static review with deterministic local checks and existing Vita hardware observations |

Canonical artifacts: `scan-manifest.json`, `findings.json`, and `coverage.json`. This report is a deterministic projection of those files.

## Threat Model

Treat imported projects, presets, samples, skins, filenames, and ambient microphone audio as untrusted across filesystem, parser, native ABI, graphics, audio, and privacy boundaries.

### Assets

- Vita filesystem integrity
- Caustic projects and presets
- application availability
- microphone privacy
- release artifact integrity

### Trust Boundaries

- untrusted Caustic content to proprietary native parser
- proprietary ARM module to Vita compatibility wrappers
- ambient audio to microphone callback
- developer build inputs to VPK

### Attacker Capabilities

- Supply a crafted Caustic project/preset/sample/skin
- Control serialized filenames and storage paths
- Influence ambient audio while the app is running

### Security Objectives

- Confine file operations to authorized roots
- Avoid unexpected microphone collection
- Prevent content-triggered memory corruption or code execution
- Preserve release provenance

### Assumptions

- The installed VPK and hash-pinned embedded APK/SO are trusted
- The Vita kernel and required plugins are outside this repository audit

## Findings

| Finding | Severity | Confidence | Detailed write-up |
| --- | --- | --- | --- |
| [Application captures microphone audio continuously from launch](#finding-1) | medium | high | inline below |
| [Crafted Caustic paths escape the data root through fopen_soloader](#finding-2) | low | medium | inline below |
| [Crafted Caustic paths escape the data root through open_soloader](#finding-3) | low | medium | inline below |

### Confidence Scale

| Label | Meaning |
| --- | --- |
| high | Direct evidence supports the finding with no material unresolved blocker. |
| medium | Evidence supports a plausible issue, but material runtime or reachability proof remains. |
| low | Evidence is incomplete and the item is retained only for explicit follow-up. |

<a id="finding-1"></a>

### [1] Application captures microphone audio continuously from launch

| Field | Value |
| --- | --- |
| Severity | medium |
| Confidence | high |
| Confidence rationale | Direct control-flow review shows unconditional thread startup and capture until shutdown; hardware testing confirmed the microphone path is active when the Vita hardware mute is disabled. |
| Category | privacy |
| CWE | CWE-359 |
| Affected lines | src/main.c:820, src/main.c:482, src/main.c:579 |

#### Summary

The full322 startup path opens the Vita microphone and continuously forwards ambient samples to the proprietary recording callback even when the user has not entered a recording workflow.

#### Root Cause

Microphone lifetime is coupled to application lifetime instead of an explicit user recording state.

**Unconditional microphone startup** — `src/main.c:820-827`

The full322 startup path starts microphone capture during application initialization without checking an in-app recording state.

```c
#ifdef CAUSTIC_FULL_322
    g_audio.mic_running = 1;
    SceUID mic_uid = sceKernelCreateThread("caustic_microphone", microphone_thread, ...);
    if (mic_uid >= 0)
        sceKernelStartThread(mic_uid, 0, NULL);
```

#### Validation

Direct control-flow review shows unconditional thread startup and capture until shutdown; hardware testing confirmed the microphone path is active when the Vita hardware mute is disabled. Validation details were not recorded separately.

Validation method: static-control-flow-plus-hardware-observation

#### Dataflow

The canonical finding records the affected path at src/main.c:820, src/main.c:482, src/main.c:579, but no expanded source-to-sink narrative was recorded.

#### Reachability

Reachability was not recorded beyond the canonical finding summary and affected locations.

#### Severity

**Medium** — The scan assigned medium severity; no separate canonical severity rationale was recorded.

Additional runtime or deployment evidence could raise or lower this severity.

#### Remediation

Start capture only after an explicit in-app recording action, stop it immediately when recording ends, expose a visible capture indicator, and clear captured buffers after use.

Tests:
- Launch and idle on every screen while verifying the microphone input port is closed.
- Enter sampler and vocoder recording flows and verify capture starts only after the record action and stops on completion/cancel.
- Suspend, resume, and exit during recording and verify the input port closes and buffers are cleared.

Preventive controls:
- Centralized microphone state machine
- Visible recording indicator
- Lifecycle tests around suspend/resume/exit

<a id="finding-2"></a>

### [2] Crafted Caustic paths escape the data root through fopen_soloader

| Field | Value |
| --- | --- |
| Severity | low |
| Confidence | medium |
| Confidence rationale | The unsafe path construction and shipped fopen import are direct evidence, and real Caustic files serialize absolute paths; an end-to-end benign-sentinel Vita PoC through the stripped parser was not performed. |
| Category | path-traversal |
| CWE | CWE-22, CWE-23 |
| Affected lines | src/dynlib.c:724, src/reimpl/io.c:122, src/reimpl/io.c:132 |

#### Summary

The fopen compatibility bridge strips an Android storage prefix and joins the remainder to the Caustic data root without canonicalizing dot segments or enforcing containment.

#### Root Cause

Prefix replacement is treated as path confinement even though it neither normalizes segments nor verifies the canonical destination remains beneath the allowed root.

**Unchecked fopen path join** — `src/reimpl/io.c:119-132`

The suffix is concatenated without normalization or containment; dot segments survive to native fopen.

```c
else if (strncmp(fname, "/sdcard/", 8) == 0) {
    sceClibSnprintf(fopen_path_real, sizeof(fopen_path_real), DATA_PATH "%s", fname + 8);
}
...
FILE* ret = sceLibcBridge_fopen(fopen_path_real, mode);
```

#### Validation

The unsafe path construction and shipped fopen import are direct evidence, and real Caustic files serialize absolute paths; an end-to-end benign-sentinel Vita PoC through the stripped parser was not performed. Validation details were not recorded separately.

Validation method: static-trace-and-deterministic-path-construction

#### Dataflow

The canonical finding records the affected path at src/dynlib.c:724, src/reimpl/io.c:122, src/reimpl/io.c:132, but no expanded source-to-sink narrative was recorded.

#### Reachability

Reachability was not recorded beyond the canonical finding summary and affected locations.

#### Severity

**Low** — The scan assigned low severity; no separate canonical severity rationale was recorded.

Additional runtime or deployment evidence could raise or lower this severity.

#### Remediation

Route every filesystem bridge through a single length-checked normalizer that rejects dot-segment escape and unauthorized Vita mount paths, and restrict app0 access to read-only operations.

Tests:
- Reject Android paths containing traversal segments that would escape ux0:/data/CAUSTIC3.
- Reject direct Vita paths outside the approved data root while preserving valid Caustic assets.
- Verify read, create, append, and truncate modes independently.

Preventive controls:
- Central path-policy helper
- Canonical containment tests
- Truncation checks

<a id="finding-3"></a>

### [3] Crafted Caustic paths escape the data root through open_soloader

| Field | Value |
| --- | --- |
| Severity | low |
| Confidence | medium |
| Confidence rationale | The unsafe path construction and shipped open/__open_2 imports are direct evidence, and real Caustic files serialize absolute paths; an end-to-end benign-sentinel Vita PoC through the stripped parser was not performed. |
| Category | path-traversal |
| CWE | CWE-22, CWE-23 |
| Affected lines | src/dynlib.c:478, src/dynlib.c:903, src/reimpl/io.c:150, src/reimpl/io.c:157 |

#### Summary

The open and fortified __open_2 compatibility bridges replace Android storage prefixes without canonicalization or a containment check before native open.

#### Root Cause

The open bridge performs syntactic prefix substitution but does not enforce a filesystem policy on the resulting Vita path.

**Unchecked open path join** — `src/reimpl/io.c:148-157`

The suffix is concatenated without normalization or containment; dot segments survive to native open.

```c
if (strncmp(_fname, "/storage/emulated/0/", 20) == 0)
    sceClibSnprintf(mapped, sizeof(mapped), DATA_PATH "%s", _fname + 20);
...
int ret = open(mapped, flags);
```

#### Validation

The unsafe path construction and shipped open/__open_2 imports are direct evidence, and real Caustic files serialize absolute paths; an end-to-end benign-sentinel Vita PoC through the stripped parser was not performed. Validation details were not recorded separately.

Validation method: static-trace-and-deterministic-path-construction

#### Dataflow

The canonical finding records the affected path at src/dynlib.c:478, src/dynlib.c:903, src/reimpl/io.c:150, src/reimpl/io.c:157, but no expanded source-to-sink narrative was recorded.

#### Reachability

Reachability was not recorded beyond the canonical finding summary and affected locations.

#### Severity

**Low** — The scan assigned low severity; no separate canonical severity rationale was recorded.

Additional runtime or deployment evidence could raise or lower this severity.

#### Remediation

Use the same centralized, length-checked path-policy helper as fopen; reject traversal, unauthorized mounts, and write access to app0 before invoking native open.

Tests:
- Exercise open and __open_2 with traversal paths for read-only and write flags.
- Reject direct Vita paths outside the approved root and all app0 write attempts.
- Confirm valid project, preset, sample, skin, save, and export operations still work.

Preventive controls:
- Shared path-policy helper
- Flag-aware mount policy
- Filesystem bridge integration tests

## Structural Hardening

The scan also produced derived, unsealed design guidance based on the complete finding collection. These proposals describe options and tradeoffs; they do not indicate that any finding has been remediated.

[Open the structural hardening portfolio](hardening/hardening.md)

## Reviewed Surfaces

| Surface | Risk Area | Outcome | Notes |
| --- | --- | --- | --- |
| Build and VPK packaging | not recorded | No issue found | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| ARM ELF loader and relocation | not recorded | No issue found | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Android and JNI bridge | not recorded | Rejected | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Filesystem translation and project content | not recorded | Reported | No additional canonical notes were recorded. Evidence: artifacts/05_findings/CAND-001/attack_path_analysis_report.md, artifacts/05_findings/CAND-002/attack_path_analysis_report.md |
| Embedded APK and ZIP demo extraction | not recorded | Rejected | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Fortify and formatting compatibility | not recorded | No issue found | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Audio output and microphone capture | not recorded | Reported | No additional canonical notes were recorded. Evidence: artifacts/05_findings/CAND-003/attack_path_analysis_report.md |
| Graphics, shaders, and render geometry | not recorded | No issue found | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Touch, controller, and synthesized input | not recorded | No issue found | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Logging and exception handling | not recorded | No issue found | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Maintained network, authentication, and database boundaries | not recorded | Not applicable | No additional canonical notes were recorded. Evidence: artifacts/03_coverage/repository_coverage_ledger.md |
| Proprietary Caustic parsers and embedded server | not recorded | Needs follow-up | No additional canonical notes were recorded. Evidence: artifacts/01_context/libcaustic_imports.txt, artifacts/03_coverage/repository_coverage_ledger.md |
| Maintained vendored primitives | not recorded | Rejected | No additional canonical notes were recorded. Evidence: artifacts/02_discovery/work_ledger.jsonl, artifacts/03_coverage/repository_coverage_ledger.md |
| Generated dependencies and archived artifacts | not recorded | Needs follow-up | No additional canonical notes were recorded. Evidence: artifacts/02_discovery/generated_exclusions.txt, artifacts/03_coverage/repository_coverage_ledger.md |

## Open Questions And Follow Up

- Can a benign crafted Caustic project drive each path bridge to a sentinel outside the data root on hardware?
  - Follow-up prompt: Run a non-destructive sentinel read/write test against a disposable Vita data directory after adding detailed bridge logging.
- What exact proprietary callback or state indicates that sampler/vocoder recording has armed and disarmed?
  - Follow-up prompt: Trace native imports/callbacks around the recording UI so microphone lifetime can be gated without regressing working capture.
- The stripped proprietary Caustic parser and embedded server source is unavailable, so their internal memory safety and authentication logic cannot be audited.
  - Follow-up prompt: Review deferred unit DEF-001 and close its stated proof gap. Paths: libcaustic.so. Surfaces: COV-012.
- Generated toolchains, binary artifacts, and backups require provenance/retention review rather than maintained-source vulnerability discovery.
  - Follow-up prompt: Review deferred unit DEF-002 and close its stated proof gap. Paths: .deps/\*\*, .toolchain/\*\*, build\*/\*\*, backups/\*\*, recovery/\*\*. Surfaces: COV-014.
