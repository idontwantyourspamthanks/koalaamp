#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# fetch-skin.sh — download the FE-3 test fixture (Nullsoft's `base-2.91.wsz`)
# into the gitignored third_party/ tree. The artwork is Nullsoft's; it is never
# committed (AGENTS.md) and is fetched only for local/spike verification.
#
# Source: Winamp Skin Museum, MD5 5e4f10275dcb1fb211d4a8b4f1bda236
# https://skins.webamp.org/skin/5e4f10275dcb1fb211d4a8b4f1bda236/base-2.91.wsz/
# (bytes served by the museum CDN, r2.webampskins.org; MD5 verified on fetch).
#
# Usage: spikes/fe3_wsz/fetch-skin.sh [dest-dir]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dest="${1:-${root}/third_party/skins}"

md5="5e4f10275dcb1fb211d4a8b4f1bda236"
url="https://r2.webampskins.org/skins/${md5}.wsz"
out="${dest}/base-2.91.wsz"

mkdir -p "${dest}"
if [[ -s "${out}" ]] && [[ "$(md5sum "${out}" | cut -d' ' -f1)" == "${md5}" ]]; then
    echo "up to date: ${out}"
    exit 0
fi

curl -fsSL -o "${out}.tmp" "${url}"
actual="$(md5sum "${out}.tmp" | cut -d' ' -f1)"
if [[ "${actual}" != "${md5}" ]]; then
    rm -f "${out}.tmp"
    echo "fetch-skin: MD5 mismatch (got ${actual}, want ${md5})" >&2
    exit 1
fi
mv "${out}.tmp" "${out}"
echo "fetched: ${out} (${actual})"
