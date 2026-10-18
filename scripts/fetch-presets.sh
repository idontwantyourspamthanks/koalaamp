#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# fetch-presets.sh — download a pinned slice of the community "cream of the crop"
# MilkDrop pack for the visualizer's preset library and the nightly soak.
#
# Presets are community-authored and their licences are not uniformly documented,
# so like the skin corpora they are fetched into the gitignored third_party/ tree
# and never committed (AGENTS.md). Fetching at a pinned commit keeps the corpus
# reproducible; the slice is taken in sorted order so the same pin always yields
# the same files.
#
# Usage: scripts/fetch-presets.sh [count] [dest-dir]
#        (defaults: 500 presets into third_party/presets/soak)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
count="${1:-500}"
dest="${2:-${root}/third_party/presets/soak}"

# The FE-14 pin (see spikes/fe14_projectm/fetch-presets.sh); the pack holds 9795
# presets at this commit, so any slice up to that is available.
commit="0180df21f5e0bd39b9060cc5de420ed2f1f9e509"
url="https://codeload.github.com/projectM-visualizer/presets-cream-of-the-crop/tar.gz/${commit}"

have() { find "$dest" -type f \( -name '*.milk' -o -name '*.milk2' \) 2>/dev/null | wc -l; }

mkdir -p "$dest"
if [ "$(have)" -ge "$count" ]; then
    echo "fetch-presets: $dest already holds $(have) presets (>= $count); nothing to do"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "fetch-presets: downloading the pinned pack (${commit:0:8}) ..."
curl -fsSL "$url" -o "$tmp/pack.tar.gz"
tar -xzf "$tmp/pack.tar.gz" -C "$tmp"

echo "fetch-presets: taking the first $count presets, sorted ..."
# Numbered prefixes keep the soak's order stable and make the slice obvious.
i=0
while IFS= read -r preset; do
    i=$((i + 1))
    cp "$preset" "$dest/$(printf '%05d' "$i")-$(basename "$preset")"
done < <(find "$tmp" -type f \( -name '*.milk' -o -name '*.milk2' \) | LC_ALL=C sort | head -n "$count")

echo "fetch-presets: $(have) presets in $dest"
