#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APK="${1:-}"
SDK_SOURCE="${VITASDK:-$ROOT/.toolchain/vitasdk}"

if [[ -z "$APK" ]]; then
    echo "Usage: $0 /path/to/a/supported-caustic.apk" >&2
    exit 2
fi
if [[ ! -f "$APK" ]]; then
    echo "Caustic APK not found: $APK" >&2
    exit 1
fi
if [[ ! -x "$SDK_SOURCE/bin/arm-vita-eabi-gcc" ]]; then
    echo "VitaSDK not found: $SDK_SOURCE" >&2
    exit 1
fi

STAGING="$(mktemp -d /tmp/caustic3-build.XXXXXX)"
cleanup() { rm -rf "$STAGING"; }
trap cleanup EXIT
VITASDK="$STAGING/vitasdk"
ln -s "$SDK_SOURCE" "$VITASDK"

APK_SHA="$(env LC_ALL=C LANG=C openssl dgst -sha256 "$APK" | awk '{print $NF}')"
case "$APK_SHA" in
    6e21e0145eb4e191f9df8745f38658af71e3bd3832339064764b0abf1d72fde7)
        PROFILE="demo3320"
        EXPECTED_SO_SHA="4490ae1b55037685ece87b94f177cc0ab43d3057c75d39ce7bc47b99338ccf4d"
        BUILD="$ROOT/build-new"
        ;;
    7cf80508530e041821ab04693c6fc7bbd1fcd4c4b598cef825d3fd212b568ebf)
        PROFILE="full322"
        EXPECTED_SO_SHA="eb68b9158072a97b8f0b6d3a563918d6dbd1e98687b6732224ab83480ccd429b"
        BUILD="$ROOT/build-full322"
        ;;
    *)
        echo "Unsupported APK SHA-256: $APK_SHA" >&2
        exit 1
        ;;
esac
VPK_DATA="$STAGING/vpk-data-$PROFILE"

mkdir -p "$VPK_DATA"
unzip -q "$APK" 'assets/*' -d "$VPK_DATA/apk"
mv "$VPK_DATA/apk/assets" "$VPK_DATA/assets"
rmdir "$VPK_DATA/apk"

unzip -p "$APK" lib/armeabi-v7a/libcaustic.so > "$VPK_DATA/libcaustic.so"
SO_SHA="$(env LC_ALL=C LANG=C openssl dgst -sha256 "$VPK_DATA/libcaustic.so" | awk '{print $NF}')"
if [[ "$SO_SHA" != "$EXPECTED_SO_SHA" ]]; then
    echo "APK ARMv7 library does not match the audited $PROFILE binary." >&2
    exit 1
fi
INCLUDE_EXTRAS="${CAUSTIC_INCLUDE_EXTRAS:-0}"
if [[ "$INCLUDE_EXTRAS" == "1" ]]; then
    for required in skins/newskin presets songs/demo; do
        if [[ ! -d "$ROOT/extras/$required" ]]; then
            echo "Missing optional extras directory: $ROOT/extras/$required" >&2
            exit 1
        fi
    done
    ln -s "$ROOT/extras" "$VPK_DATA/extras"
fi
ln -s "$ROOT/sce_sys" "$VPK_DATA/sce_sys"
ln -s "$APK" "$VPK_DATA/caustic.apk"

# The temporary, space-free VitaSDK symlink changes on every invocation. A
# cached toolchain path would therefore point at a deleted staging directory.
rm -rf "$BUILD"
mkdir -p "$BUILD"
export VITASDK
export PATH="$VITASDK/bin:$PATH"
if [[ -d "$ROOT/.toolchain/host" ]]; then
    export PYTHONPATH="$ROOT/.toolchain/host${PYTHONPATH:+:$PYTHONPATH}"
fi
if [[ -x "$ROOT/.toolchain/host/bin/cmake" ]]; then
    CMAKE="$ROOT/.toolchain/host/bin/cmake"
else
    CMAKE="$(command -v cmake || true)"
fi
if [[ -z "$CMAKE" ]]; then
    echo "CMake was not found. Install CMake or provide .toolchain/host/bin/cmake." >&2
    exit 1
fi

"$CMAKE" -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DVPK_DATA_ROOT="$VPK_DATA" -DCAUSTIC_PROFILE="$PROFILE" \
    -DCAUSTIC_INCLUDE_EXTRAS="$INCLUDE_EXTRAS"
"$CMAKE" --build "$BUILD" --parallel

echo "Built: $BUILD/Caustic3Vita.vpk"

# Vita3K can become extremely slow while deflating the hundreds of small Caustic
# preset files. Generate its larger store-only archive only when requested.
if [[ "${BUILD_FAST_INSTALL:-0}" == "1" ]]; then
python3 - "$BUILD/Caustic3Vita.vpk" "$BUILD/Caustic3Vita-fast-install.vpk" <<'PY'
import shutil
import sys
import zipfile

source, destination = sys.argv[1:]
with zipfile.ZipFile(source, "r") as src, zipfile.ZipFile(
    destination, "w", compression=zipfile.ZIP_STORED, allowZip64=True
) as dst:
    for entry in src.infolist():
        copied = zipfile.ZipInfo(entry.filename, entry.date_time)
        copied.comment = entry.comment
        copied.extra = entry.extra
        copied.create_system = entry.create_system
        copied.external_attr = entry.external_attr
        copied.internal_attr = entry.internal_attr
        copied.flag_bits = entry.flag_bits & ~0x08
        with src.open(entry, "r") as reader, dst.open(copied, "w") as writer:
            shutil.copyfileobj(reader, writer, length=1024 * 1024)
print(f"Built: {destination} (store-only, optimized for Vita3K installation)")
PY
fi
