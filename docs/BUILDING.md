# Building

## Prerequisites

- macOS or Linux development host
- VitaSDK configured for `arm-vita-eabi`
- softfp-compatible builds of vitaGL, vitaShaRK, math-neon, kubridge, and
  required Vita stubs
- CMake, Python 3, `unzip`, and OpenSSL
- A legally obtained APK matching a supported profile

The supported maintenance APK is published by the Caustic developer on the
[official Caustic 3 page](https://www.singlecellsoftware.com/caustic3.html). End
users should begin with the [Vita installation tutorial](INSTALLING.md).

All linked ARM libraries must agree on `-mfloat-abi=softfp` and the expected
NEON configuration. ABI mismatches commonly produce link failures or runtime
crashes.

## Supported profiles

The build script selects profiles by exact APK and ARMv7 library SHA-256. It
currently knows:

- `full322`: primary enhanced Vita profile, based on
  `Caustic_3.2.2_64b.apk`—the most recent official Android maintenance build
  published by the Caustic developer
- `demo3320`: legacy compatibility profile for the older Caustic 3.3.2.0 demo
  APK used during early wrapper development

Unknown files fail closed. Adding a profile requires a source review of its
imports, offsets, lifecycle, renderer hooks, and native symbol addresses—not
just adding another hash.

## Build

Set `VITASDK` if your SDK is not located at `.toolchain/vitasdk`:

```sh
export VITASDK=/opt/vitasdk
./scripts/build-vpk.sh /absolute/path/to/caustic.apk
```

The script:

1. verifies the APK hash;
2. creates private temporary staging;
3. extracts APK assets and the ARMv7 shared library;
4. independently verifies the library hash;
5. configures the corresponding CMake profile;
6. builds the eboot and VPK;
7. removes temporary staging on exit.

The APK, shared library, extracted assets, VPK, toolchain, and build trees are
ignored by Git and must never be committed.

## Optional local extras

Optional content is off by default. To package content you have the right to
use, create all three directories:

```text
extras/skins/newskin/
extras/presets/
extras/songs/demo/
```

Then build with:

```sh
CAUSTIC_INCLUDE_EXTRAS=1 ./scripts/build-vpk.sh /path/to/caustic.apk
```

Do not redistribute third-party packs without permission.

The enhanced Vita build tested by the project maintainer uses this mechanism
to add curated presets beyond the vanilla Caustic library. Optional skins use
the same mechanism, but third-party skins are not guaranteed to render exactly
as they do on Android; the default skin remains the compatibility baseline.

## Vita3K archive

The standard compressed VPK is intended for Vita. A larger store-only archive
can help Vita3K installation performance:

```sh
BUILD_FAST_INSTALL=1 ./scripts/build-vpk.sh /path/to/caustic.apk
```

Vita3K is useful for packaging/smoke checks but cannot replace physical Vita
testing for microphone, plugins, touch, timing, and audio behavior.

## Host tests

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc \
  -DDATA_PATH='"ux0:/data/CAUSTIC3/"' \
  tests/path_policy_test.c src/reimpl/path_policy.c \
  -o /tmp/path-policy-test
/tmp/path-policy-test
bash -n scripts/build-vpk.sh
bash -n scripts/verify-public-tree.sh
```
