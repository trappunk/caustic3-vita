#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

forbidden=$(find . \
    \( -path './.git' -o -path './build-*' \) -prune -o -type f \
    \( -iname '*.apk' -o -iname '*.vpk' -o -iname '*.so' \
       -o -iname '*.suprx' -o -iname '*.skprx' -o -iname '*.raw' \) \
    -print)
if [[ -n "$forbidden" ]]; then
    echo "Proprietary/generated files are not allowed:" >&2
    echo "$forbidden" >&2
    exit 1
fi

if find . \( -path './.git' -o -path './build-*' \) -prune -o \
    -type f -size +10M -print -quit | grep -q .; then
    echo "Unexpected file larger than 10 MiB:" >&2
    find . \( -path './.git' -o -path './build-*' \) -prune -o \
        -type f -size +10M -print >&2
    exit 1
fi

developer_root="/Users"
if grep -RIl --exclude-dir=.git --exclude-dir='build-*' -- "$developer_root/" . | grep -q .; then
    echo "Absolute developer paths are not allowed:" >&2
    grep -RIl --exclude-dir=.git --exclude-dir='build-*' -- "$developer_root/" . >&2
    exit 1
fi

echo "public tree verification passed"
