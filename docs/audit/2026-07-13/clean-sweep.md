# Clean Sweep and Optimization Record

Date: 2026-07-13

## Immutable baseline

- `build-full322/Caustic3Vita-01.00-final.vpk`
- Size: 45,950,091 bytes
- SHA-256: `96f3bda9acbecf09d95c03059de02bb76f535573de830d50ff1dcfe19da5ae0f`
- The installed Vita `eboot.bin` still matches this baseline.
- The same verified fallback VPK remains at `ux0:data/Caustic3Vita-01.00-final.vpk`.

## 01.01 audit candidate

- `build-full322/Caustic3Vita-01.01-audit-candidate.vpk`
- Size: 45,910,621 bytes
- SHA-256: `66da00abaf5cf49ef6987e9fb94a56b8b08bb450ddf103089c40cda7b33061f3`
- ZIP integrity: passed for all 182 entries.
- Package comparison: the entry set is identical to 01.00; only `eboot.bin` and `sce_sys/param.sfo` changed.
- `param.sfo` identifies version 01.01 and title ID `CSTC00001`.
- The VPK copied to `ux0:data/Caustic3Vita-01.01-audit-candidate.vpk` is byte-identical to the local candidate.

## 01.01 final hardware-validated build

- Size: 45,951,294 bytes
- VPK SHA-256: `37922f6ba3e4d878eddd6e1a2bf4677970caf58f59fbc449ecc2228694898a26`
- `eboot.bin` size: 637,254 bytes
- `eboot.bin` SHA-256: `78abe06412bec32c51b2f47fb75692949e5577d2e82caedad24c8f9cb45298e7`
- ZIP integrity passed for all 182 entries.
- Physical Vita testing passed after disabling linker garbage collection.

## Security and reliability changes

- Added a centralized, length-checked filesystem policy and routed `fopen`, `open`, `__open_2`, `stat`, `opendir`, `mkdir`, `rename`, `remove`, `unlink`, `rmdir`, and `chmod` through it.
- Confined writable operations to `ux0:/data/CAUSTIC3`, restricted `app0:` to read-only access, and rejected traversal, unknown mounts, and truncation.
- Added host-side path-policy tests, including Android path mapping, traversal, unauthorized mounts, read-only package access, relative paths, and truncation.
- Initially changed `pthread_once` so concurrent callers waited for completion;
  physical Vita testing later showed Caustic requires the established
  re-entrant compatibility behavior, which the final release restores.
- Corrected absolute timeout arithmetic in `sem_timedwait`.
- Added a missing allocation failure check to the mmap compatibility function.
- Hardened APK demo extraction against short files, EOCD underflow, invalid offsets, oversized entries, unsafe names, output-path truncation, and out-of-file compressed data.
- Verified every one of the full322 APK's 946 demo entries complies with the new extractor policy.
- Changed packaging to private per-build staging and optional Vita3K store-only output.

## Measured cleanup and optimization

- Candidate-only function/data section garbage collection reduced `eboot.bin`
  by 6.17%, but that candidate failed immediately after its first rendered
  frame on physical Vita hardware. The final release disables garbage
  collection because runtime-resolved wrapper symbols are not visible to the
  static linker's reachability analysis.
- The final VPK is 1,203 bytes larger than the 01.00 baseline while retaining
  its complete package entry set and adding the hardened wrapper behavior.
- Removed 104,845,418 bytes of regenerable duplicate `.vpk.out` files and local metadata/cache files.
- Preserved all named milestone VPKs, recovery snapshots, backups, the unique microphone capture, the 01.00 final, and the 01.01 candidate.
- Historical artifacts still consume about 1.8 GiB across `build-full322`, `build-new`, and `release-baseline`; they were retained because deletion would remove rollback evidence rather than merely clean generated intermediates.

## Verification performed

- Full VitaSDK full322 compile and VPK package: passed.
- Host path-policy build with `-Wall -Wextra -Werror`: passed.
- Path-policy test suite: passed.
- Shell syntax and Python helper compilation: passed.
- Git whitespace/error check: passed.
- Candidate ZIP test and baseline/candidate entry comparison: passed.
- Local/Vita candidate SHA-256 and byte comparison: passed.
- Stable 01.00 local/Vita SHA-256 and installed `eboot.bin` comparison: passed.

## Remaining known risk

The 01.01 release passed physical Vita regression testing and replaces 01.00.
Continuous microphone capture remains open pending a trustworthy Caustic
recording-state signal; see the main security report for privacy, battery, and
performance implications.
